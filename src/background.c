/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * background.c - Background rendering
 *
 * Simple solid colour background for now.
 * TODO: Implement efficient checkerboard pattern.
 */

#define _POSIX_C_SOURCE 200809L

#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "infinidesk/background.h"
#include "infinidesk/server.h"

/* Dark grey background colour */
static const float bg_colour[4] = { 0.18f, 0.18f, 0.18f, 1.0f };  /* #2e2e2e */

/* Single large background rectangle */
static struct wlr_scene_rect *bg_rect = NULL;

void background_init(struct infinidesk_server *server) {
    /* Create a single large rectangle to cover any reasonable screen size */
    bg_rect = wlr_scene_rect_create(
        server->background_tree,
        8192, 8192,  /* Large enough for any screen */
        bg_colour);

    if (!bg_rect) {
        wlr_log(WLR_ERROR, "Failed to create background rectangle");
        return;
    }

    /* Position at top-left with margin for panning */
    wlr_scene_node_set_position(&bg_rect->node, -4096, -4096);

    wlr_log(WLR_DEBUG, "Background initialised");
}

void background_update(struct infinidesk_server *server) {
    (void)server;
    /* No update needed for solid colour background */
}
