#ifndef MODEL_H
#define MODEL_H

#include <semaphore.h>
#include <sys/types.h>

#define BUF_SIZE 4096
#define SHARED_FILE_PATH "/mymsgbuf"
#define MAX_COMMAND 256
#define MAX_USERNAME 32
#define MAX_HISTORY 50
#define MAX_FILE_SIZE (BUF_SIZE * 2) // Allow larger files (8KB)

typedef struct {
    pid_t pid;
    char command[MAX_COMMAND];
    int status;
} ProcessInfo;

typedef struct {
    char sender[MAX_USERNAME];
    int type; // 0 = text message, 1 = file transfer
    char filename[MAX_COMMAND]; // Name of file being sent (if type == 1)
    char data[MAX_FILE_SIZE]; // Message or file content
    size_t data_size; // Size of data
} MessageEntry;

typedef struct shmbuf {
    sem_t sem;
    size_t cnt;
    int fd;
    MessageEntry messages[MAX_HISTORY];
    int msg_index;
} ShmBuf;

typedef struct {
    ProcessInfo *processes;
    int process_count;
    ShmBuf *shmp;
    char username[MAX_USERNAME];
    char command_history[MAX_HISTORY][MAX_COMMAND];
    int cmd_count;
} Model;

Model *model_init(const char *username);
void model_destroy(Model *model);
void model_execute_command(Model *model, const char *command, char *output, size_t output_size);
void model_send_message(Model *model, const char *message);
void model_send_file(Model *model, const char *filename);
void model_read_messages(Model *model, char *buffer, size_t buffer_size);

#endif