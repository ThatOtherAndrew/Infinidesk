/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * canvas.h - Infinite canvas coordinate system and viewport management
 */

#ifndef INFINIDESK_CANVAS_H
#define INFINIDESK_CANVAS_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration */
struct infinidesk_server;

/* Animation duration for viewport snap (in milliseconds) */
#define CANVAS_SNAP_DURATION_MS 800

/*
 * The infinite canvas state.
 *
 * The canvas uses a coordinate system where:
 * - (0, 0) is the initial centre of the canvas
 * - Positive X is to the right
 * - Positive Y is downward
 * - Coordinates are unbounded (can be any double value)
 *
 * The viewport represents what portion of the canvas is visible on screen.
 */
struct infinidesk_canvas {
    /* Viewport position in canvas coordinates (top-left corner) */
    double viewport_x;
    double viewport_y;

    /* Zoom level (1.0 = 100%, 0.5 = zoomed out, 2.0 = zoomed in) */
    double scale;

    /* Panning state */
    bool is_panning;
    double pan_start_cursor_x;  /* Cursor position when pan started (screen) */
    double pan_start_cursor_y;
    double pan_start_viewport_x;  /* Viewport position when pan started */
    double pan_start_viewport_y;

    /* Reference to server for updating views */
    struct infinidesk_server *server;

    /* Viewport snap animation state */
    bool snap_anim_active;
    uint32_t snap_anim_start_ms;
    double snap_start_x, snap_start_y;
    double snap_target_x, snap_target_y;
};

/*
 * Initialise the canvas with default values.
 * The viewport starts centred at (0, 0) with scale 1.0.
 */
void canvas_init(struct infinidesk_canvas *canvas, struct infinidesk_server *server);

/*
 * Convert canvas coordinates to screen coordinates.
 *
 * screen = (canvas - viewport) * scale
 */
void canvas_to_screen(struct infinidesk_canvas *canvas,
                      double canvas_x, double canvas_y,
                      double *screen_x, double *screen_y);

/*
 * Convert screen coordinates to canvas coordinates.
 *
 * canvas = screen / scale + viewport
 */
void screen_to_canvas(struct infinidesk_canvas *canvas,
                      double screen_x, double screen_y,
                      double *canvas_x, double *canvas_y);

/*
 * Begin a panning operation.
 * Call this when the user starts dragging to pan.
 */
void canvas_pan_begin(struct infinidesk_canvas *canvas,
                      double cursor_x, double cursor_y);

/*
 * Update the viewport during a pan operation.
 * Call this as the cursor moves during panning.
 */
void canvas_pan_update(struct infinidesk_canvas *canvas,
                       double cursor_x, double cursor_y);

/*
 * End the panning operation.
 */
void canvas_pan_end(struct infinidesk_canvas *canvas);

/*
 * Pan the canvas by a delta amount (in screen pixels).
 * This is used for touchpad/scroll-based panning.
 */
void canvas_pan_delta(struct infinidesk_canvas *canvas,
                      double delta_x, double delta_y);

/*
 * Zoom the canvas by a factor, keeping the focus point stationary.
 *
 * factor > 1.0 zooms in, factor < 1.0 zooms out.
 * focus_x/focus_y are in screen coordinates - this point will
 * remain at the same screen position after zooming.
 */
void canvas_zoom(struct infinidesk_canvas *canvas,
                 double factor,
                 double focus_x, double focus_y);

/*
 * Set the zoom level directly.
 * focus_x/focus_y are the screen coordinates to zoom towards.
 */
void canvas_set_scale(struct infinidesk_canvas *canvas,
                      double scale,
                      double focus_x, double focus_y);

/*
 * Update all view positions in the scene graph.
 * Call this after changing the viewport.
 */
void canvas_update_view_positions(struct infinidesk_canvas *canvas);

/*
 * Get the canvas coordinates for the centre of the current viewport.
 * Useful for spawning new windows.
 */
void canvas_get_viewport_centre(struct infinidesk_canvas *canvas,
                                int output_width, int output_height,
                                double *centre_x, double *centre_y);

/*
 * Update the viewport snap animation.
 * Call this each frame from the render loop.
 */
void canvas_update_snap_animation(struct infinidesk_canvas *canvas, uint32_t time_ms);

#endif /* INFINIDESK_CANVAS_H */
