/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * drawing.h - Canvas drawing layer
 */

#ifndef INFINIDESK_DRAWING_H
#define INFINIDESK_DRAWING_H

#include <stdbool.h>
#include <wayland-server-core.h>

#include "infinidesk/drawing_ui.h"

/* Forward declaration */
struct infinidesk_server;
struct wlr_render_pass;

/* A point in canvas coordinates */
struct drawing_point {
    double x;
    double y;
    struct wl_list link;  /* drawing_stroke.points */
};

/* A stroke - a continuous line made of multiple points */
struct drawing_stroke {
    struct wl_list points;  /* drawing_point.link */
    struct wl_list link;    /* drawing_layer.strokes */
    struct drawing_color color; /* Color of this stroke */
};

/* The drawing layer state */
struct drawing_layer {
    struct infinidesk_server *server;

    /* Drawing state */
    bool drawing_mode;      /* Whether drawing mode is active */
    bool is_drawing;        /* Currently drawing a stroke */

    /* Strokes list (in order of creation) */
    struct wl_list strokes; /* drawing_stroke.link */

    /* Redo stack for undone strokes */
    struct wl_list redo_stack; /* drawing_stroke.link */

    /* Current stroke being drawn */
    struct drawing_stroke *current_stroke;

    /* Last cursor position (canvas coordinates) for stroke tracking */
    double last_canvas_x;
    double last_canvas_y;

    /* Current drawing color */
    struct drawing_color current_color;

    /* UI panel */
    struct drawing_ui_panel ui_panel;
};

/*
 * Initialize the drawing layer.
 */
void drawing_init(struct drawing_layer *drawing, struct infinidesk_server *server);

/*
 * Clean up the drawing layer.
 */
void drawing_finish(struct drawing_layer *drawing);

/*
 * Toggle drawing mode on/off.
 */
void drawing_toggle_mode(struct drawing_layer *drawing);

/*
 * Clear all drawings.
 */
void drawing_clear_all(struct drawing_layer *drawing);

/*
 * Undo the last stroke.
 */
void drawing_undo_last(struct drawing_layer *drawing);

/*
 * Redo the last undone stroke.
 */
void drawing_redo_last(struct drawing_layer *drawing);

/*
 * Begin a new stroke at the given canvas coordinates.
 */
void drawing_stroke_begin(struct drawing_layer *drawing,
                          double canvas_x, double canvas_y);

/*
 * Add a point to the current stroke.
 */
void drawing_stroke_add_point(struct drawing_layer *drawing,
                              double canvas_x, double canvas_y);

/*
 * End the current stroke.
 */
void drawing_stroke_end(struct drawing_layer *drawing);

/*
 * Render all strokes to the given render pass.
 * This should be called during the output render cycle.
 */
void drawing_render(struct drawing_layer *drawing,
                    struct wlr_render_pass *pass,
                    int output_width, int output_height);

#endif /* INFINIDESK_DRAWING_H */
