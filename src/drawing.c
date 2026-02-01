/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * drawing.c - Canvas drawing layer implementation
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <math.h>

#include <wlr/render/pass.h>
#include <wlr/util/log.h>

#include "infinidesk/drawing.h"
#include "infinidesk/server.h"
#include "infinidesk/canvas.h"

/* Drawing configuration */
#define DRAWING_LINE_WIDTH 4.0f
#define DRAWING_COLOR_A 1.0f
#define MIN_POINT_DISTANCE 2.0  /* Minimum distance between points in canvas coords */

/* Forward declarations */
static struct drawing_stroke *drawing_stroke_create(void);
static void drawing_stroke_destroy(struct drawing_stroke *stroke);
static struct drawing_point *drawing_point_create(double x, double y);
static void drawing_point_destroy(struct drawing_point *point);

void drawing_init(struct drawing_layer *drawing, struct infinidesk_server *server) {
    drawing->server = server;
    drawing->drawing_mode = false;
    drawing->is_drawing = false;
    drawing->current_stroke = NULL;
    drawing->last_canvas_x = 0;
    drawing->last_canvas_y = 0;

    wl_list_init(&drawing->strokes);
    wl_list_init(&drawing->redo_stack);

    /* Initialize with default red color */
    drawing->current_color = COLOR_RED;

    /* UI panel initialized on first frame */
    drawing->ui_panel.hovered_button = UI_BUTTON_NONE;
    drawing->ui_panel.pressed_button = UI_BUTTON_NONE;

    wlr_log(WLR_DEBUG, "Drawing layer initialized");
}

void drawing_finish(struct drawing_layer *drawing) {
    /* Clean up all strokes */
    drawing_clear_all(drawing);

    wlr_log(WLR_DEBUG, "Drawing layer finished");
}

void drawing_toggle_mode(struct drawing_layer *drawing) {
    drawing->drawing_mode = !drawing->drawing_mode;

    /* If disabling drawing mode while drawing, end the current stroke */
    if (!drawing->drawing_mode && drawing->is_drawing) {
        drawing_stroke_end(drawing);
    }

    wlr_log(WLR_INFO, "Drawing mode %s",
            drawing->drawing_mode ? "enabled" : "disabled");
}

void drawing_clear_all(struct drawing_layer *drawing) {
    struct drawing_stroke *stroke, *tmp_stroke;

    wl_list_for_each_safe(stroke, tmp_stroke, &drawing->strokes, link) {
        drawing_stroke_destroy(stroke);
    }

    /* Clear redo stack as well */
    wl_list_for_each_safe(stroke, tmp_stroke, &drawing->redo_stack, link) {
        drawing_stroke_destroy(stroke);
    }

    drawing->current_stroke = NULL;
    drawing->is_drawing = false;

    wlr_log(WLR_INFO, "All drawings cleared");
}

void drawing_undo_last(struct drawing_layer *drawing) {
    /* If currently drawing, end and remove that stroke */
    if (drawing->is_drawing && drawing->current_stroke) {
        drawing_stroke_destroy(drawing->current_stroke);
        drawing->current_stroke = NULL;
        drawing->is_drawing = false;
        wlr_log(WLR_INFO, "Undid current stroke");
        return;
    }

    /* Otherwise remove the last completed stroke */
    if (wl_list_empty(&drawing->strokes)) {
        wlr_log(WLR_DEBUG, "No strokes to undo");
        return;
    }

    /* Get the last stroke in the list */
    struct drawing_stroke *stroke =
        wl_container_of(drawing->strokes.prev, stroke, link);

    /* Move stroke to redo stack instead of destroying */
    wl_list_remove(&stroke->link);
    wl_list_insert(drawing->redo_stack.prev, &stroke->link);

    wlr_log(WLR_INFO, "Undid last stroke");
}

void drawing_redo_last(struct drawing_layer *drawing) {
    if (wl_list_empty(&drawing->redo_stack)) {
        wlr_log(WLR_DEBUG, "No strokes to redo");
        return;
    }

    struct drawing_stroke *stroke =
        wl_container_of(drawing->redo_stack.prev, stroke, link);

    wl_list_remove(&stroke->link);
    wl_list_insert(drawing->strokes.prev, &stroke->link);

    wlr_log(WLR_INFO, "Redid stroke");
}

void drawing_stroke_begin(struct drawing_layer *drawing,
                          double canvas_x, double canvas_y) {
    if (!drawing->drawing_mode) {
        return;
    }

    /* Create a new stroke */
    drawing->current_stroke = drawing_stroke_create();
    if (!drawing->current_stroke) {
        wlr_log(WLR_ERROR, "Failed to create stroke");
        return;
    }

    /* Save the current color with the stroke */
    drawing->current_stroke->color = drawing->current_color;

    /* Add the first point */
    struct drawing_point *point = drawing_point_create(canvas_x, canvas_y);
    if (!point) {
        wlr_log(WLR_ERROR, "Failed to create point");
        free(drawing->current_stroke);
        drawing->current_stroke = NULL;
        return;
    }

    wl_list_insert(&drawing->current_stroke->points, &point->link);

    drawing->is_drawing = true;
    drawing->last_canvas_x = canvas_x;
    drawing->last_canvas_y = canvas_y;

    wlr_log(WLR_DEBUG, "Started new stroke at (%.2f, %.2f)", canvas_x, canvas_y);
}

void drawing_stroke_add_point(struct drawing_layer *drawing,
                              double canvas_x, double canvas_y) {
    if (!drawing->is_drawing || !drawing->current_stroke) {
        return;
    }

    /* Only add point if it's far enough from the last point */
    double dx = canvas_x - drawing->last_canvas_x;
    double dy = canvas_y - drawing->last_canvas_y;
    double distance = sqrt(dx * dx + dy * dy);

    if (distance < MIN_POINT_DISTANCE) {
        return;
    }

    struct drawing_point *point = drawing_point_create(canvas_x, canvas_y);
    if (!point) {
        wlr_log(WLR_ERROR, "Failed to create point");
        return;
    }

    wl_list_insert(drawing->current_stroke->points.prev, &point->link);

    drawing->last_canvas_x = canvas_x;
    drawing->last_canvas_y = canvas_y;
}

void drawing_stroke_end(struct drawing_layer *drawing) {
    if (!drawing->is_drawing || !drawing->current_stroke) {
        return;
    }

    /* Only keep strokes with at least 2 points */
    int point_count = 0;
    struct drawing_point *point;
    wl_list_for_each(point, &drawing->current_stroke->points, link) {
        point_count++;
        if (point_count >= 2) {
            break;
        }
    }

    if (point_count < 2) {
        wlr_log(WLR_DEBUG, "Stroke too short, discarding");
        drawing_stroke_destroy(drawing->current_stroke);
    } else {
        /* Add the completed stroke to the list */
        wl_list_insert(drawing->strokes.prev, &drawing->current_stroke->link);
        wlr_log(WLR_DEBUG, "Finished stroke with %d points", point_count);

        /* Clear redo stack when new stroke is drawn */
        struct drawing_stroke *redo_stroke, *tmp;
        wl_list_for_each_safe(redo_stroke, tmp, &drawing->redo_stack, link) {
            drawing_stroke_destroy(redo_stroke);
        }
    }

    drawing->current_stroke = NULL;
    drawing->is_drawing = false;
}

void drawing_render(struct drawing_layer *drawing,
                    struct wlr_render_pass *pass,
                    int output_width, int output_height) {
    (void)output_width;
    (void)output_height;

    struct infinidesk_canvas *canvas = &drawing->server->canvas;

    /* Render all completed strokes */
    struct drawing_stroke *stroke;
    wl_list_for_each(stroke, &drawing->strokes, link) {
        struct drawing_point *prev_point = NULL;
        struct drawing_point *point;

        wl_list_for_each(point, &stroke->points, link) {
            if (prev_point) {
                /* Convert canvas coordinates to screen coordinates */
                double screen_x1, screen_y1, screen_x2, screen_y2;
                canvas_to_screen(canvas,
                    prev_point->x, prev_point->y,
                    &screen_x1, &screen_y1);
                canvas_to_screen(canvas,
                    point->x, point->y,
                    &screen_x2, &screen_y2);

                /* Draw line segment */
                /* Note: wlroots doesn't have a direct line primitive,
                 * so we approximate with small rectangles */
                double dx = screen_x2 - screen_x1;
                double dy = screen_y2 - screen_y1;
                double length = sqrt(dx * dx + dy * dy);

                if (length > 0.1) {
                    double scaled_width = DRAWING_LINE_WIDTH * canvas->scale;

                    /* Draw multiple small rects along the line for smoothness */
                    int segments = (int)(length / 2.0) + 1;
                    for (int i = 0; i <= segments; i++) {
                        double t = segments > 0 ? (double)i / segments : 0;
                        double x = screen_x1 + dx * t;
                        double y = screen_y1 + dy * t;

                        /* Draw a small rectangle at this point */
                        wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                            .box = {
                                .x = (int)(x - scaled_width / 2),
                                .y = (int)(y - scaled_width / 2),
                                .width = (int)scaled_width + 1,
                                .height = (int)scaled_width + 1,
                            },
                            .color = {
                                .r = stroke->color.r,
                                .g = stroke->color.g,
                                .b = stroke->color.b,
                                .a = DRAWING_COLOR_A,
                            },
                        });
                    }
                }
            }
            prev_point = point;
        }
    }

    /* Render the current stroke being drawn */
    if (drawing->is_drawing && drawing->current_stroke) {
        struct drawing_point *prev_point = NULL;
        struct drawing_point *point;

        wl_list_for_each(point, &drawing->current_stroke->points, link) {
            if (prev_point) {
                double screen_x1, screen_y1, screen_x2, screen_y2;
                canvas_to_screen(canvas,
                    prev_point->x, prev_point->y,
                    &screen_x1, &screen_y1);
                canvas_to_screen(canvas,
                    point->x, point->y,
                    &screen_x2, &screen_y2);

                double dx = screen_x2 - screen_x1;
                double dy = screen_y2 - screen_y1;
                double length = sqrt(dx * dx + dy * dy);

                if (length > 0.1) {
                    double scaled_width = DRAWING_LINE_WIDTH * canvas->scale;

                    int segments = (int)(length / 2.0) + 1;
                    for (int i = 0; i <= segments; i++) {
                        double t = segments > 0 ? (double)i / segments : 0;
                        double x = screen_x1 + dx * t;
                        double y = screen_y1 + dy * t;

                        wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                            .box = {
                                .x = (int)(x - scaled_width / 2),
                                .y = (int)(y - scaled_width / 2),
                                .width = (int)scaled_width + 1,
                                .height = (int)scaled_width + 1,
                            },
                            .color = {
                                .r = drawing->current_color.r,
                                .g = drawing->current_color.g,
                                .b = drawing->current_color.b,
                                .a = DRAWING_COLOR_A,
                            },
                        });
                    }
                }
            }
            prev_point = point;
        }
    }
}

/* Internal functions */

static struct drawing_stroke *drawing_stroke_create(void) {
    struct drawing_stroke *stroke = calloc(1, sizeof(*stroke));
    if (!stroke) {
        return NULL;
    }

    wl_list_init(&stroke->points);
    wl_list_init(&stroke->link);

    return stroke;
}

static void drawing_stroke_destroy(struct drawing_stroke *stroke) {
    if (!stroke) {
        return;
    }

    /* Free all points in the stroke */
    struct drawing_point *point, *tmp;
    wl_list_for_each_safe(point, tmp, &stroke->points, link) {
        drawing_point_destroy(point);
    }

    wl_list_remove(&stroke->link);
    free(stroke);
}

static struct drawing_point *drawing_point_create(double x, double y) {
    struct drawing_point *point = calloc(1, sizeof(*point));
    if (!point) {
        return NULL;
    }

    point->x = x;
    point->y = y;
    wl_list_init(&point->link);

    return point;
}

static void drawing_point_destroy(struct drawing_point *point) {
    if (!point) {
        return;
    }

    wl_list_remove(&point->link);
    free(point);
}
