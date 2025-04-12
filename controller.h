#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "model.h"
#include "view.h"

typedef struct {
    Model *model;
    View *view;
} Controller;

Controller *controller_init(const char *username);
void controller_handle_input(const char *input, void *data);
void controller_destroy(Controller *controller);

#endif