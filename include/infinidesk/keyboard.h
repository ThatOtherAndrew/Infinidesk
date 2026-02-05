/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * keyboard.h - Keyboard input handling
 */

#ifndef INFINIDESK_KEYBOARD_H
#define INFINIDESK_KEYBOARD_H

#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>

/* Forward declaration */
struct infinidesk_server;

/*
 * Keyboard device wrapper.
 */
struct infinidesk_keyboard {
    struct wl_list link; /* infinidesk_server.keyboards */
    struct infinidesk_server *server;
    struct wlr_keyboard *wlr_keyboard;

    struct wl_listener key;
    struct wl_listener modifiers;
    struct wl_listener destroy;
};

/*
 * Create and configure a keyboard for the given device.
 */
void keyboard_create(struct infinidesk_server *server,
                     struct wlr_keyboard *wlr_keyboard);

/*
 * Handle keyboard key events.
 */
void keyboard_handle_key(struct wl_listener *listener, void *data);

/*
 * Handle keyboard modifier changes.
 */
void keyboard_handle_modifiers(struct wl_listener *listener, void *data);

/*
 * Handle keyboard destruction.
 */
void keyboard_handle_destroy(struct wl_listener *listener, void *data);

/*
 * Process compositor keybindings.
 * Returns true if the key was handled by the compositor,
 * false if it should be forwarded to the client.
 */
bool keyboard_handle_keybinding(struct infinidesk_server *server,
                                uint32_t modifiers, xkb_keysym_t sym);

#endif /* INFINIDESK_KEYBOARD_H */
