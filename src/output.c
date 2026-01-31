/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * output.c - Output (monitor) management
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "infinidesk/output.h"
#include "infinidesk/server.h"

void output_init(struct infinidesk_server *server) {
    server->new_output.notify = handle_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);
}

void handle_new_output(struct wl_listener *listener, void *data) {
    struct infinidesk_server *server =
        wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_log(WLR_INFO, "New output: %s (%s %s)",
            wlr_output->name,
            wlr_output->make ?: "Unknown",
            wlr_output->model ?: "Unknown");

    /* Allocate output wrapper */
    struct infinidesk_output *output = calloc(1, sizeof(*output));
    if (!output) {
        wlr_log(WLR_ERROR, "Failed to allocate output");
        return;
    }

    output->server = server;
    output->wlr_output = wlr_output;

    /* Set up event listeners */
    output->frame.notify = output_handle_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_handle_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_handle_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    /* Initialise the output with allocator */
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    /* Configure the output mode */
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    /* Use the preferred mode if available */
    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    if (mode) {
        wlr_log(WLR_INFO, "Setting output mode: %dx%d@%dmHz",
                mode->width, mode->height, mode->refresh);
        wlr_output_state_set_mode(&state, mode);
    }

    /* Commit the output state */
    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    /* Add output to layout */
    struct wlr_output_layout_output *l_output =
        wlr_output_layout_add_auto(server->output_layout, wlr_output);

    /* Create scene output */
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(
        server->scene_output_layout, l_output, output->scene_output);

    /* Add to server's output list */
    wl_list_insert(&server->outputs, &output->link);

    wlr_log(WLR_DEBUG, "Output %s configured", wlr_output->name);
}

void output_handle_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_output *output =
        wl_container_of(listener, output, frame);

    /* Render the scene to this output */
    struct wlr_scene_output *scene_output = output->scene_output;
    wlr_scene_output_commit(scene_output, NULL);

    /* Send frame done to all visible surfaces */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

void output_handle_request_state(struct wl_listener *listener, void *data) {
    struct infinidesk_output *output =
        wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;

    wlr_log(WLR_DEBUG, "Output %s requested state change",
            output->wlr_output->name);

    /* Apply the requested state */
    wlr_output_commit_state(output->wlr_output, event->state);
}

void output_handle_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_output *output =
        wl_container_of(listener, output, destroy);

    wlr_log(WLR_INFO, "Output %s destroyed", output->wlr_output->name);

    wl_list_remove(&output->link);
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);

    free(output);
}

struct infinidesk_output *output_get_primary(struct infinidesk_server *server) {
    if (wl_list_empty(&server->outputs)) {
        return NULL;
    }

    struct infinidesk_output *output;
    return wl_container_of(server->outputs.next, output, link);
}

void output_get_effective_resolution(struct infinidesk_output *output,
                                     int *width, int *height)
{
    wlr_output_effective_resolution(output->wlr_output, width, height);
}
