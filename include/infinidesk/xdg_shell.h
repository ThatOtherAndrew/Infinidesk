/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * xdg_shell.h - XDG shell protocol handling
 */

#ifndef INFINIDESK_XDG_SHELL_H
#define INFINIDESK_XDG_SHELL_H

#include <wayland-server-core.h>

/* Forward declaration */
struct infinidesk_server;

/*
 * Initialise XDG shell handling for the server.
 */
void xdg_shell_init(struct infinidesk_server *server);

/*
 * Handle a new XDG toplevel surface.
 */
void handle_new_xdg_toplevel(struct wl_listener *listener, void *data);

/*
 * Handle a new XDG popup surface.
 */
void handle_new_xdg_popup(struct wl_listener *listener, void *data);

#endif /* INFINIDESK_XDG_SHELL_H */
