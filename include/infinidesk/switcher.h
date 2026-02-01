/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * switcher.h - Alt+Tab window switcher overlay
 */

#ifndef INFINIDESK_SWITCHER_H
#define INFINIDESK_SWITCHER_H

#include <stdbool.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/pass.h>

/* Forward declarations */
struct infinidesk_server;
struct infinidesk_view;

/*
 * Switcher overlay state
 */
struct infinidesk_switcher {
    struct infinidesk_server *server;

    bool active;
    struct infinidesk_view *selected;

    /* Rendered texture */
    struct wlr_texture *texture;
    int texture_width;
    int texture_height;

    /* Need to re-render */
    bool dirty;
};

/*
 * Initialize the switcher.
 */
void switcher_init(struct infinidesk_switcher *switcher,
                   struct infinidesk_server *server);

/*
 * Clean up switcher resources.
 */
void switcher_finish(struct infinidesk_switcher *switcher);

/*
 * Start the switcher, selecting the next view.
 */
void switcher_start(struct infinidesk_switcher *switcher);

/*
 * Cycle to the next view.
 */
void switcher_next(struct infinidesk_switcher *switcher);

/*
 * Cycle to the previous view.
 */
void switcher_prev(struct infinidesk_switcher *switcher);

/*
 * Confirm the selection and snap to the view.
 */
void switcher_confirm(struct infinidesk_switcher *switcher);

/*
 * Cancel the switcher without changing focus.
 */
void switcher_cancel(struct infinidesk_switcher *switcher);

/*
 * Render the switcher overlay.
 * Call this from the output render loop when switcher is active.
 */
void switcher_render(struct infinidesk_switcher *switcher,
                     struct wlr_render_pass *pass,
                     int output_width, int output_height);

#endif /* INFINIDESK_SWITCHER_H */
