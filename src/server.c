/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * server.c - Core server initialisation and management
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>

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
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/util/log.h>

#include "infinidesk/server.h"
#include "infinidesk/canvas.h"
#include "infinidesk/drawing.h"
#include "infinidesk/switcher.h"
#include "infinidesk/output.h"
#include "infinidesk/input.h"
#include "infinidesk/cursor.h"
#include "infinidesk/xdg_shell.h"
#include "infinidesk/layer_shell.h"
#include "infinidesk/view.h"
#include "infinidesk/background.h"

bool server_init(struct infinidesk_server *server) {
    wlr_log(WLR_DEBUG, "Initialising Wayland display");

    /* Set default output scale (will be overridden by config if loaded) */
    server->output_scale = 1.0f;

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

    /* Create fractional scale manager for HiDPI support */
    wlr_fractional_scale_manager_v1_create(server->wl_display, 1);
    wlr_log(WLR_DEBUG, "Fractional scale manager created");

    /* Create viewporter for surface cropping/scaling (required by swww, etc.) */
    wlr_viewporter_create(server->wl_display);
    wlr_log(WLR_DEBUG, "Viewporter created");

    /* Create the output layout */
    wlr_log(WLR_DEBUG, "Creating output layout");
    server->output_layout = wlr_output_layout_create(server->wl_display);
    if (!server->output_layout) {
        wlr_log(WLR_ERROR, "Failed to create output layout");
        goto error_allocator;
    }

    /* Create xdg-output manager for clients that need output info */
    wlr_xdg_output_manager_v1_create(server->wl_display, server->output_layout);
    wlr_log(WLR_DEBUG, "XDG output manager created");

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

    /* Initialise drawing layer */
    drawing_init(&server->drawing, server);

    /* Initialise alt-tab switcher */
    switcher_init(&server->switcher, server);

    /* Initialise output handling */
    output_init(server);

    /* Initialise input handling */
    input_init(server);

    /* Initialise cursor */
    cursor_init(server);

    /* Initialise XDG shell */
    xdg_shell_init(server);

    /* Initialise layer shell */
    layer_shell_init(server);

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

    /* Destroy views */
    struct infinidesk_view *view, *view_tmp;
    wl_list_for_each_safe(view, view_tmp, &server->views, link) {
        view_destroy(view);
    }

    /* Clean up drawing layer */
    drawing_finish(&server->drawing);

    /* Clean up switcher */
    switcher_finish(&server->switcher);

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
    /*
     * Custom hit testing that accounts for canvas scale.
     *
     * The scene graph doesn't know about our custom scaled rendering,
     * so we must do hit testing ourselves by matching the rendering logic.
     *
     * Key insight: We render surfaces at scaled positions and sizes, so we
     * must do hit testing in screen space against the scaled bounds, then
     * convert back to surface-local coordinates.
     */

    struct infinidesk_canvas *canvas = &server->canvas;

    /* Views are ordered front-to-back in the list (front first) */
    struct infinidesk_view *view;
    wl_list_for_each(view, &server->views, link) {
        if (!view->xdg_toplevel->base->surface->mapped) {
            continue;
        }

        /* Get the view geometry */
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);

        /*
         * Calculate where the view is rendered on screen.
         * This must match the calculation in view_render().
         *
         * In view_render:
         *   base_x = round(screen_x) - round(geo.x * scale)
         *
         * Where screen_x = (view->x - viewport_x) * scale
         */
        double screen_x, screen_y;
        canvas_to_screen(canvas, view->x, view->y, &screen_x, &screen_y);

        /*
         * The top-left corner of the rendered surface on screen.
         * This accounts for CSD windows where geo.x/y may be non-zero.
         */
        double render_x = screen_x - geo.x * canvas->scale;
        double render_y = screen_y - geo.y * canvas->scale;

        /*
         * The rendered size on screen. We use the geometry dimensions
         * (the "window" portion) scaled by canvas scale.
         */
        double render_width = geo.width * canvas->scale;
        double render_height = geo.height * canvas->scale;

        /* Check if cursor (in screen coords) is within rendered geometry bounds */
        if (lx >= render_x && lx < render_x + render_width &&
            ly >= render_y && ly < render_y + render_height) {

            /*
             * Found a view - calculate surface-local coordinates.
             *
             * The cursor position relative to where we started rendering
             * (render_x, render_y), divided by scale, gives us the position
             * relative to the content origin (geometry origin).
             */
            double content_local_x = (lx - render_x) / canvas->scale;
            double content_local_y = (ly - render_y) / canvas->scale;

            /*
             * Use wlr_xdg_surface_surface_at to find the actual surface
             * (handles subsurfaces, popups, etc.). This function expects
             * coordinates relative to the XDG surface origin (buffer origin),
             * not the geometry/content origin. For CSD windows, we must add
             * back the geometry offset.
             */
            double surface_local_x = content_local_x + geo.x;
            double surface_local_y = content_local_y + geo.y;

            double sub_x, sub_y;
            struct wlr_surface *found_surface = wlr_xdg_surface_surface_at(
                view->xdg_toplevel->base,
                surface_local_x, surface_local_y,
                &sub_x, &sub_y);

            if (found_surface) {
                *surface = found_surface;
                *sx = sub_x;
                *sy = sub_y;
                return view;
            }

            /*
             * If no surface found at exact point (e.g., in transparent
             * regions of CSD), return the main surface anyway.
             * Use content-local coordinates for the main surface.
             */
            *surface = view->xdg_toplevel->base->surface;
            *sx = content_local_x;
            *sy = content_local_y;
            return view;
        }
    }

    /* No view found under cursor */
    *surface = NULL;
    *sx = 0;
    *sy = 0;
    return NULL;
}
