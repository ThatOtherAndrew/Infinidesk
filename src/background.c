/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * background.c - Background rendering
 *
 * Background is now rendered directly in output.c's custom render pass.
 * These functions are kept for API compatibility but are no-ops.
 */

#define _POSIX_C_SOURCE 200809L

#include <wlr/util/log.h>

#include "infinidesk/background.h"
#include "infinidesk/server.h"

void background_init(struct infinidesk_server *server) {
    (void)server;
    wlr_log(WLR_DEBUG, "Background initialised (rendered in custom pass)");
}

void background_update(struct infinidesk_server *server) {
    (void)server;
    /* No-op - background is rendered directly in output_render_custom() */
}
