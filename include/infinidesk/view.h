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
#include <wlr/render/pass.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>

/* Forward declaration */
struct infinidesk_server;
struct infinidesk_canvas;

/* Animation duration in milliseconds */
#define VIEW_FOCUS_ANIM_DURATION_MS 200
#define VIEW_MAP_ANIM_DURATION_MS 200

/*
 * A view represents a toplevel window on the canvas.
 *
 * Views have positions in canvas coordinates, which are then
 * transformed to screen coordinates based on the current viewport.
 */
struct infinidesk_view {
    struct wl_list link; /* infinidesk_server.views */
    struct infinidesk_server *server;

    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;

    /* Unique view identifier for alt-tab matching */
    uint32_t id;

    /* Position in canvas coordinates */
    double x;
    double y;

    /* Last known geometry offset (for detecting CSD geometry changes) */
    int last_geo_x;
    int last_geo_y;

    /* Interactive move state */
    bool is_moving;
    double grab_x; /* Canvas coords where grab started */
    double grab_y;
    double grab_view_x; /* View position when grab started */
    double grab_view_y;

    /* Interactive resize state */
    bool is_resizing;
    uint32_t resize_edges; /* Which edges are being resized (enum wlr_edges) */
    double resize_grab_x;  /* Canvas coords where grab started */
    double resize_grab_y;
    double resize_start_x; /* View position when resize started */
    double resize_start_y;
    int resize_start_width; /* View size when resize started */
    int resize_start_height;

    /* Focus animation state */
    bool focused;                 /* Current focus state */
    double focus_animation;       /* 0.0 = unfocused, 1.0 = focused */
    uint32_t focus_anim_start_ms; /* Timestamp when animation started */
    bool focus_anim_active;       /* Whether animation is in progress */

    /* Map/unmap animation state */
    double map_animation;       /* 0.0 = hidden, 1.0 = fully visible */
    uint32_t map_anim_start_ms; /* Timestamp when animation started */
    bool is_animating_out;      /* Keep rendering after unmap until animation
                                 * completes
                                 */

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
 * Focus the view, giving it keyboard focus and visual focus indication.
 * Does not raise the view - use view_raise() for that.
 */
void view_focus(struct infinidesk_view *view);

/*
 * Raise the view to the top of the stack (front of rendering order).
 */
void view_raise(struct infinidesk_view *view);

/*
 * Get the geometry of the view in canvas coordinates.
 */
void view_get_geometry(struct infinidesk_view *view, double *x, double *y,
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
void view_move_begin(struct infinidesk_view *view, double cursor_x,
                     double cursor_y);

/*
 * Update view position during an interactive move.
 * cursor_x/cursor_y are in canvas coordinates.
 */
void view_move_update(struct infinidesk_view *view, double cursor_x,
                      double cursor_y);

/*
 * End the interactive move operation.
 */
void view_move_end(struct infinidesk_view *view);

/*
 * Begin an interactive resize operation.
 * edges is a bitfield of enum wlr_edges indicating which edges to resize.
 * cursor_x/cursor_y are in canvas coordinates.
 */
void view_resize_begin(struct infinidesk_view *view, uint32_t edges,
                       double cursor_x, double cursor_y);

/*
 * Update view size during an interactive resize.
 * cursor_x/cursor_y are in canvas coordinates.
 */
void view_resize_update(struct infinidesk_view *view, double cursor_x,
                        double cursor_y);

/*
 * End the interactive resize operation.
 */
void view_resize_end(struct infinidesk_view *view);

/*
 * Close the view (request the client to close).
 */
void view_close(struct infinidesk_view *view);

/*
 * Render the view to a render pass with the current canvas transform.
 * output_scale is the HiDPI scale factor of the output (e.g., 1.0, 1.5, 2.0).
 */
void view_render(struct infinidesk_view *view, struct wlr_render_pass *pass,
                 float output_scale);

/*
 * Render the view's popup surfaces (context menus, dropdowns, etc.).
 * Should be called after all views are rendered so popups appear on top.
 */
void view_render_popups(struct infinidesk_view *view,
                        struct wlr_render_pass *pass, float output_scale);

/*
 * Snaps to a view
 */
void view_snap(struct infinidesk_canvas *canvas, struct infinidesk_view *view,
               int, int);

/*
 * Update focus animation state for all views.
 * Should be called each frame.
 */
void view_update_focus_animations(struct infinidesk_server *server,
                                  uint32_t time_ms);

/*
 * Check if any view has an active animation.
 */
bool view_any_animating(struct infinidesk_server *server);

/*
 * Gather all views so they are exactly minimum_gap pixels apart (edge-to-edge),
 * preserving relative directional positioning, and center on viewport.
 */
void views_gather(struct infinidesk_server *server, double minimum_gap);

#endif /* INFINIDESK_VIEW_H */
