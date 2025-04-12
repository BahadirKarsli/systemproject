#include "view.h"
#include <stdio.h>
#include <string.h>
#include "controller.h"

static void on_entry_activate(GtkEntry *entry, gpointer data) {
    View *view = (View *)data;
    const char *input = gtk_entry_get_text(entry);
    printf("Entry activated with input: %s\n", input);
    view->on_command(input, view->controller);
    gtk_entry_set_text(entry, "");
}

static gboolean update_messages(gpointer data) {
    View *view = (View *)data;
    Controller *ctrl = (Controller *)view->controller;
    char buffer[BUF_SIZE * MAX_HISTORY];
    model_read_messages(ctrl->model, buffer, sizeof(buffer));

    size_t len = strlen(buffer);
    while (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }

    if (buffer[0] != '\0' && strcmp(buffer, view->last_message) != 0) {
        char buffer_copy[BUF_SIZE * MAX_HISTORY];
        strncpy(buffer_copy, buffer, sizeof(buffer_copy) - 1);
        buffer_copy[sizeof(buffer_copy) - 1] = '\0';

        GtkTextBuffer *msg_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->message_text));
        gtk_text_buffer_set_text(msg_buffer, "", -1);
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(msg_buffer, &end);

        char *line = strtok(buffer_copy, "\n");
        while (line != NULL && line[0] != '\0') {
            char sender[MAX_USERNAME];
            char timestamp[32];
            char content[BUF_SIZE];
            if (sscanf(line, "[%[^]]] [%[^]]] %[^\n]", sender, timestamp, content) == 3) {
                const char *tag_name = (strcmp(sender, "User1") == 0) ? "user1" : "user2";
                char formatted_line[BUF_SIZE];
                // content'ın uzunluğunu sınırlandır
                int max_content_len = BUF_SIZE - (strlen(sender) + strlen(timestamp) + 10); // 10: sabit karakterler için
                if (max_content_len < 0) max_content_len = 0;
                snprintf(formatted_line, sizeof(formatted_line), "[%s] [%s] %.*s", 
                         sender, timestamp, max_content_len, content);
                gtk_text_buffer_insert_with_tags_by_name(msg_buffer, &end, formatted_line, -1, tag_name, NULL);
                gtk_text_buffer_insert(msg_buffer, &end, "\n", -1);
            } else if (sscanf(line, "[%[^]]] File: %[^\n]", sender, content) == 2) {
                const char *tag_name = (strcmp(sender, "User1") == 0) ? "user1" : "user2";
                gtk_text_buffer_insert_with_tags_by_name(msg_buffer, &end, line, -1, tag_name, NULL);
                gtk_text_buffer_insert(msg_buffer, &end, "\n", -1);
            }
            line = strtok(NULL, "\n");
        }
        strncpy(view->last_message, buffer, sizeof(view->last_message) - 1);
        view->last_message[sizeof(view->last_message) - 1] = '\0';
        printf("Updated UI with: %s\n", buffer);
    }
    return TRUE;
}

View *view_init(void (*on_command)(const char *input, void *data), void *controller) {
    gtk_init(NULL, NULL);
    View *view = malloc(sizeof(View));
    if (!view) {
        fprintf(stderr, "Failed to allocate View\n");
        return NULL;
    }
    view->on_command = on_command;
    view->controller = controller;
    view->last_message[0] = '\0';

    view->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(view->window), "");
    gtk_window_set_default_size(GTK_WINDOW(view->window), 700, 500);
    g_signal_connect(view->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(view->window), vbox);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "window {"
        "    background-color: #000000;"
        "    border-radius: 8px;"
        "}"
        "textview, scrolledwindow textview, textview text {"
        "    background-color: #000000;"
        "    color: #00ff00;"
        "    font-family: 'IBM Plex Mono', 'Courier New', Monospace;"
        "    font-size: 14px;"
        "    padding: 8px;"
        "    background-image: linear-gradient(rgba(255, 255, 255, 0.05) 1px, transparent 1px);"
        "    background-size: 100% 4px;"
        "    border: 1px solid #00ff00;"
        "    border-radius: 4px;"
        "    box-shadow: 0 0 3px #00ff00;"
        "}"
        "entry {"
        "    background-color: #000000;"
        "    color: #00ff00;"
        "    border: 1px solid #00ff00;"
        "    font-family: 'IBM Plex Mono', 'Courier New', Monospace;"
        "    font-size: 13px;"
        "    padding: 10px;"
        "    box-shadow: 0 0 3px #00ff00;"
        "}"
        "entry:focus {"
        "    box-shadow: 0 0 3px #00ff00;"
        "}"
        "label {"
        "    color: #00ff00;"
        "    font-family: 'IBM Plex Mono', 'Courier New', Monospace;"
        "    font-size: 12px;"
        "    font-weight: bold;"
        "    padding: 3px 0;"
        "}"
        "scrolledwindow {"
        "    background-color: #000000;"
        "    border: none;"
        "}"
        "scrolledwindow textview {"
        "    background-color: #000000;"
        "    color: #00ff00;"
        "}"
        ,
        -1, NULL);
    
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
                                              GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    GtkWidget *output_label = gtk_label_new("Command Output & History");
    gtk_box_pack_start(GTK_BOX(vbox), output_label, FALSE, FALSE, 0);
    view->output_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view->output_text), FALSE);
    GtkWidget *output_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(output_scrolled), view->output_text);
    gtk_box_pack_start(GTK_BOX(vbox), output_scrolled, TRUE, TRUE, 0);

    GtkWidget *message_label = gtk_label_new("Message History");
    gtk_box_pack_start(GTK_BOX(vbox), message_label, FALSE, FALSE, 0);
    view->message_text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view->message_text), FALSE);
    GtkWidget *message_scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(message_scrolled), view->message_text);
    gtk_box_pack_start(GTK_BOX(vbox), message_scrolled, TRUE, TRUE, 0);

    GtkTextBuffer *msg_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->message_text));
    gtk_text_buffer_create_tag(msg_buffer, "user1", "foreground", "#ff5555", NULL);
    gtk_text_buffer_create_tag(msg_buffer, "user2", "foreground", "#55ff55", NULL);

    GtkWidget *entry_label = gtk_label_new("Enter Command");
    gtk_box_pack_start(GTK_BOX(vbox), entry_label, FALSE, FALSE, 0);
    view->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->entry), "Type here...");
    g_signal_connect(view->entry, "activate", G_CALLBACK(on_entry_activate), view);
    gtk_box_pack_start(GTK_BOX(vbox), view->entry, FALSE, FALSE, 0);

    GtkWidget *status_bar = gtk_label_new("Ready");
    gtk_box_pack_start(GTK_BOX(vbox), status_bar, FALSE, FALSE, 0);

    gtk_widget_show_all(view->window);
    g_timeout_add(500, update_messages, view);

    printf("View initialized\n");
    return view;
}

void view_update_output(View *view, const char *output) {
    if (!view || !view->output_text) return;
    Controller *ctrl = (Controller *)view->controller;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->output_text));
    gtk_text_buffer_set_text(buffer, "", -1);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);

    for (int i = 0; i < ctrl->model->cmd_count; i++) {
        char line[MAX_COMMAND + 10];
        snprintf(line, sizeof(line), "> %s\n", ctrl->model->command_history[i]);
        gtk_text_buffer_insert(buffer, &end, line, -1);
    }
    gtk_text_buffer_insert(buffer, &end, output, -1);
    printf("Output updated: %s\n", output);
}

void view_destroy(View *view) {
    if (!view) return;
    gtk_widget_destroy(view->window);
    free(view);
    printf("View destroyed\n");
}