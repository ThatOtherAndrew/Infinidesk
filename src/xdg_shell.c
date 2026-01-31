/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * xdg_shell.c - XDG shell protocol handling
 */

#define _POSIX_C_SOURCE 200809L

#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "infinidesk/xdg_shell.h"
#include "infinidesk/server.h"
#include "infinidesk/view.h"

void xdg_shell_init(struct infinidesk_server *server) {
    /* Create the XDG shell */
    server->xdg_shell = wlr_xdg_shell_create(server->wl_display, 6);
    if (!server->xdg_shell) {
        wlr_log(WLR_ERROR, "Failed to create XDG shell");
        return;
    }

    /* Listen for new toplevel surfaces */
    server->new_xdg_toplevel.notify = handle_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel,
                  &server->new_xdg_toplevel);

    /* Listen for new popup surfaces */
    server->new_xdg_popup.notify = handle_new_xdg_popup;
    wl_signal_add(&server->xdg_shell->events.new_popup,
                  &server->new_xdg_popup);

    wlr_log(WLR_DEBUG, "XDG shell initialised");
}

void handle_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct infinidesk_server *server =
        wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;

    wlr_log(WLR_INFO, "New XDG toplevel: %s (%s)",
            xdg_toplevel->title ?: "(untitled)",
            xdg_toplevel->app_id ?: "(no app_id)");

    /* Create a view for this toplevel */
    struct infinidesk_view *view = view_create(server, xdg_toplevel);
    if (!view) {
        wlr_log(WLR_ERROR, "Failed to create view for toplevel");
        return;
    }

    wlr_log(WLR_DEBUG, "Created view %p for toplevel", (void *)view);
}

void handle_new_xdg_popup(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_popup *xdg_popup = data;

    wlr_log(WLR_DEBUG, "New XDG popup");

    /*
     * Popups need to be attached to the scene graph.
     * We find the parent surface and create the popup in its scene tree.
     */
    struct wlr_xdg_surface *parent_surface =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    if (!parent_surface) {
        wlr_log(WLR_ERROR, "Popup has no parent XDG surface");
        return;
    }

    /* Get the parent's scene tree */
    struct wlr_scene_tree *parent_tree = parent_surface->data;
    if (!parent_tree) {
        wlr_log(WLR_ERROR, "Parent surface has no scene tree");
        return;
    }

    /*
     * Create the popup in the scene graph.
     * wlr_scene_xdg_surface_create handles the popup positioning
     * relative to its parent automatically.
     */
    struct wlr_scene_tree *popup_tree =
        wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
    if (!popup_tree) {
        wlr_log(WLR_ERROR, "Failed to create scene tree for popup");
        return;
    }

    /* Store reference to the scene tree in the xdg_surface */
    xdg_popup->base->data = popup_tree;

    wlr_log(WLR_DEBUG, "Created popup scene tree");
}
