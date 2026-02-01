/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * output.h - Output (monitor) management
 */

#ifndef INFINIDESK_OUTPUT_H
#define INFINIDESK_OUTPUT_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>

/* Forward declaration */
struct infinidesk_server;

/*
 * Output (monitor) wrapper.
 */
struct infinidesk_output {
    struct wl_list link;  /* infinidesk_server.outputs */
    struct infinidesk_server *server;

    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;

    /* Frame timing for animations */
    struct timespec last_frame_time;
    bool has_last_frame;

    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

/*
 * Initialise output handling for the server.
 */
void output_init(struct infinidesk_server *server);

/*
 * Handle a new output being added.
 */
void handle_new_output(struct wl_listener *listener, void *data);

/*
 * Handle output frame events (render).
 */
void output_handle_frame(struct wl_listener *listener, void *data);

/*
 * Handle output state change requests.
 */
void output_handle_request_state(struct wl_listener *listener, void *data);

/*
 * Handle output destruction.
 */
void output_handle_destroy(struct wl_listener *listener, void *data);

/*
 * Get the primary output (first in list).
 * Returns NULL if no outputs are available.
 */
struct infinidesk_output *output_get_primary(struct infinidesk_server *server);

/*
 * Get the effective resolution of an output.
 */
void output_get_effective_resolution(struct infinidesk_output *output,
                                     int *width, int *height);

#endif /* INFINIDESK_OUTPUT_H */
