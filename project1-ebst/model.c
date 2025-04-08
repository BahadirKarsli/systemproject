#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "model.h"

#define errExit(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

Model *model_init(const char *username) {
    Model *model = malloc(sizeof(Model));
    model->processes = NULL;
    model->process_count = 0;
    model->cmd_count = 0;
    strncpy(model->username, username, MAX_USERNAME - 1);
    model->username[MAX_USERNAME - 1] = '\0';

    int fd = shm_open(SHARED_FILE_PATH, O_CREAT | O_RDWR, 0600);
    if (fd < 0) errExit("shm_open failed");

    struct stat shm_stat;
    int is_new = (fstat(fd, &shm_stat) == 0 && shm_stat.st_size == 0);
    if (is_new && ftruncate(fd, sizeof(ShmBuf)) == -1) errExit("ftruncate failed");

    model->shmp = mmap(NULL, sizeof(ShmBuf), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (model->shmp == MAP_FAILED) errExit("mmap failed");

    model->shmp->fd = fd;

    if (is_new) {
        model->shmp->cnt = 0;
        model->shmp->msg_index = 0;
        sem_init(&model->shmp->sem, 1, 1);
        printf("Initialized new shared memory for %s: cnt=%zu, msg_index=%d\n", username, model->shmp->cnt, model->shmp->msg_index);
    } else {
        printf("Attached to existing shared memory for %s: cnt=%zu, msg_index=%d\n", username, model->shmp->cnt, model->shmp->msg_index);
    }

    return model;
}

void model_destroy(Model *model) {
    if (model->shmp) {
        if (strcmp(model->username, "User1") == 0) {
            sem_destroy(&model->shmp->sem);
            munmap(model->shmp, sizeof(ShmBuf));
            shm_unlink(SHARED_FILE_PATH);
            printf("Destroyed shared memory by %s\n", model->username);
        } else {
            munmap(model->shmp, sizeof(ShmBuf));
            printf("Detached shared memory by %s\n", model->username);
        }
    }
    free(model->processes);
    free(model);
}

void model_execute_command(Model *model, const char *command, char *output, size_t output_size) {
    int pipefd[2];
    if (pipe(pipefd) == -1) errExit("pipe failed");

    pid_t pid = fork();
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        char *args[MAX_COMMAND];
        char cmd_copy[MAX_COMMAND];
        strncpy(cmd_copy, command, MAX_COMMAND);
        int argc = 0;
        args[argc++] = strtok(cmd_copy, " ");
        while ((args[argc] = strtok(NULL, " "))) argc++;
        execvp(args[0], args);
        perror("execvp failed");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        close(pipefd[1]);
        ssize_t n = read(pipefd[0], output, output_size - 1);
        if (n > 0) output[n] = '\0';
        else output[0] = '\0';
        close(pipefd[0]);

        model->processes = realloc(model->processes, sizeof(ProcessInfo) * (model->process_count + 1));
        ProcessInfo *p = &model->processes[model->process_count++];
        p->pid = pid;
        strncpy(p->command, command, MAX_COMMAND);
        p->status = 0;
        waitpid(pid, &p->status, 0);
        p->status = 1;

        if (model->cmd_count < MAX_HISTORY) {
            strncpy(model->command_history[model->cmd_count++], command, MAX_COMMAND);
        } else {
            memmove(model->command_history[0], model->command_history[1], (MAX_HISTORY - 1) * MAX_COMMAND);
            strncpy(model->command_history[MAX_HISTORY - 1], command, MAX_COMMAND);
        }
    } else {
        errExit("fork failed");
    }
}

void model_send_message(Model *model, const char *message) {
    sem_wait(&model->shmp->sem);
    int idx = model->shmp->msg_index % MAX_HISTORY;
    strncpy(model->shmp->messages[idx].sender, model->username, MAX_USERNAME);
    model->shmp->messages[idx].type = 0;
    strncpy(model->shmp->messages[idx].data, message, MAX_FILE_SIZE);
    model->shmp->messages[idx].data_size = strlen(message) + 1;
    model->shmp->msg_index++;
    model->shmp->cnt = (model->shmp->cnt < MAX_HISTORY) ? model->shmp->cnt + 1 : MAX_HISTORY;
    printf("Sent message: [%s] %s (index: %d, cnt: %zu)\n", model->username, message, idx, model->shmp->cnt);
    sem_post(&model->shmp->sem);
}

void model_send_file(Model *model, const char *filename) {
    sem_wait(&model->shmp->sem);
    int idx = model->shmp->msg_index % MAX_HISTORY;

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open file");
        sem_post(&model->shmp->sem);
        return;
    }
    ssize_t bytes_read = read(fd, model->shmp->messages[idx].data, MAX_FILE_SIZE - 1);
    if (bytes_read < 0) {
        perror("Failed to read file");
        close(fd);
        sem_post(&model->shmp->sem);
        return;
    }
    model->shmp->messages[idx].data[bytes_read] = '\0';
    close(fd);

    strncpy(model->shmp->messages[idx].sender, model->username, MAX_USERNAME);
    model->shmp->messages[idx].type = 1;
    strncpy(model->shmp->messages[idx].filename, filename, MAX_COMMAND);
    model->shmp->messages[idx].data_size = bytes_read;
    model->shmp->msg_index++;
    model->shmp->cnt = (model->shmp->cnt < MAX_HISTORY) ? model->shmp->cnt + 1 : MAX_HISTORY;
    printf("Sent file: [%s] %s (%zu bytes, index: %d, cnt: %zu)\n", model->username, filename, bytes_read, idx, model->shmp->cnt);
    sem_post(&model->shmp->sem);
}

void model_read_messages(Model *model, char *buffer, size_t buffer_size) {
    static char last_buffer[BUF_SIZE * MAX_HISTORY] = {0}; // Track last content per user
    sem_wait(&model->shmp->sem);
    buffer[0] = '\0';
    size_t offset = 0;
    for (int i = 0; i < model->shmp->cnt; i++) {
        int idx = (model->shmp->msg_index - model->shmp->cnt + i) % MAX_HISTORY;
        if (idx < 0) idx += MAX_HISTORY;
        char line[BUF_SIZE + MAX_USERNAME + MAX_COMMAND + 20];
        if (model->shmp->messages[idx].type == 0) {
            snprintf(line, sizeof(line), "[%s] %.*s\n", 
                     model->shmp->messages[idx].sender, 
                     (int)(sizeof(line) - MAX_USERNAME - 5), 
                     model->shmp->messages[idx].data);
        } else {
            snprintf(line, sizeof(line), "[%s] File: %s (%zu bytes)\n", 
                     model->shmp->messages[idx].sender, 
                     model->shmp->messages[idx].filename, 
                     model->shmp->messages[idx].data_size);
        }
        strncat(buffer, line, buffer_size - offset - 1);
        offset += strlen(line);
    }
    if (offset > 0 && strcmp(buffer, last_buffer) != 0) {
        printf("Read messages for %s: cnt=%zu, msg_index=%d, content:\n%s", 
               model->username, model->shmp->cnt, model->shmp->msg_index, buffer);
        strncpy(last_buffer, buffer, sizeof(last_buffer));
    }
    sem_post(&model->shmp->sem);
}