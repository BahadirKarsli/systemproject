#ifndef VIEW_H
#define VIEW_H

#include <gtk/gtk.h>

#define BUF_SIZE 4096
#define MAX_HISTORY 50

typedef struct {
    GtkWidget *window;
    GtkWidget *output_text;
    GtkWidget *message_text;
    GtkWidget *entry;
    char last_message[BUF_SIZE * MAX_HISTORY];
    void (*on_command)(const char *input, void *data);
    void *controller;
} View;

View *view_init(void (*on_command)(const char *input, void *data), void *data);
void view_update_output(View *view, const char *output);
void view_append_message(View *view, const char *sender, const char *message);
void view_destroy(View *view);

#endif