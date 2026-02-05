/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * layer_shell.h - wlr-layer-shell-unstable-v1 protocol support
 */

#ifndef INFINIDESK_LAYER_SHELL_H
#define INFINIDESK_LAYER_SHELL_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_scene.h>

/* Forward declarations */
struct infinidesk_server;
struct infinidesk_output;

/* Number of layer shell layers */
#define LAYER_SHELL_LAYER_COUNT 4

/*
 * A layer surface represents a surface from the layer-shell protocol.
 * Layer surfaces are anchored to outputs and rendered at specific z-levels
 * (background, bottom, top, overlay).
 */
struct infinidesk_layer_surface {
    struct wl_list link; /* infinidesk_output.layer_surfaces[layer] */
    struct infinidesk_server *server;
    struct infinidesk_output *output;

    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;
    struct wlr_scene_tree *scene_tree;

    /* Event listeners */
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener new_popup;
};

/*
 * Initialise layer shell support for the server.
 */
void layer_shell_init(struct infinidesk_server *server);

/*
 * Handle a new layer surface being created.
 */
void handle_new_layer_surface(struct wl_listener *listener, void *data);

/*
 * Arrange all layer surfaces on an output.
 * This calculates positions based on anchors and updates exclusive zones.
 * Should be called when:
 *   - A layer surface maps/unmaps
 *   - Output geometry changes
 *   - Layer surface commits with changed state
 */
void layer_shell_arrange(struct infinidesk_output *output);

/*
 * Get the usable area of an output after accounting for exclusive zones.
 */
void layer_shell_get_usable_area(struct infinidesk_output *output,
                                 struct wlr_box *usable_area);

/*
 * Find a layer surface at the given output-local coordinates.
 * Returns the surface and surface-local coordinates, or NULL if not found.
 * Searches from overlay to background (top to bottom in z-order).
 */
struct infinidesk_layer_surface *
layer_surface_at(struct infinidesk_output *output, double ox, double oy,
                 struct wlr_surface **surface, double *sx, double *sy);

#endif /* INFINIDESK_LAYER_SHELL_H */
