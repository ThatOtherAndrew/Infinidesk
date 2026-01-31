/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * cursor.h - Cursor and pointer input handling
 */

#ifndef INFINIDESK_CURSOR_H
#define INFINIDESK_CURSOR_H

#include <wayland-server-core.h>

/* Forward declaration */
struct infinidesk_server;

/*
 * Initialise cursor handling for the server.
 */
void cursor_init(struct infinidesk_server *server);

/*
 * Handle relative cursor motion events.
 */
void cursor_handle_motion(struct wl_listener *listener, void *data);

/*
 * Handle absolute cursor motion events (e.g., from tablets).
 */
void cursor_handle_motion_absolute(struct wl_listener *listener, void *data);

/*
 * Handle cursor button events.
 */
void cursor_handle_button(struct wl_listener *listener, void *data);

/*
 * Handle cursor axis events (scroll wheel, touchpad scroll).
 */
void cursor_handle_axis(struct wl_listener *listener, void *data);

/*
 * Handle cursor frame events.
 */
void cursor_handle_frame(struct wl_listener *listener, void *data);

/*
 * Handle seat request_set_cursor events.
 */
void cursor_handle_request_cursor(struct wl_listener *listener, void *data);

/*
 * Process cursor motion and update focus.
 */
void cursor_process_motion(struct infinidesk_server *server, uint32_t time);

/*
 * Reset cursor mode to passthrough.
 */
void cursor_reset_mode(struct infinidesk_server *server);

#endif /* INFINIDESK_CURSOR_H */
