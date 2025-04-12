#include "controller.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>

// Komut ayrıştırma için yardımcı yapı
typedef struct {
    char *args[10]; // Maksimum 10 argüman
    int arg_count;
    char *redirect_in;  // < için
    char *redirect_out; // > için
    int append;         // >> için (1 ise append, 0 ise overwrite)
    char *pipe_next;    // | sonrası komut
} ParsedCommand;

void parse_command(const char *input, ParsedCommand *parsed) {
    char temp[BUF_SIZE];
    strncpy(temp, input, BUF_SIZE - 1);
    temp[BUF_SIZE - 1] = '\0';

    parsed->arg_count = 0;
    parsed->redirect_in = NULL;
    parsed->redirect_out = NULL;
    parsed->append = 0;
    parsed->pipe_next = NULL;

    // Boru kontrolü
    char *pipe_pos = strstr(temp, "|");
    if (pipe_pos) {
        *pipe_pos = '\0';
        parsed->pipe_next = pipe_pos + 1;
        while (isspace(*parsed->pipe_next)) parsed->pipe_next++;
    }

    // Yönlendirme kontrolü
    char *redirect_out = strstr(temp, ">>");
    if (redirect_out) {
        *redirect_out = '\0';
        parsed->redirect_out = redirect_out + 2;
        parsed->append = 1;
        while (isspace(*parsed->redirect_out)) parsed->redirect_out++;
    } else {
        redirect_out = strstr(temp, ">");
        if (redirect_out) {
            *redirect_out = '\0';
            parsed->redirect_out = redirect_out + 1;
            parsed->append = 0;
            while (isspace(*parsed->redirect_out)) parsed->redirect_out++;
        }
    }

    char *redirect_in = strstr(temp, "<");
    if (redirect_in) {
        *redirect_in = '\0';
        parsed->redirect_in = redirect_in + 1;
        while (isspace(*parsed->redirect_in)) parsed->redirect_in++;
    }

    // Argümanları ayrıştır
    char *token = strtok(temp, " ");
    int in_quotes = 0;
    char quoted_arg[BUF_SIZE] = {0};
    int quoted_pos = 0;

    while (token && parsed->arg_count < 10) {
        if (token[0] == '"') {
            in_quotes = 1;
            quoted_pos = 0;
            strncpy(quoted_arg, token + 1, BUF_SIZE - 1);
            quoted_pos = strlen(token) - 1;
        } else if (in_quotes) {
            if (token[strlen(token) - 1] == '"') {
                in_quotes = 0;
                token[strlen(token) - 1] = '\0';
                strncat(quoted_arg, " ", BUF_SIZE - quoted_pos - 1);
                quoted_pos++;
                strncat(quoted_arg, token, BUF_SIZE - quoted_pos - 1);
                parsed->args[parsed->arg_count] = strdup(quoted_arg);
                parsed->arg_count++;
            } else {
                strncat(quoted_arg, " ", BUF_SIZE - quoted_pos - 1);
                quoted_pos++;
                strncat(quoted_arg, token, BUF_SIZE - quoted_pos - 1);
                quoted_pos += strlen(token);
            }
        } else {
            parsed->args[parsed->arg_count] = strdup(token);
            parsed->arg_count++;
        }
        token = strtok(NULL, " ");
    }

    if (in_quotes) {
        // Tırnak kapanmadı, hata
        parsed->args[0] = strdup("echo");
        parsed->args[1] = strdup("Error: Unclosed quotes");
        parsed->arg_count = 2;
    }

    parsed->args[parsed->arg_count] = NULL; // Argüman listesini sonlandır
}

void free_parsed_command(ParsedCommand *parsed) {
    for (int i = 0; i < parsed->arg_count; i++) {
        if (parsed->args[i]) {
            free(parsed->args[i]);
            parsed->args[i] = NULL; // Çift serbest bırakmayı önlemek için NULL yap
        }
    }
    parsed->arg_count = 0;
    parsed->redirect_in = NULL;
    parsed->redirect_out = NULL;
    parsed->append = 0;
    parsed->pipe_next = NULL;
}

// Asenkron komut çalıştırma için yardımcı yapı
typedef struct {
    Controller *ctrl;
    char command[BUF_SIZE];
    char output[BUF_SIZE];
    int pipefd[2];
    pid_t pid;
    ParsedCommand parsed; // Yönlendirme bilgilerini saklamak için
} CommandData;

// Komut tamamlandığında çağrılacak geri çağrı fonksiyonu
static gboolean command_finished(gpointer user_data) {
    CommandData *data = (CommandData *)user_data;
    Controller *ctrl = data->ctrl;

    // Borudan çıktıyı oku
    close(data->pipefd[1]); // Yazma ucunu kapat
    char buffer[BUF_SIZE] = {0};
    ssize_t n = read(data->pipefd[0], buffer, BUF_SIZE - 1);
    if (n > 0) {
        buffer[n] = '\0';
        strncpy(data->output, buffer, BUF_SIZE - 1);
        fprintf(stderr, "Command output: %s\n", data->output); // Debug için stderr'a yaz
    } else {
        data->output[0] = '\0';
        fprintf(stderr, "No output received\n"); // Debug için stderr'a yaz
    }
    close(data->pipefd[0]);

    // Sürecin tamamlanmasını bekle
    int status;
    waitpid(data->pid, &status, 0);
    fprintf(stderr, "Child process %d exited with status %d\n", data->pid, WEXITSTATUS(status)); // Debug için stderr'a yaz

    // Yönlendirme varsa mesaj göster, yoksa çıktıyı göster
    if (data->parsed.redirect_out) {
        snprintf(data->output, BUF_SIZE, "Output redirected to %s\n", data->parsed.redirect_out);
    }
    if (strlen(data->output) > 0) {
        view_update_output(ctrl->view, data->output);
    } else {
        view_update_output(ctrl->view, "Command executed, but no output\n");
    }

    // Belleği serbest bırak
    free_parsed_command(&data->parsed);
    free(data);
    return FALSE; // GSource'u kaldır
}

Controller *controller_init(const char *username) {
    Controller *ctrl = malloc(sizeof(Controller));
    ctrl->model = model_init(username);
    ctrl->view = view_init(controller_handle_input, ctrl);
    return ctrl;
}

void controller_handle_input(const char *input, void *data) {
    Controller *ctrl = (Controller *)data;
    char output[BUF_SIZE] = {0};
    const int MAX_PATH_LEN = 1024;

    fprintf(stderr, "Controller received input: %s\n", input); // Debug için stderr'a yaz

    // Komutu ayrıştır
    ParsedCommand parsed;
    parse_command(input, &parsed);

    if (parsed.arg_count > 0) {
        if (strcmp(parsed.args[0], "cd") == 0) {
            if (parsed.arg_count < 2) {
                snprintf(output, sizeof(output), "cd: missing directory argument\n");
                view_update_output(ctrl->view, output);
            } else {
                char *arg = parsed.args[1];
                arg[MAX_PATH_LEN - 1] = '\0';
                if (chdir(arg) == 0) {
                    char cwd[BUF_SIZE];
                    if (getcwd(cwd, sizeof(cwd)) != NULL) {
                        cwd[MAX_PATH_LEN - 1] = '\0';
                        snprintf(output, sizeof(output), "Changed directory to: %.*s\n", MAX_PATH_LEN, cwd);
                    } else {
                        snprintf(output, sizeof(output), "Changed directory, but failed to get current directory\n");
                    }
                } else {
                    snprintf(output, sizeof(output), "cd: failed to change directory to '%.*s'\n", MAX_PATH_LEN, arg);
                }
                view_update_output(ctrl->view, output);
            }
            free_parsed_command(&parsed);
        } else if (strncmp(input, "@msg ", 5) == 0) {
            model_send_message(ctrl->model, input + 5);
            free_parsed_command(&parsed);
        } else if (strncmp(input, "@file ", 6) == 0) {
            model_send_file(ctrl->model, input + 6);
            free_parsed_command(&parsed);
        } else if (strcmp(parsed.args[0], "nano") == 0) {
            pid_t pid = fork();
            if (pid == 0) {
                const char *filename = (parsed.arg_count > 1) ? parsed.args[1] : "";
                char nano_cmd[BUF_SIZE];
                snprintf(nano_cmd, sizeof(nano_cmd), 
                         "code %s -r && code -r -w --command \"workbench.action.terminal.focus\" && code -r -w --command \"workbench.action.terminal.sendSequence\" --args \"\\\"nano %s\\\"\"",
                         filename, filename);
                fprintf(stderr, "Executing in child: %s\n", nano_cmd); // Debug için stderr'a yaz
                execlp("sh", "sh", "-c", nano_cmd, (char *)NULL);
                perror("execlp failed");
                exit(EXIT_FAILURE);
            } else if (pid > 0) {
                fprintf(stderr, "Spawned VS Code with nano, PID: %d\n", pid); // Debug için stderr'a yaz
            } else {
                perror("fork failed");
                strncpy(output, "Error: Failed to launch nano in VS Code\n", BUF_SIZE);
                view_update_output(ctrl->view, output);
            }
            free_parsed_command(&parsed);
        } else {
            // Boru ve yönlendirme işlemleri
            if (parsed.pipe_next) {
                // Boru işlemi: Gerçek pipe kullanımı
                int pipefd[2] = {-1, -1};
                pid_t pids[10]; // Maksimum 10 boru için
                int pid_count = 0;
                int prev_read_fd = -1;
                int output_pipefd[2] = {-1, -1}; // Son çıktıyı almak için ayrı bir boru
                char *current_input = strdup(input);
                ParsedCommand current_parsed;

                // Son çıktıyı almak için bir boru oluştur
                if (pipe(output_pipefd) == -1) {
                    snprintf(output, sizeof(output), "Error: Failed to create output pipe\n");
                    view_update_output(ctrl->view, output);
                    free(current_input);
                    free_parsed_command(&parsed);
                    return;
                }

                while (current_input) {
                    parse_command(current_input, &current_parsed);
                    free(current_input); // current_input'u serbest bırak

                    // Yeni bir boru oluştur (son komut hariç)
                    if (current_parsed.pipe_next) {
                        if (pipe(pipefd) == -1) {
                            snprintf(output, sizeof(output), "Error: Failed to create pipe\n");
                            view_update_output(ctrl->view, output);
                            free_parsed_command(&current_parsed);
                            close(output_pipefd[0]);
                            close(output_pipefd[1]);
                            return;
                        }
                    }

                    pids[pid_count] = fork();
                    if (pids[pid_count] == 0) { // Çocuk süreç
                        // Giriş yönlendirmesi
                        if (prev_read_fd != -1) {
                            dup2(prev_read_fd, STDIN_FILENO);
                            close(prev_read_fd);
                        }

                        // Çıkış yönlendirmesi
                        if (current_parsed.pipe_next) {
                            close(pipefd[0]); // Okuma ucunu kapat
                            dup2(pipefd[1], STDOUT_FILENO); // Çıktıyı pipe'a yönlendir
                            close(pipefd[1]);
                        } else {
                            // Son komut: Çıktıyı output_pipefd'ye yönlendir
                            if (current_parsed.redirect_out) {
                                int fd = open(current_parsed.redirect_out, O_WRONLY | O_CREAT | (current_parsed.append ? O_APPEND : O_TRUNC), 0644);
                                if (fd == -1) {
                                    perror("Failed to open redirect file");
                                    exit(EXIT_FAILURE);
                                }
                                dup2(fd, STDOUT_FILENO);
                                close(fd);
                            } else {
                                close(output_pipefd[0]); // Okuma ucunu kapat
                                dup2(output_pipefd[1], STDOUT_FILENO); // Çıktıyı output_pipefd'ye yönlendir
                                close(output_pipefd[1]);
                            }
                        }

                        // Komutun temel kısmını oluştur
                        char cmd[BUF_SIZE] = {0};
                        for (int i = 0; i < current_parsed.arg_count; i++) {
                            strncat(cmd, current_parsed.args[i], BUF_SIZE - strlen(cmd) - 1);
                            if (i < current_parsed.arg_count - 1) strncat(cmd, " ", BUF_SIZE - strlen(cmd) - 1);
                        }
                        fprintf(stderr, "Executing command in child: %s\n", cmd); // Debug için stderr'a yaz

                        execlp("sh", "sh", "-c", cmd, (char *)NULL);
                        perror("execlp failed");
                        exit(EXIT_FAILURE);
                    }

                    // Ana süreç: Boruyu kapat ve bir sonraki komuta geç
                    if (prev_read_fd != -1) {
                        close(prev_read_fd);
                    }
                    if (current_parsed.pipe_next) {
                        close(pipefd[1]); // Yazma ucunu kapat
                        prev_read_fd = pipefd[0]; // Okuma ucunu bir sonraki komut için sakla
                        current_input = strdup(current_parsed.pipe_next);
                        pid_count++;
                    } else {
                        current_input = NULL;
                        if (prev_read_fd != -1) {
                            close(prev_read_fd);
                        }
                        if (pipefd[1] != -1) {
                            close(pipefd[1]);
                        }
                    }

                    free_parsed_command(&current_parsed);
                }

                // Tüm süreçlerin tamamlanmasını bekle
                for (int i = 0; i <= pid_count; i++) {
                    int status;
                    waitpid(pids[i], &status, 0);
                    fprintf(stderr, "Child process %d exited with status %d\n", pids[i], WEXITSTATUS(status)); // Debug için stderr'a yaz
                }

                // Son çıktıyı output_pipefd'den oku
                close(output_pipefd[1]); // Yazma ucunu kapat
                if (!parsed.redirect_out) {
                    char buffer[BUF_SIZE] = {0};
                    ssize_t n = read(output_pipefd[0], buffer, BUF_SIZE - 1);
                    if (n > 0) {
                        buffer[n] = '\0';
                        strncpy(output, buffer, BUF_SIZE - 1);
                        fprintf(stderr, "Pipe output: %s\n", output); // Debug için stderr'a yaz
                    } else {
                        snprintf(output, sizeof(output), "No output from pipe\n");
                        fprintf(stderr, "No data read from output_pipefd\n"); // Debug için stderr'a yaz
                    }
                } else {
                    snprintf(output, sizeof(output), "Output redirected to %s\n", parsed.redirect_out);
                }
                close(output_pipefd[0]);

                // Çıktıyı göster
                view_update_output(ctrl->view, output);
            } else {
                // Boru yoksa, komutu asenkron çalıştır
                CommandData *data = malloc(sizeof(CommandData));
                data->ctrl = ctrl;
                strncpy(data->command, input, BUF_SIZE - 1);
                data->command[BUF_SIZE - 1] = '\0';
                data->output[0] = '\0';
                data->parsed = parsed; // Yönlendirme bilgilerini sakla

                if (pipe(data->pipefd) == -1) {
                    snprintf(output, sizeof(output), "Error: Failed to create pipe\n");
                    view_update_output(ctrl->view, output);
                    free(data);
                    free_parsed_command(&parsed);
                    return;
                }

                data->pid = fork();
                if (data->pid == 0) { // Çocuk süreç
                    close(data->pipefd[0]); // Okuma ucunu kapat
                    dup2(data->pipefd[1], STDOUT_FILENO); // Çıktıyı pipe'a yönlendir
                    dup2(data->pipefd[1], STDERR_FILENO); // Hataları da pipe'a yönlendir
                    close(data->pipefd[1]);

                    // Yönlendirme varsa
                    if (parsed.redirect_out) {
                        int fd = open(parsed.redirect_out, O_WRONLY | O_CREAT | (parsed.append ? O_APPEND : O_TRUNC), 0644);
                        if (fd == -1) {
                            perror("Failed to open redirect file");
                            exit(EXIT_FAILURE);
                        }
                        dup2(fd, STDOUT_FILENO);
                        close(fd);
                    }

                    // Komutu çalıştır
                    model_execute_command(ctrl->model, data->command, data->output, BUF_SIZE);
                    exit(0);
                } else if (data->pid > 0) {
                    // Ana süreç: Komut tamamlandığında çağrılacak geri çağrıyı ayarla
                    g_idle_add(command_finished, data);
                } else {
                    perror("fork failed");
                    snprintf(output, sizeof(output), "Error: Failed to execute command\n");
                    view_update_output(ctrl->view, output);
                    free(data);
                    free_parsed_command(&parsed);
                }
                return; // parsed'i burada serbest bırakmamak için return
            }
        }
    }

    free_parsed_command(&parsed);
}

void controller_destroy(Controller *controller) {
    view_destroy(controller->view);
    model_destroy(controller->model);
    free(controller);
}

int main() {
    // Standart çıktıyı kontrol et ve gerekirse sıfırla
    freopen("/dev/tty", "w", stdout);
    freopen("/dev/tty", "w", stderr);
    fprintf(stderr, "Debug output enabled\n"); // Debug için stderr'a yaz

    pid_t pid = fork();
    if (pid == 0) {
        Controller *ctrl = controller_init("User2");
        gtk_main();
        controller_destroy(ctrl);
        exit(0);
    } else if (pid > 0) {
        Controller *ctrl = controller_init("User1");
        gtk_main();
        controller_destroy(ctrl);
        wait(NULL);
    } else {
        perror("fork failed");
        return 1;
    }
    return 0;
}