/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * background.h - Checkerboard background rendering
 */

#ifndef INFINIDESK_BACKGROUND_H
#define INFINIDESK_BACKGROUND_H

/* Forward declaration */
struct infinidesk_server;

/*
 * Initialise the checkerboard background.
 * Creates a tiled background pattern in the scene graph.
 */
void background_init(struct infinidesk_server *server);

/*
 * Update the background when viewport changes.
 * Repositions tiles to cover the visible area.
 */
void background_update(struct infinidesk_server *server);

#endif /* INFINIDESK_BACKGROUND_H */
