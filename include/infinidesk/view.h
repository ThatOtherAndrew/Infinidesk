/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * view.h - Window (view) abstraction
 */

#ifndef INFINIDESK_VIEW_H
#define INFINIDESK_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/render/pass.h>

/* Forward declaration */
struct infinidesk_server;

/* Animation duration in milliseconds */
#define VIEW_FOCUS_ANIM_DURATION_MS 200

/*
 * A view represents a toplevel window on the canvas.
 *
 * Views have positions in canvas coordinates, which are then
 * transformed to screen coordinates based on the current viewport.
 */
struct infinidesk_view {
    struct wl_list link;  /* infinidesk_server.views */
    struct infinidesk_server *server;

    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;

    /* Position in canvas coordinates */
    double x;
    double y;

    /* Interactive move state */
    bool is_moving;
    double grab_x;  /* Canvas coords where grab started */
    double grab_y;
    double grab_view_x;  /* View position when grab started */
    double grab_view_y;

    /* Focus animation state */
    bool focused;                   /* Current focus state */
    double focus_animation;         /* 0.0 = unfocused, 1.0 = focused */
    uint32_t focus_anim_start_ms;   /* Timestamp when animation started */
    bool focus_anim_active;         /* Whether animation is in progress */

    /* Surface event listeners */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;

    /* Toplevel event listeners */
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximise;
    struct wl_listener request_fullscreen;
    struct wl_listener set_title;
    struct wl_listener set_app_id;
};

/*
 * Create a new view for an XDG toplevel.
 * The view is not yet mapped (visible) after creation.
 */
struct infinidesk_view *view_create(struct infinidesk_server *server,
                                    struct wlr_xdg_toplevel *xdg_toplevel);

/*
 * Destroy a view and clean up resources.
 */
void view_destroy(struct infinidesk_view *view);

/*
 * Focus the view, bringing it to the front and giving it keyboard focus.
 */
void view_focus(struct infinidesk_view *view);

/*
 * Get the geometry of the view in canvas coordinates.
 */
void view_get_geometry(struct infinidesk_view *view,
                       double *x, double *y,
                       int *width, int *height);

/*
 * Set the view's position in canvas coordinates.
 */
void view_set_position(struct infinidesk_view *view, double x, double y);

/*
 * Update the view's scene graph position based on canvas coordinates
 * and the current viewport.
 */
void view_update_scene_position(struct infinidesk_view *view);

/*
 * Begin an interactive move operation.
 * cursor_x/cursor_y are in canvas coordinates.
 */
void view_move_begin(struct infinidesk_view *view,
                     double cursor_x, double cursor_y);

/*
 * Update view position during an interactive move.
 * cursor_x/cursor_y are in canvas coordinates.
 */
void view_move_update(struct infinidesk_view *view,
                      double cursor_x, double cursor_y);

/*
 * End the interactive move operation.
 */
void view_move_end(struct infinidesk_view *view);

/*
 * Close the view (request the client to close).
 */
void view_close(struct infinidesk_view *view);

/*
 * Render the view to a render pass with the current canvas transform.
 */
void view_render(struct infinidesk_view *view, struct wlr_render_pass *pass);

/*
 * Update focus animation state for all views.
 * Should be called each frame.
 */
void view_update_focus_animations(struct infinidesk_server *server, uint32_t time_ms);

/*
 * Check if any view has an active animation.
 */
bool view_any_animating(struct infinidesk_server *server);

#endif /* INFINIDESK_VIEW_H */
