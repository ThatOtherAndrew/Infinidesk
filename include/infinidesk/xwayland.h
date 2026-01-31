/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * xwayland.h - XWayland integration
 */

#ifndef INFINIDESK_XWAYLAND_H
#define INFINIDESK_XWAYLAND_H

#include <wayland-server-core.h>

struct infinidesk_server;

bool xwayland_init(struct infinidesk_server *server);
void handle_xwayland_ready(struct wl_listener *listener, void *data);
void handle_xwayland_new_surface(struct wl_listener *listener, void *data);
void xwayland_finish(struct infinidesk_server *server);

#endif /* INFINIDESK_XWAYLAND_H */
