/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * server.c - Core server initialisation and management
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>

#include "infinidesk/server.h"
#include "infinidesk/canvas.h"
#include "infinidesk/output.h"
#include "infinidesk/input.h"
#include "infinidesk/cursor.h"
#include "infinidesk/xdg_shell.h"
#include "infinidesk/xwayland.h"
#include "infinidesk/view.h"
#include "infinidesk/background.h"

bool server_init(struct infinidesk_server *server) {
    wlr_log(WLR_DEBUG, "Initialising Wayland display");

    /* Create the Wayland display */
    server->wl_display = wl_display_create();
    if (!server->wl_display) {
        wlr_log(WLR_ERROR, "Failed to create Wayland display");
        return false;
    }
    server->event_loop = wl_display_get_event_loop(server->wl_display);

    /* Create the backend */
    wlr_log(WLR_DEBUG, "Creating backend");
    server->backend = wlr_backend_autocreate(server->event_loop, NULL);
    if (!server->backend) {
        wlr_log(WLR_ERROR, "Failed to create wlroots backend");
        goto error_display;
    }

    /* Create the renderer */
    wlr_log(WLR_DEBUG, "Creating renderer");
    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        wlr_log(WLR_ERROR, "Failed to create wlroots renderer");
        goto error_backend;
    }
    wlr_renderer_init_wl_display(server->renderer, server->wl_display);

    /* Create the allocator */
    wlr_log(WLR_DEBUG, "Creating allocator");
    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (!server->allocator) {
        wlr_log(WLR_ERROR, "Failed to create wlroots allocator");
        goto error_renderer;
    }

    /* Create the compositor and subcompositor */
    wlr_log(WLR_DEBUG, "Creating compositor interfaces");
    server->compositor = wlr_compositor_create(server->wl_display, 6, server->renderer);
    if (!server->compositor) {
        wlr_log(WLR_ERROR, "Failed to create wlr_compositor");
        goto error_allocator;
    }

    server->subcompositor = wlr_subcompositor_create(server->wl_display);
    if (!server->subcompositor) {
        wlr_log(WLR_ERROR, "Failed to create wlr_subcompositor");
        goto error_allocator;
    }

    /* Create data device manager (for clipboard, etc.) */
    server->data_device_manager = wlr_data_device_manager_create(server->wl_display);
    if (!server->data_device_manager) {
        wlr_log(WLR_ERROR, "Failed to create data device manager");
        goto error_allocator;
    }

    /* Create the output layout */
    wlr_log(WLR_DEBUG, "Creating output layout");
    server->output_layout = wlr_output_layout_create(server->wl_display);
    if (!server->output_layout) {
        wlr_log(WLR_ERROR, "Failed to create output layout");
        goto error_allocator;
    }

    /* Create the scene graph */
    wlr_log(WLR_DEBUG, "Creating scene graph");
    server->scene = wlr_scene_create();
    if (!server->scene) {
        wlr_log(WLR_ERROR, "Failed to create scene");
        goto error_output_layout;
    }

    /* Create a tree for the background (rendered first, behind everything) */
    server->background_tree = wlr_scene_tree_create(&server->scene->tree);
    if (!server->background_tree) {
        wlr_log(WLR_ERROR, "Failed to create background tree");
        goto error_scene;
    }

    /* Create a tree for views within the scene (rendered on top of background) */
    server->view_tree = wlr_scene_tree_create(&server->scene->tree);
    if (!server->view_tree) {
        wlr_log(WLR_ERROR, "Failed to create view tree");
        goto error_scene;
    }

    /* Attach the scene to the output layout */
    server->scene_output_layout = wlr_scene_attach_output_layout(
        server->scene, server->output_layout);
    if (!server->scene_output_layout) {
        wlr_log(WLR_ERROR, "Failed to attach scene to output layout");
        goto error_scene;
    }

    /* Initialise lists */
    wl_list_init(&server->outputs);
    wl_list_init(&server->views);
    wl_list_init(&server->keyboards);

    /* Initialise the canvas */
    canvas_init(&server->canvas, server);

    /* Initialise output handling */
    output_init(server);

    /* Initialise input handling */
    input_init(server);

    /* Initialise cursor */
    cursor_init(server);

    /* Initialise XDG shell */
    xdg_shell_init(server);

    /* Initialise XWayland */
    if (!xwayland_init(server)) {
        wlr_log(WLR_ERROR, "Failed to initialize XWayland");
        /* Non-fatal - compositor can run without XWayland */
    }

    /* Initialise background */
    background_init(server);

    wlr_log(WLR_INFO, "Server initialisation complete");
    return true;

error_scene:
    /* Scene is destroyed with display */
error_output_layout:
    /* Output layout is destroyed with display */
error_allocator:
    wlr_allocator_destroy(server->allocator);
error_renderer:
    wlr_renderer_destroy(server->renderer);
error_backend:
    wlr_backend_destroy(server->backend);
error_display:
    wl_display_destroy(server->wl_display);
    return false;
}

bool server_start(struct infinidesk_server *server) {
    /* Add a Unix socket to the Wayland display */
    const char *socket = wl_display_add_socket_auto(server->wl_display);
    if (!socket) {
        wlr_log(WLR_ERROR, "Failed to create Wayland socket");
        return false;
    }

    /* Start the backend */
    if (!wlr_backend_start(server->backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        return false;
    }

    /* Set the WAYLAND_DISPLAY environment variable */
    setenv("WAYLAND_DISPLAY", socket, true);
    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);

    return true;
}

void server_run(struct infinidesk_server *server) {
    wl_display_run(server->wl_display);
}

void server_finish(struct infinidesk_server *server) {
    wlr_log(WLR_DEBUG, "Cleaning up server resources");

    /* Clean up XWayland */
    xwayland_finish(server);

    /* Destroy views */
    struct infinidesk_view *view, *view_tmp;
    wl_list_for_each_safe(view, view_tmp, &server->views, link) {
        view_destroy(view);
    }

    /* Note: Most resources are automatically cleaned up when the display
     * is destroyed, as they're attached to it. We explicitly clean up
     * only what we need to. */

    wl_display_destroy_clients(server->wl_display);
    wl_display_destroy(server->wl_display);
}

struct infinidesk_view *server_view_at(
    struct infinidesk_server *server,
    double lx, double ly,
    struct wlr_surface **surface,
    double *sx, double *sy)
{
    /* Use the scene graph to find the node at the given coordinates */
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, lx, ly, sx, sy);

    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }

    /* Get the scene buffer from the node */
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);

    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;

    /* Walk up the tree to find our view's scene_tree */
    struct wlr_scene_tree *tree = node->parent;
    while (tree && !tree->node.data) {
        tree = tree->node.parent;
    }

    if (!tree || !tree->node.data) {
        return NULL;
    }

    return tree->node.data;
}
