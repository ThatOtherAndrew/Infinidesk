/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * xwayland.c - XWayland integration
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <wlr/xwayland/xwayland.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "infinidesk/xwayland.h"
#include "infinidesk/server.h"
#include "infinidesk/view.h"

bool xwayland_init(struct infinidesk_server *server) {
    wlr_log(WLR_DEBUG, "Initializing XWayland");

    server->xwayland = wlr_xwayland_create(server->wl_display, server->compositor, false);
    if (!server->xwayland) {
        wlr_log(WLR_ERROR, "Failed to create XWayland server");
        return false;
    }

    server->xwayland_ready.notify = handle_xwayland_ready;
    wl_signal_add(&server->xwayland->events.ready, &server->xwayland_ready);

    server->xwayland_new_surface.notify = handle_xwayland_new_surface;
    wl_signal_add(&server->xwayland->events.new_surface, &server->xwayland_new_surface);

    wlr_log(WLR_INFO, "XWayland initialized");
    return true;
}

void handle_xwayland_ready(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_server *server = wl_container_of(listener, server, xwayland_ready);

    wlr_log(WLR_INFO, "XWayland server ready on display %s", server->xwayland->display_name);

    /* Set the seat for XWayland */
    wlr_xwayland_set_seat(server->xwayland, server->seat);

    /* Set DISPLAY environment variable for X11 clients */
    setenv("DISPLAY", server->xwayland->display_name, true);

    /*
     * Force GTK and Electron apps to use X11 instead of Wayland.
     * Without this, apps like Firefox and VSCode will try to connect
     * to the parent Wayland compositor instead of using XWayland.
     */
    setenv("GDK_BACKEND", "x11", true);
    setenv("ELECTRON_OZONE_PLATFORM_HINT", "x11", true);
    setenv("QT_QPA_PLATFORM", "xcb", true);

    /* Print to stderr so user can easily see the display for launching X11 apps */
    fprintf(stderr, "\n======================================\n");
    fprintf(stderr, "XWayland ready: DISPLAY=%s\n", server->xwayland->display_name);
    fprintf(stderr, "All apps will use X11 (XWayland)\n");
    fprintf(stderr, "======================================\n\n");

    wlr_log(WLR_DEBUG, "Set DISPLAY=%s", server->xwayland->display_name);
    wlr_log(WLR_DEBUG, "Forcing X11 backend for GTK/Electron/Qt apps");

    /* Run startup command now that XWayland is ready */
    if (server->startup_cmd) {
        wlr_log(WLR_INFO, "Running startup command: %s", server->startup_cmd);
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", server->startup_cmd, (char *)NULL);
            _exit(EXIT_FAILURE);
        }
    }
}

void handle_xwayland_new_surface(struct wl_listener *listener, void *data) {
    struct infinidesk_server *server = wl_container_of(listener, server, xwayland_new_surface);
    struct wlr_xwayland_surface *xwayland_surface = data;

    wlr_log(WLR_DEBUG, "New XWayland surface: title=%s, class=%s, override_redirect=%d",
            xwayland_surface->title ?: "(null)",
            xwayland_surface->class ?: "(null)",
            xwayland_surface->override_redirect);

    /* Ignore override-redirect windows (tooltips, menus, etc.) in MVP */
    if (xwayland_surface->override_redirect) {
        wlr_log(WLR_DEBUG, "Ignoring override-redirect window");
        return;
    }

    /* Create a view for this XWayland surface */
    struct infinidesk_view *view = view_create_xwayland(server, xwayland_surface);
    if (!view) {
        wlr_log(WLR_ERROR, "Failed to create view for XWayland surface");
        return;
    }

    wlr_log(WLR_DEBUG, "Created view %p for XWayland surface", (void *)view);
}

void xwayland_finish(struct infinidesk_server *server) {
    if (!server->xwayland) {
        return;
    }

    wlr_log(WLR_DEBUG, "Cleaning up XWayland");

    wl_list_remove(&server->xwayland_ready.link);
    wl_list_remove(&server->xwayland_new_surface.link);

    /* XWayland server is destroyed with the display */
}
