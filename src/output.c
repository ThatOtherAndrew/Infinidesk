/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * output.c - Output (monitor) management with custom rendering
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <time.h>

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/render/pass.h>
#include <wlr/backend/wayland.h>
#include <wlr/util/log.h>

#include "infinidesk/output.h"
#include "infinidesk/server.h"
#include "infinidesk/view.h"
#include "infinidesk/canvas.h"

/* Background colour */
static const float bg_colour[4] = { 0.18f, 0.18f, 0.18f, 1.0f };

/* Forward declarations */
static void output_render_custom(struct infinidesk_output *output);
static void send_frame_done_iterator(struct wlr_surface *surface,
                                     int sx, int sy, void *data);

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

    /* Create scene output (still needed for some operations) */
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(
        server->scene_output_layout, l_output, output->scene_output);

    /* Add to server's output list */
    wl_list_insert(&server->outputs, &output->link);

    /* Set window title and app_id when running nested in a Wayland compositor */
    if (wlr_output_is_wl(wlr_output)) {
        wlr_wl_output_set_title(wlr_output, "Infinidesk");
        wlr_wl_output_set_app_id(wlr_output, "infinidesk");
        wlr_log(WLR_DEBUG, "Set nested Wayland window title/app_id");
    }

    wlr_log(WLR_DEBUG, "Output %s configured", wlr_output->name);
}

void output_handle_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_output *output =
        wl_container_of(listener, output, frame);

    /* Use custom rendering pipeline */
    output_render_custom(output);
}

/*
 * Custom rendering with canvas transforms.
 * This bypasses the scene graph to allow arbitrary scaling of surfaces.
 */
static void output_render_custom(struct infinidesk_output *output) {
    struct infinidesk_server *server = output->server;
    struct wlr_output *wlr_output = output->wlr_output;

    /* Initialise output state */
    struct wlr_output_state state;
    wlr_output_state_init(&state);

    /* Begin a render pass */
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(
        wlr_output, &state, NULL, NULL);
    if (!pass) {
        wlr_log(WLR_ERROR, "Failed to begin render pass");
        wlr_output_state_finish(&state);
        return;
    }

    /* Get output dimensions */
    int width, height;
    wlr_output_effective_resolution(wlr_output, &width, &height);

    /* Clear with background colour */
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
        .box = { .width = width, .height = height },
        .color = {
            .r = bg_colour[0],
            .g = bg_colour[1],
            .b = bg_colour[2],
            .a = bg_colour[3],
        },
    });

    /* Render views back-to-front (reverse iteration since list is front-to-back) */
    struct infinidesk_view *view;
    wl_list_for_each_reverse(view, &server->views, link) {
        if (!view->xdg_toplevel->base->surface->mapped) {
            continue;
        }
        view_render(view, pass);
    }

    /* Submit the render pass */
    wlr_render_pass_submit(pass);

    /* Commit the output - check for failure */
    if (!wlr_output_commit_state(wlr_output, &state)) {
        wlr_log(WLR_ERROR, "Failed to commit output state");
    }
    wlr_output_state_finish(&state);

    /* Send frame done to all mapped surfaces (including subsurfaces) */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    wl_list_for_each(view, &server->views, link) {
        if (view->xdg_toplevel->base->surface->mapped) {
            wlr_xdg_surface_for_each_surface(view->xdg_toplevel->base,
                send_frame_done_iterator, &now);
        }
    }
}

/* Iterator to send frame_done to each surface */
static void send_frame_done_iterator(struct wlr_surface *surface,
                                     int sx, int sy, void *data)
{
    (void)sx;
    (void)sy;
    struct timespec *now = data;
    wlr_surface_send_frame_done(surface, now);
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
