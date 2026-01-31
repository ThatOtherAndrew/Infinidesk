/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * input.h - Input device management
 */

#ifndef INFINIDESK_INPUT_H
#define INFINIDESK_INPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>

/* Forward declaration */
struct infinidesk_server;

/*
 * Set up input handling for the server.
 * This connects the new_input signal handler.
 */
void input_init(struct infinidesk_server *server);

/*
 * Handle a new input device being added.
 */
void handle_new_input(struct wl_listener *listener, void *data);

#endif /* INFINIDESK_INPUT_H */
