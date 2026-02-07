/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * cursor.c - Cursor and pointer input handling
 */

#define _POSIX_C_SOURCE 200809L

#include <linux/input-event-codes.h>

#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <wlr/xcursor.h>

#include "infinidesk/canvas.h"
#include "infinidesk/cursor.h"
#include "infinidesk/drawing.h"
#include "infinidesk/drawing_ui.h"
#include "infinidesk/server.h"
#include "infinidesk/view.h"

/* Zoom factor for scroll wheel zoom */
#define ZOOM_SCROLL_FACTOR 1.03

/* Timeout in ms to end scroll-pan gesture after last scroll event */
#define SCROLL_PAN_TIMEOUT_MS 100

/* Timer callback to end scroll-pan gesture */
static int scroll_pan_timer_callback(void *data) {
    struct infinidesk_server *server = data;
    server->scroll_panning = false;
    return 0; /* Don't repeat */
}

void cursor_init(struct infinidesk_server *server) {
    /* Create the cursor */
    server->cursor = wlr_cursor_create();
    if (!server->cursor) {
        wlr_log(WLR_ERROR, "Failed to create cursor");
        return;
    }

    /* Attach cursor to output layout */
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

    /* Create the xcursor manager for cursor themes */
    server->xcursor_manager = wlr_xcursor_manager_create(NULL, 24);
    if (!server->xcursor_manager) {
        wlr_log(WLR_ERROR, "Failed to create xcursor manager");
        wlr_cursor_destroy(server->cursor);
        server->cursor = NULL;
        return;
    }

    /* Set up cursor event listeners */
    server->cursor_motion.notify = cursor_handle_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);

    server->cursor_motion_absolute.notify = cursor_handle_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute,
                  &server->cursor_motion_absolute);

    server->cursor_button.notify = cursor_handle_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);

    server->cursor_axis.notify = cursor_handle_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);

    server->cursor_frame.notify = cursor_handle_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

    /* Set up seat request_set_cursor listener */
    server->request_cursor.notify = cursor_handle_request_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor,
                  &server->request_cursor);

    /* Initialise cursor state */
    server->cursor_mode = INFINIDESK_CURSOR_PASSTHROUGH;
    server->grabbed_view = NULL;
    server->super_pressed = false;
    server->scroll_panning = false;
    server->scroll_pan_timer = NULL;

    wlr_log(WLR_DEBUG, "Cursor handling initialised");
}

void cursor_handle_motion(struct wl_listener *listener, void *data) {
    struct infinidesk_server *server =
        wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    /* Move the cursor */
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x,
                    event->delta_y);

    /* Process the motion */
    cursor_process_motion(server, event->time_msec);
}

void cursor_handle_motion_absolute(struct wl_listener *listener, void *data) {
    struct infinidesk_server *server =
        wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;

    /* Warp to the absolute position */
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
                             event->y);

    /* Process the motion */
    cursor_process_motion(server, event->time_msec);
}

void cursor_handle_button(struct wl_listener *listener, void *data) {
    struct infinidesk_server *server =
        wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;

    /* Notify the seat of the button event */
    wlr_seat_pointer_notify_button(server->seat, event->time_msec,
                                   event->button, event->state);

    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        /* Button pressed */
        double sx, sy;
        struct wlr_surface *surface = NULL;
        struct infinidesk_view *view = server_view_at(
            server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

        /* Check if drawing mode is active */
        if (server->drawing.drawing_mode) {
            /* Check if cursor is over UI panel first */
            enum drawing_ui_button button =
                drawing_ui_get_button_at(&server->drawing.ui_panel,
                                         server->cursor->x, server->cursor->y);

            if (button != UI_BUTTON_NONE) {
                if (event->button == BTN_LEFT) {
                    /* Click on UI button */
                    drawing_ui_handle_click(&server->drawing.ui_panel,
                                            &server->drawing, button);
                    wlr_log(WLR_DEBUG, "UI button clicked: %d", button);
                    return;
                }
            }

            if (event->button == BTN_LEFT) {
                /* Left click in drawing mode (not on UI): Begin drawing stroke
                 */
                wlr_log(WLR_DEBUG, "Beginning drawing stroke");
                server->cursor_mode = INFINIDESK_CURSOR_DRAW;

                /* Convert cursor position to canvas coordinates */
                double canvas_x, canvas_y;
                screen_to_canvas(&server->canvas, server->cursor->x,
                                 server->cursor->y, &canvas_x, &canvas_y);
                drawing_stroke_begin(&server->drawing, canvas_x, canvas_y);
                return;
            }
        }

        /* Check for Super key modifier actions */
        if (server->super_pressed) {
            if (event->button == BTN_LEFT && view) {
                /* Super + Left click: Begin window move */
                wlr_log(WLR_DEBUG, "Beginning view move");
                server->cursor_mode = INFINIDESK_CURSOR_MOVE;
                server->grabbed_view = view;
                server->grab_x = server->cursor->x;
                server->grab_y = server->cursor->y;

                /* Convert cursor position to canvas coordinates */
                double canvas_x, canvas_y;
                screen_to_canvas(&server->canvas, server->cursor->x,
                                 server->cursor->y, &canvas_x, &canvas_y);
                view_move_begin(view, canvas_x, canvas_y);

                /* Focus and raise the view being moved */
                view_focus(view);
                view_raise(view);
                return;
            } else if (event->button == BTN_RIGHT) {
                /* Super + Right click: Begin canvas pan */
                wlr_log(WLR_DEBUG, "Beginning canvas pan");
                server->cursor_mode = INFINIDESK_CURSOR_PAN;
                canvas_pan_begin(&server->canvas, server->cursor->x,
                                 server->cursor->y);
                return;
            }
        }

        /* Regular click - focus and raise the view if we clicked on one */
        if (view) {
            view_focus(view);
            view_raise(view);
        }

    } else {
        /* Button released */
        if (server->cursor_mode == INFINIDESK_CURSOR_MOVE) {
            /* End window move */
            if (server->grabbed_view) {
                view_move_end(server->grabbed_view);
            }
            cursor_reset_mode(server);

        } else if (server->cursor_mode == INFINIDESK_CURSOR_PAN) {
            /* End canvas pan */
            canvas_pan_end(&server->canvas);
            cursor_reset_mode(server);

        } else if (server->cursor_mode == INFINIDESK_CURSOR_DRAW) {
            /* End drawing stroke */
            drawing_stroke_end(&server->drawing);
            cursor_reset_mode(server);
        }
    }
}

void cursor_handle_axis(struct wl_listener *listener, void *data) {
    struct infinidesk_server *server =
        wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;

    /* Alt + Scroll: Zoom canvas */
    if (server->super_pressed) {
        if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            double factor = (event->delta < 0) ? ZOOM_SCROLL_FACTOR
                                               : (1.0 / ZOOM_SCROLL_FACTOR);
            canvas_zoom(&server->canvas, factor, server->cursor->x,
                        server->cursor->y);
        }
        /* Ignore horizontal scroll when Alt is held */
        return;
    }

    /*
     * If we're already scroll-panning, continue panning regardless of
     * what's under the cursor. This prevents windows from stealing the
     * gesture mid-pan.
     */
    if (server->scroll_panning) {
        if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            canvas_pan_delta(&server->canvas, 0, event->delta);
        } else {
            canvas_pan_delta(&server->canvas, event->delta, 0);
        }
        /* Reset the timer on each scroll event */
        if (server->scroll_pan_timer) {
            wl_event_source_timer_update(server->scroll_pan_timer,
                                         SCROLL_PAN_TIMEOUT_MS);
        }
        return;
    }

    /*
     * Check if cursor is over a view - if so, pass scroll to the client.
     * Otherwise, use scroll to pan the canvas.
     */
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct infinidesk_view *view = server_view_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (view && surface) {
        /* Scroll over a window - pass to client */
        wlr_seat_pointer_notify_axis(
            server->seat, event->time_msec, event->orientation, event->delta,
            event->delta_discrete, event->source, event->relative_direction);
    } else {
        /* Scroll over empty canvas - start pan gesture */
        server->scroll_panning = true;

        /* Create or reset the timer to end the gesture */
        if (server->scroll_pan_timer) {
            wl_event_source_timer_update(server->scroll_pan_timer,
                                         SCROLL_PAN_TIMEOUT_MS);
        } else {
            server->scroll_pan_timer = wl_event_loop_add_timer(
                server->event_loop, scroll_pan_timer_callback, server);
            wl_event_source_timer_update(server->scroll_pan_timer,
                                         SCROLL_PAN_TIMEOUT_MS);
        }

        if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
            canvas_pan_delta(&server->canvas, 0, event->delta);
        } else {
            canvas_pan_delta(&server->canvas, event->delta, 0);
        }
    }
}

void cursor_handle_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_server *server =
        wl_container_of(listener, server, cursor_frame);

    /* Notify the seat of the frame event */
    wlr_seat_pointer_notify_frame(server->seat);
}

void cursor_handle_request_cursor(struct wl_listener *listener, void *data) {
    struct infinidesk_server *server =
        wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    /* Only honour cursor image requests from the focused client */
    struct wlr_seat_client *focused_client =
        server->seat->pointer_state.focused_client;

    if (focused_client == event->seat_client) {
        /* Set the cursor surface from the client */
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x,
                               event->hotspot_y);
    }
}

void cursor_process_motion(struct infinidesk_server *server, uint32_t time) {
    switch (server->cursor_mode) {
    case INFINIDESK_CURSOR_MOVE: {
        /* Update the view position during move */
        if (server->grabbed_view) {
            double canvas_x, canvas_y;
            screen_to_canvas(&server->canvas, server->cursor->x,
                             server->cursor->y, &canvas_x, &canvas_y);
            view_move_update(server->grabbed_view, canvas_x, canvas_y);
        }
        return;
    }

    case INFINIDESK_CURSOR_PAN: {
        /* Update the viewport during pan */
        canvas_pan_update(&server->canvas, server->cursor->x,
                          server->cursor->y);
        return;
    }

    case INFINIDESK_CURSOR_DRAW: {
        /* Add points to the current stroke */
        double canvas_x, canvas_y;
        screen_to_canvas(&server->canvas, server->cursor->x, server->cursor->y,
                         &canvas_x, &canvas_y);
        drawing_stroke_add_point(&server->drawing, canvas_x, canvas_y);
        return;
    }

    case INFINIDESK_CURSOR_RESIZE:
        /* Resize not yet implemented */
        return;

    case INFINIDESK_CURSOR_PASSTHROUGH:
    default:
        break;
    }

    /* Passthrough mode: update focus and cursor image */

    /* Update UI hover state if drawing mode is active */
    if (server->drawing.drawing_mode) {
        drawing_ui_update_hover(&server->drawing.ui_panel, server->cursor->x,
                                server->cursor->y);
    }

    /*
     * Check if cursor is near a window edge for resizing.
     * This takes priority over normal view hover.
     */
    struct infinidesk_view *edge_view = NULL;
    uint32_t edges = server_view_edge_at(server, server->cursor->x,
                                         server->cursor->y, &edge_view);

    if (edges != WLR_EDGE_NONE && edge_view) {
        /* Cursor is on a resize edge - show resize cursor */
        const char *cursor_name = wlr_xcursor_get_resize_name(edges);
        wlr_cursor_set_xcursor(server->cursor, server->xcursor_manager,
                               cursor_name);

        /* Clear pointer focus since we're on an edge, not a surface */
        wlr_seat_pointer_clear_focus(server->seat);
        return;
    }

    /*
     * Passthrough mode: update pointer focus, cursor image, and keyboard focus.
     * Implements focus-follows-mouse behaviour.
     */
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct infinidesk_view *view = server_view_at(
        server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (!view) {
        /* No view under cursor - set default cursor */
        wlr_cursor_set_xcursor(server->cursor, server->xcursor_manager,
                               "default");
    }

    if (surface) {
        /* Notify the seat of the pointer entering/moving on the surface */
        wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);

        /*
         * Focus-follows-mouse: focus the view under cursor.
         * Skip focus changes during scroll panning to avoid stealing focus
         * while the user is navigating the canvas.
         */
        if (view && !server->scroll_panning) {
            view_focus(view);
        }
    } else {
        /* Clear pointer focus if not on a surface */
        wlr_seat_pointer_clear_focus(server->seat);
    }
}

void cursor_reset_mode(struct infinidesk_server *server) {
    server->cursor_mode = INFINIDESK_CURSOR_PASSTHROUGH;
    server->grabbed_view = NULL;

    wlr_log(WLR_DEBUG, "Cursor mode reset to passthrough");
}
