/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * server.h - Core server state and initialisation
 */

#ifndef INFINIDESK_SERVER_H
#define INFINIDESK_SERVER_H

#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

#include "infinidesk/canvas.h"

/* Forward declarations */
struct infinidesk_view;
struct infinidesk_output;
struct infinidesk_keyboard;

/* Cursor interaction modes */
enum infinidesk_cursor_mode {
    INFINIDESK_CURSOR_PASSTHROUGH,  /* Normal - events go to clients */
    INFINIDESK_CURSOR_MOVE,         /* Moving a window (Super + drag) */
    INFINIDESK_CURSOR_PAN,          /* Panning the canvas */
    INFINIDESK_CURSOR_RESIZE,       /* Resizing a window (future) */
};

/* Main server state */
struct infinidesk_server {
    /* Wayland core */
    struct wl_display *wl_display;
    struct wl_event_loop *event_loop;

    /* wlroots core */
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_compositor *compositor;
    struct wlr_subcompositor *subcompositor;
    struct wlr_data_device_manager *data_device_manager;

    /* Scene graph */
    struct wlr_scene *scene;
    struct wlr_scene_tree *background_tree;  /* Tree for background */
    struct wlr_scene_tree *view_tree;  /* Tree for window views */
    struct wlr_scene_output_layout *scene_output_layout;

    /* Output management */
    struct wlr_output_layout *output_layout;
    struct wl_list outputs;  /* infinidesk_output.link */
    struct wl_listener new_output;

    /* Input management */
    struct wlr_seat *seat;
    struct wl_list keyboards;  /* infinidesk_keyboard.link */
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;

    /* Cursor */
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *xcursor_manager;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    /* Cursor state */
    enum infinidesk_cursor_mode cursor_mode;
    struct infinidesk_view *grabbed_view;
    double grab_x, grab_y;  /* Cursor position at grab start */
    uint32_t resize_edges;  /* For resize operations */
    bool scroll_panning;    /* Currently scroll-panning (started on empty canvas) */
    struct wl_event_source *scroll_pan_timer;  /* Timer to end scroll-pan gesture */

    /* XDG shell */
    struct wlr_xdg_shell *xdg_shell;
    struct wl_list views;  /* infinidesk_view.link */
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;

    /* XWayland */
    struct wlr_xwayland *xwayland;
    struct wl_listener xwayland_ready;
    struct wl_listener xwayland_new_surface;
    /* XDG decoration (to disable client-side decorations) */
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_manager;
    struct wl_listener new_xdg_decoration;

    /* Infinite canvas */
    struct infinidesk_canvas canvas;

    /* Modifier state for input handling */
    bool super_pressed;

    /* Startup command (run after XWayland is ready) */
    char *startup_cmd;
};

/*
 * Initialise the compositor server.
 * Returns true on success, false on failure.
 */
bool server_init(struct infinidesk_server *server);

/*
 * Start the server backend and begin accepting clients.
 * Returns true on success, false on failure.
 */
bool server_start(struct infinidesk_server *server);

/*
 * Run the server event loop.
 * This function blocks until the server is terminated.
 */
void server_run(struct infinidesk_server *server);

/*
 * Clean up and destroy the server.
 */
void server_finish(struct infinidesk_server *server);

/*
 * Get the view at the given layout coordinates.
 * If surface and sx/sy are provided, they will be set to the
 * specific surface and surface-local coordinates.
 */
struct infinidesk_view *server_view_at(
    struct infinidesk_server *server,
    double lx, double ly,
    struct wlr_surface **surface,
    double *sx, double *sy);

#endif /* INFINIDESK_SERVER_H */
