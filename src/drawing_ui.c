/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * drawing_ui.c - Drawing UI panel implementation
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <wlr/render/pass.h>
#include <wlr/util/log.h>

#include "infinidesk/drawing_ui.h"
#include "infinidesk/drawing.h"

/* UI Layout constants */
#define UI_PANEL_X 20
#define UI_BUTTON_WIDTH 50
#define UI_BUTTON_HEIGHT 50
#define UI_BUTTON_SPACING 10
#define UI_PANEL_PADDING 10
#define UI_SEPARATOR_HEIGHT 20

/* UI Colors */
#define UI_BG_COLOR       {0.15f, 0.15f, 0.15f, 0.9f}
#define UI_BUTTON_NORMAL  {0.25f, 0.25f, 0.25f, 1.0f}
#define UI_BUTTON_HOVER   {0.35f, 0.35f, 0.35f, 1.0f}
#define UI_BUTTON_PRESSED {0.15f, 0.15f, 0.15f, 1.0f}
#define UI_BUTTON_SELECTED {0.45f, 0.45f, 0.45f, 1.0f}
#define UI_ICON_COLOR     {0.9f, 0.9f, 0.9f, 1.0f}

/* Forward declarations */
static void render_button(struct wlr_render_pass *pass, int x, int y,
                         int width, int height, float color[4]);
static void render_color_button(struct wlr_render_pass *pass, int x, int y,
                               int width, int height, struct drawing_color color,
                               bool is_selected, bool is_hovered);
static void render_undo_icon(struct wlr_render_pass *pass, int x, int y, float scale);
static void render_redo_icon(struct wlr_render_pass *pass, int x, int y, float scale);
static void render_clear_icon(struct wlr_render_pass *pass, int x, int y, float scale);
static int get_button_y(struct drawing_ui_panel *panel, int button_index);
static bool is_color_equal(struct drawing_color a, struct drawing_color b);

void drawing_ui_init(struct drawing_ui_panel *panel, int screen_width, int screen_height) {
    (void)screen_width;

    panel->width = UI_BUTTON_WIDTH + 2 * UI_PANEL_PADDING;

    /* Calculate total height: 3 color buttons + separator + 3 action buttons */
    panel->height = UI_PANEL_PADDING * 2 +
                   UI_BUTTON_HEIGHT * 6 +
                   UI_BUTTON_SPACING * 5 +
                   UI_SEPARATOR_HEIGHT;

    panel->x = UI_PANEL_X;
    panel->y = (screen_height - panel->height) / 2;

    panel->hovered_button = UI_BUTTON_NONE;
    panel->pressed_button = UI_BUTTON_NONE;

    wlr_log(WLR_DEBUG, "UI panel initialized at (%d, %d) size %dx%d",
            panel->x, panel->y, panel->width, panel->height);
}

void drawing_ui_render(struct drawing_ui_panel *panel,
                       struct drawing_layer *drawing,
                       struct wlr_render_pass *pass,
                       int screen_width, int screen_height,
                       float output_scale) {
    (void)screen_width;
    (void)screen_height;

    /*
     * Scale all UI coordinates and sizes to physical pixels.
     * The panel position/size are in logical coordinates, but we render
     * in physical pixels.
     */
    float s = output_scale;

    /* Render panel background */
    float bg_color[] = UI_BG_COLOR;
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
        .box = {
            .x = (int)(panel->x * s),
            .y = (int)(panel->y * s),
            .width = (int)(panel->width * s),
            .height = (int)(panel->height * s),
        },
        .color = {
            .r = bg_color[0],
            .g = bg_color[1],
            .b = bg_color[2],
            .a = bg_color[3],
        },
    });

    /* Button positions (scaled) */
    int button_x = (int)((panel->x + UI_PANEL_PADDING) * s);
    int button_w = (int)(UI_BUTTON_WIDTH * s);
    int button_h = (int)(UI_BUTTON_HEIGHT * s);

    /* Color buttons */
    render_color_button(pass, button_x, (int)(get_button_y(panel, 0) * s),
                       button_w, button_h, COLOR_RED,
                       is_color_equal(drawing->current_color, COLOR_RED),
                       panel->hovered_button == UI_BUTTON_COLOR_RED);

    render_color_button(pass, button_x, (int)(get_button_y(panel, 1) * s),
                       button_w, button_h, COLOR_GREEN,
                       is_color_equal(drawing->current_color, COLOR_GREEN),
                       panel->hovered_button == UI_BUTTON_COLOR_GREEN);

    render_color_button(pass, button_x, (int)(get_button_y(panel, 2) * s),
                       button_w, button_h, COLOR_BLUE,
                       is_color_equal(drawing->current_color, COLOR_BLUE),
                       panel->hovered_button == UI_BUTTON_COLOR_BLUE);

    /* Action buttons (after separator) */
    int undo_y = (int)(get_button_y(panel, 3) * s);
    int redo_y = (int)(get_button_y(panel, 4) * s);
    int clear_y = (int)(get_button_y(panel, 5) * s);

    /* Undo button */
    float undo_color[4];
    if (panel->hovered_button == UI_BUTTON_UNDO) {
        float hover[] = UI_BUTTON_HOVER;
        memcpy(undo_color, hover, sizeof(undo_color));
    } else {
        float normal[] = UI_BUTTON_NORMAL;
        memcpy(undo_color, normal, sizeof(undo_color));
    }
    render_button(pass, button_x, undo_y, button_w, button_h, undo_color);
    render_undo_icon(pass, button_x, undo_y, s);

    /* Redo button */
    float redo_color[4];
    if (panel->hovered_button == UI_BUTTON_REDO) {
        float hover[] = UI_BUTTON_HOVER;
        memcpy(redo_color, hover, sizeof(redo_color));
    } else {
        float normal[] = UI_BUTTON_NORMAL;
        memcpy(redo_color, normal, sizeof(redo_color));
    }
    render_button(pass, button_x, redo_y, button_w, button_h, redo_color);
    render_redo_icon(pass, button_x, redo_y, s);

    /* Clear button */
    float clear_color[4];
    if (panel->hovered_button == UI_BUTTON_CLEAR) {
        float hover[] = UI_BUTTON_HOVER;
        memcpy(clear_color, hover, sizeof(clear_color));
    } else {
        float normal[] = UI_BUTTON_NORMAL;
        memcpy(clear_color, normal, sizeof(clear_color));
    }
    render_button(pass, button_x, clear_y, button_w, button_h, clear_color);
    render_clear_icon(pass, button_x, clear_y, s);
}

enum drawing_ui_button drawing_ui_get_button_at(struct drawing_ui_panel *panel,
                                                  double x, double y) {
    /* Check if cursor is within panel bounds */
    if (x < panel->x || x >= panel->x + panel->width ||
        y < panel->y || y >= panel->y + panel->height) {
        return UI_BUTTON_NONE;
    }

    int button_x = panel->x + UI_PANEL_PADDING;
    int relative_x = (int)x - button_x;

    /* Check X coordinate */
    if (relative_x < 0 || relative_x >= UI_BUTTON_WIDTH) {
        return UI_BUTTON_NONE;
    }

    /* Check each button's Y coordinate */
    for (int i = 0; i < 6; i++) {
        int button_y = get_button_y(panel, i);
        if ((int)y >= button_y && (int)y < button_y + UI_BUTTON_HEIGHT) {
            /* Map button index to enum */
            switch (i) {
                case 0: return UI_BUTTON_COLOR_RED;
                case 1: return UI_BUTTON_COLOR_GREEN;
                case 2: return UI_BUTTON_COLOR_BLUE;
                case 3: return UI_BUTTON_UNDO;
                case 4: return UI_BUTTON_REDO;
                case 5: return UI_BUTTON_CLEAR;
            }
        }
    }

    return UI_BUTTON_NONE;
}

void drawing_ui_handle_click(struct drawing_ui_panel *panel,
                              struct drawing_layer *drawing,
                              enum drawing_ui_button button) {
    (void)panel;

    switch (button) {
        case UI_BUTTON_COLOR_RED:
            drawing->current_color = COLOR_RED;
            wlr_log(WLR_DEBUG, "Selected red color");
            break;

        case UI_BUTTON_COLOR_GREEN:
            drawing->current_color = COLOR_GREEN;
            wlr_log(WLR_DEBUG, "Selected green color");
            break;

        case UI_BUTTON_COLOR_BLUE:
            drawing->current_color = COLOR_BLUE;
            wlr_log(WLR_DEBUG, "Selected blue color");
            break;

        case UI_BUTTON_UNDO:
            drawing_undo_last(drawing);
            break;

        case UI_BUTTON_REDO:
            drawing_redo_last(drawing);
            break;

        case UI_BUTTON_CLEAR:
            drawing_clear_all(drawing);
            break;

        case UI_BUTTON_NONE:
            break;
    }
}

void drawing_ui_update_hover(struct drawing_ui_panel *panel,
                              double x, double y) {
    panel->hovered_button = drawing_ui_get_button_at(panel, x, y);
}

/* Helper functions */

static void render_button(struct wlr_render_pass *pass, int x, int y,
                         int width, int height, float color[4]) {
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
        .box = {
            .x = x,
            .y = y,
            .width = width,
            .height = height,
        },
        .color = {
            .r = color[0],
            .g = color[1],
            .b = color[2],
            .a = color[3],
        },
    });
}

static void render_color_button(struct wlr_render_pass *pass, int x, int y,
                               int width, int height, struct drawing_color color,
                               bool is_selected, bool is_hovered) {
    /* Button background */
    float bg_color[4];
    if (is_selected) {
        float selected[] = UI_BUTTON_SELECTED;
        memcpy(bg_color, selected, sizeof(bg_color));
    } else if (is_hovered) {
        float hover[] = UI_BUTTON_HOVER;
        memcpy(bg_color, hover, sizeof(bg_color));
    } else {
        float normal[] = UI_BUTTON_NORMAL;
        memcpy(bg_color, normal, sizeof(bg_color));
    }
    render_button(pass, x, y, width, height, bg_color);

    /* Color swatch (centered, smaller than button) */
    int swatch_size = width - 16;
    int swatch_x = x + (width - swatch_size) / 2;
    int swatch_y = y + (height - swatch_size) / 2;

    float swatch_color[4] = {color.r, color.g, color.b, 1.0f};
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
        .box = {
            .x = swatch_x,
            .y = swatch_y,
            .width = swatch_size,
            .height = swatch_size,
        },
        .color = {
            .r = swatch_color[0],
            .g = swatch_color[1],
            .b = swatch_color[2],
            .a = swatch_color[3],
        },
    });
}

static void render_undo_icon(struct wlr_render_pass *pass, int x, int y, float scale) {
    float icon_color[] = UI_ICON_COLOR;
    int button_w = (int)(UI_BUTTON_WIDTH * scale);
    int button_h = (int)(UI_BUTTON_HEIGHT * scale);
    int center_x = x + button_w / 2;
    int center_y = y + button_h / 2;

    /* Left-pointing triangle */
    int icon_size = (int)(12 * scale);
    int line_w = (int)(2 * scale);
    if (line_w < 1) line_w = 1;

    for (int i = 0; i < icon_size; i++) {
        int h = (int)((i * 2 + 1) * scale / icon_size * icon_size);
        if (h < 1) h = 1;
        wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
            .box = {
                .x = center_x - (int)(6 * scale) + i,
                .y = center_y - i,
                .width = line_w,
                .height = i * 2 + 1,
            },
            .color = {
                .r = icon_color[0],
                .g = icon_color[1],
                .b = icon_color[2],
                .a = icon_color[3],
            },
        });
    }
}

static void render_redo_icon(struct wlr_render_pass *pass, int x, int y, float scale) {
    float icon_color[] = UI_ICON_COLOR;
    int button_w = (int)(UI_BUTTON_WIDTH * scale);
    int button_h = (int)(UI_BUTTON_HEIGHT * scale);
    int center_x = x + button_w / 2;
    int center_y = y + button_h / 2;

    /* Right-pointing triangle */
    int icon_size = (int)(12 * scale);
    int line_w = (int)(2 * scale);
    if (line_w < 1) line_w = 1;

    for (int i = 0; i < icon_size; i++) {
        wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
            .box = {
                .x = center_x + (int)(6 * scale) - i,
                .y = center_y - i,
                .width = line_w,
                .height = i * 2 + 1,
            },
            .color = {
                .r = icon_color[0],
                .g = icon_color[1],
                .b = icon_color[2],
                .a = icon_color[3],
            },
        });
    }
}

static void render_clear_icon(struct wlr_render_pass *pass, int x, int y, float scale) {
    float icon_color[] = UI_ICON_COLOR;
    int button_w = (int)(UI_BUTTON_WIDTH * scale);
    int button_h = (int)(UI_BUTTON_HEIGHT * scale);
    int center_x = x + button_w / 2;
    int center_y = y + button_h / 2;

    /* X shape using two diagonal rectangles */
    int size = (int)(16 * scale);
    int dot_size = (int)(3 * scale);
    if (dot_size < 1) dot_size = 1;

    /* Top-left to bottom-right diagonal */
    for (int i = 0; i < size; i++) {
        wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
            .box = {
                .x = center_x - size/2 + i,
                .y = center_y - size/2 + i,
                .width = dot_size,
                .height = dot_size,
            },
            .color = {
                .r = icon_color[0],
                .g = icon_color[1],
                .b = icon_color[2],
                .a = icon_color[3],
            },
        });
    }

    /* Top-right to bottom-left diagonal */
    for (int i = 0; i < size; i++) {
        wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
            .box = {
                .x = center_x + size/2 - i,
                .y = center_y - size/2 + i,
                .width = dot_size,
                .height = dot_size,
            },
            .color = {
                .r = icon_color[0],
                .g = icon_color[1],
                .b = icon_color[2],
                .a = icon_color[3],
            },
        });
    }
}

static int get_button_y(struct drawing_ui_panel *panel, int button_index) {
    int y = panel->y + UI_PANEL_PADDING;

    if (button_index < 3) {
        /* Color buttons (0-2) */
        y += button_index * (UI_BUTTON_HEIGHT + UI_BUTTON_SPACING);
    } else {
        /* Action buttons (3-5) - add separator space */
        y += 3 * (UI_BUTTON_HEIGHT + UI_BUTTON_SPACING);
        y += UI_SEPARATOR_HEIGHT;
        y += (button_index - 3) * (UI_BUTTON_HEIGHT + UI_BUTTON_SPACING);
    }

    return y;
}

static bool is_color_equal(struct drawing_color a, struct drawing_color b) {
    return fabsf(a.r - b.r) < 0.01f &&
           fabsf(a.g - b.g) < 0.01f &&
           fabsf(a.b - b.b) < 0.01f;
}
