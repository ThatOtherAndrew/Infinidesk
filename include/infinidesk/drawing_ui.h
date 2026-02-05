/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * drawing_ui.h - Drawing UI panel
 */

#ifndef INFINIDESK_DRAWING_UI_H
#define INFINIDESK_DRAWING_UI_H

#include <stdbool.h>

/* Forward declarations */
struct drawing_layer;
struct wlr_render_pass;

/* RGB color structure */
struct drawing_color {
    float r;
    float g;
    float b;
};

/* Predefined colors */
#define COLOR_RED ((struct drawing_color){1.0f, 0.2f, 0.2f})
#define COLOR_GREEN ((struct drawing_color){0.2f, 1.0f, 0.2f})
#define COLOR_BLUE ((struct drawing_color){0.2f, 0.5f, 1.0f})

/* UI button identifiers */
enum drawing_ui_button {
    UI_BUTTON_NONE = 0,
    UI_BUTTON_COLOR_RED,
    UI_BUTTON_COLOR_GREEN,
    UI_BUTTON_COLOR_BLUE,
    UI_BUTTON_UNDO,
    UI_BUTTON_REDO,
    UI_BUTTON_CLEAR,
};

/* UI panel state */
struct drawing_ui_panel {
    /* Panel position and dimensions */
    int x;
    int y;
    int width;
    int height;

    /* Interaction state */
    enum drawing_ui_button hovered_button;
    enum drawing_ui_button pressed_button;
};

/*
 * Initialize the UI panel with screen dimensions.
 */
void drawing_ui_init(struct drawing_ui_panel *panel, int screen_width,
                     int screen_height);

/*
 * Render the UI panel.
 * output_scale is the HiDPI scale factor for converting to physical pixels.
 */
void drawing_ui_render(struct drawing_ui_panel *panel,
                       struct drawing_layer *drawing,
                       struct wlr_render_pass *pass, int screen_width,
                       int screen_height, float output_scale);

/*
 * Get the button at the given screen coordinates.
 * Returns UI_BUTTON_NONE if no button is at that position.
 */
enum drawing_ui_button drawing_ui_get_button_at(struct drawing_ui_panel *panel,
                                                double x, double y);

/*
 * Handle a click on a UI button.
 */
void drawing_ui_handle_click(struct drawing_ui_panel *panel,
                             struct drawing_layer *drawing,
                             enum drawing_ui_button button);

/*
 * Update the hover state based on cursor position.
 */
void drawing_ui_update_hover(struct drawing_ui_panel *panel, double x,
                             double y);

#endif /* INFINIDESK_DRAWING_UI_H */
