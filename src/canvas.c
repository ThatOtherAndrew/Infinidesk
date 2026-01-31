/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * canvas.c - Infinite canvas coordinate system and viewport management
 */

#define _POSIX_C_SOURCE 200809L

#include <math.h>

#include <wlr/util/log.h>

#include "infinidesk/canvas.h"
#include "infinidesk/server.h"
#include "infinidesk/view.h"

/* Minimum and maximum zoom levels */
#define ZOOM_MIN 0.1
#define ZOOM_MAX 4.0

void canvas_init(struct infinidesk_canvas *canvas, struct infinidesk_server *server) {
    canvas->server = server;

    /* Start with viewport at origin */
    canvas->viewport_x = 0.0;
    canvas->viewport_y = 0.0;

    /* Initial zoom level is 100% */
    canvas->scale = 1.0;

    /* Not panning initially */
    canvas->is_panning = false;
    canvas->pan_start_cursor_x = 0.0;
    canvas->pan_start_cursor_y = 0.0;
    canvas->pan_start_viewport_x = 0.0;
    canvas->pan_start_viewport_y = 0.0;

    wlr_log(WLR_DEBUG, "Canvas initialised at origin with scale 1.0");
}

void canvas_to_screen(struct infinidesk_canvas *canvas,
                      double canvas_x, double canvas_y,
                      double *screen_x, double *screen_y)
{
    /* screen = (canvas - viewport) * scale */
    *screen_x = (canvas_x - canvas->viewport_x) * canvas->scale;
    *screen_y = (canvas_y - canvas->viewport_y) * canvas->scale;
}

void screen_to_canvas(struct infinidesk_canvas *canvas,
                      double screen_x, double screen_y,
                      double *canvas_x, double *canvas_y)
{
    /* canvas = screen / scale + viewport */
    *canvas_x = screen_x / canvas->scale + canvas->viewport_x;
    *canvas_y = screen_y / canvas->scale + canvas->viewport_y;
}

void canvas_pan_begin(struct infinidesk_canvas *canvas,
                      double cursor_x, double cursor_y)
{
    canvas->is_panning = true;
    canvas->pan_start_cursor_x = cursor_x;
    canvas->pan_start_cursor_y = cursor_y;
    canvas->pan_start_viewport_x = canvas->viewport_x;
    canvas->pan_start_viewport_y = canvas->viewport_y;

    wlr_log(WLR_DEBUG, "Pan started at cursor (%.1f, %.1f), viewport (%.1f, %.1f)",
            cursor_x, cursor_y, canvas->viewport_x, canvas->viewport_y);
}

void canvas_pan_update(struct infinidesk_canvas *canvas,
                       double cursor_x, double cursor_y)
{
    if (!canvas->is_panning) {
        return;
    }

    /* Calculate cursor delta in screen space */
    double delta_x = cursor_x - canvas->pan_start_cursor_x;
    double delta_y = cursor_y - canvas->pan_start_cursor_y;

    /* Move viewport in opposite direction (dragging moves the canvas,
     * which means the viewport moves in the opposite direction) */
    canvas->viewport_x = canvas->pan_start_viewport_x - delta_x / canvas->scale;
    canvas->viewport_y = canvas->pan_start_viewport_y - delta_y / canvas->scale;

    /* Update all view positions */
    canvas_update_view_positions(canvas);
}

void canvas_pan_end(struct infinidesk_canvas *canvas) {
    if (canvas->is_panning) {
        wlr_log(WLR_DEBUG, "Pan ended at viewport (%.1f, %.1f)",
                canvas->viewport_x, canvas->viewport_y);
    }
    canvas->is_panning = false;
}

void canvas_pan_delta(struct infinidesk_canvas *canvas,
                      double delta_x, double delta_y)
{
    /* Move viewport by the delta (in canvas space) */
    canvas->viewport_x -= delta_x / canvas->scale;
    canvas->viewport_y -= delta_y / canvas->scale;

    /* Update all view positions */
    canvas_update_view_positions(canvas);
}

void canvas_zoom(struct infinidesk_canvas *canvas,
                 double factor,
                 double focus_x, double focus_y)
{
    /* Calculate new scale, clamped to limits */
    double new_scale = canvas->scale * factor;
    if (new_scale < ZOOM_MIN) {
        new_scale = ZOOM_MIN;
    } else if (new_scale > ZOOM_MAX) {
        new_scale = ZOOM_MAX;
    }

    if (new_scale == canvas->scale) {
        return;  /* No change */
    }

    /* Get the canvas position under the focus point before zoom */
    double canvas_focus_x, canvas_focus_y;
    screen_to_canvas(canvas, focus_x, focus_y, &canvas_focus_x, &canvas_focus_y);

    /* Apply the new scale */
    canvas->scale = new_scale;

    /* Adjust viewport so the focus point stays in the same screen position */
    /* After zoom: focus = (canvas_focus - new_viewport) * new_scale
     * We want focus to remain at the same screen position, so:
     * new_viewport = canvas_focus - focus / new_scale */
    canvas->viewport_x = canvas_focus_x - focus_x / canvas->scale;
    canvas->viewport_y = canvas_focus_y - focus_y / canvas->scale;

    wlr_log(WLR_DEBUG, "Zoomed to scale %.2f, viewport (%.1f, %.1f)",
            canvas->scale, canvas->viewport_x, canvas->viewport_y);

    /* Update all view positions */
    canvas_update_view_positions(canvas);
}

void canvas_set_scale(struct infinidesk_canvas *canvas,
                      double scale,
                      double focus_x, double focus_y)
{
    double factor = scale / canvas->scale;
    canvas_zoom(canvas, factor, focus_x, focus_y);
}

void canvas_update_view_positions(struct infinidesk_canvas *canvas) {
    struct infinidesk_view *view;
    wl_list_for_each(view, &canvas->server->views, link) {
        view_update_scene_position(view);
    }
}

void canvas_get_viewport_centre(struct infinidesk_canvas *canvas,
                                int output_width, int output_height,
                                double *centre_x, double *centre_y)
{
    /* The centre of the viewport in screen space is (width/2, height/2) */
    /* Convert to canvas space */
    screen_to_canvas(canvas,
                     output_width / 2.0,
                     output_height / 2.0,
                     centre_x, centre_y);
}
