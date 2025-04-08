#include "controller.h"
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

Controller *controller_init(const char *username) {
    Controller *ctrl = malloc(sizeof(Controller));
    ctrl->model = model_init(username);
    ctrl->view = view_init(controller_handle_input, ctrl);
    return ctrl;
}

void controller_handle_input(const char *input, void *data) {
    Controller *ctrl = (Controller *)data;
    char output[BUF_SIZE] = {0};

    printf("Controller received input: %s\n", input);

    if (strncmp(input, "@msg ", 5) == 0) {
        model_send_message(ctrl->model, input + 5);
    } else if (strncmp(input, "@file ", 6) == 0) {
        model_send_file(ctrl->model, input + 6);
    } else if (strcmp(input, "nano") == 0 || strncmp(input, "nano ", 5) == 0) {
        pid_t pid = fork();
        if (pid == 0) { // Child process
            // Extract filename (if any)
            const char *filename = (strlen(input) > 5) ? input + 5 : "";
            char nano_cmd[BUF_SIZE];
            
            // Open VS Code and send nano command to terminal
            snprintf(nano_cmd, sizeof(nano_cmd), 
                     "code %s -r && code -r -w --command \"workbench.action.terminal.focus\" && code -r -w --command \"workbench.action.terminal.sendSequence\" --args \"\\\"nano %s\\\"\"",
                     filename, filename);
            
            printf("Executing in child: %s\n", nano_cmd);
            execlp("sh", "sh", "-c", nano_cmd, (char *)NULL);
            perror("execlp failed");
            exit(EXIT_FAILURE);
        } else if (pid > 0) { // Parent
            printf("Spawned VS Code with nano, PID: %d\n", pid);
            // Don’t wait here, let child run independently
        } else {
            perror("fork failed");
            strncpy(output, "Error: Failed to launch nano in VS Code\n", BUF_SIZE);
            view_update_output(ctrl->view, output);
        }
    } else {
        model_execute_command(ctrl->model, input, output, BUF_SIZE);
        view_update_output(ctrl->view, output);
    }
}

void controller_destroy(Controller *controller) {
    view_destroy(controller->view);
    model_destroy(controller->model);
    free(controller);
}

int main() {
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