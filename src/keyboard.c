/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * keyboard.c - Keyboard input handling
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <unistd.h>

#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "infinidesk/keyboard.h"
#include "infinidesk/server.h"
#include "infinidesk/view.h"
#include "infinidesk/drawing.h"

void keyboard_create(struct infinidesk_server *server,
                     struct wlr_keyboard *wlr_keyboard)
{
    struct infinidesk_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    if (!keyboard) {
        wlr_log(WLR_ERROR, "Failed to allocate keyboard");
        return;
    }

    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    /* Set up keyboard with default XKB keymap */
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) {
        wlr_log(WLR_ERROR, "Failed to create XKB context");
        free(keyboard);
        return;
    }

    struct xkb_keymap *keymap = xkb_keymap_new_from_names(
        context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap) {
        wlr_log(WLR_ERROR, "Failed to create XKB keymap");
        xkb_context_unref(context);
        free(keyboard);
        return;
    }

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    /* Set up repeat info (rate in Hz, delay in ms) */
    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    /* Set up event listeners */
    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&wlr_keyboard->base.events.destroy, &keyboard->destroy);

    /* Add to server's keyboard list */
    wl_list_insert(&server->keyboards, &keyboard->link);

    /* Set the keyboard for the seat */
    wlr_seat_set_keyboard(server->seat, wlr_keyboard);

    wlr_log(WLR_DEBUG, "Keyboard created and configured");
}

void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct infinidesk_keyboard *keyboard =
        wl_container_of(listener, keyboard, key);
    struct infinidesk_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;

    /* Get the keycode and translate to XKB keysym */
    uint32_t keycode = event->keycode + 8;  /* libinput -> XKB offset */
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
        keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    /* Get current modifiers */
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);

    /* Track Alt key state (used for canvas operations)
     * Note: Using Alt instead of Super for better nested compositor support */
    for (int i = 0; i < nsyms; i++) {
        if (syms[i] == XKB_KEY_Alt_L || syms[i] == XKB_KEY_Alt_R) {
            server->super_pressed = (event->state == WL_KEYBOARD_KEY_STATE_PRESSED);
            break;
        }
    }

    /* Check for compositor keybindings on key press */
    bool handled = false;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            handled = keyboard_handle_keybinding(server, modifiers, syms[i]);
            if (handled) {
                break;
            }
        }
    }

    /* If the key wasn't handled by a keybinding, forward it to the client */
    if (!handled) {
        wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(server->seat,
            event->time_msec, event->keycode, event->state);
    }
}

void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_keyboard *keyboard =
        wl_container_of(listener, keyboard, modifiers);
    struct infinidesk_server *server = keyboard->server;

    /* Send modifiers to the focused client */
    wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(server->seat,
        &keyboard->wlr_keyboard->modifiers);
}

void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_keyboard *keyboard =
        wl_container_of(listener, keyboard, destroy);

    wlr_log(WLR_DEBUG, "Keyboard destroyed");

    /* Remove listeners */
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->destroy.link);

    /* Remove from server list */
    wl_list_remove(&keyboard->link);

    free(keyboard);
}

bool keyboard_handle_keybinding(struct infinidesk_server *server,
                                uint32_t modifiers,
                                xkb_keysym_t sym)
{
    /*
     * Compositor keybindings (using Alt for nested compositor compatibility):
     * - Alt + Enter:  Launch terminal (kitty)
     * - Alt + Q:      Close focused window
     * - Alt + Escape: Exit compositor
     * - Alt + D:      Toggle drawing mode
     * - Alt + C:      Clear all drawings
     * - Alt + U:      Undo last stroke
     */

    /* Check for Alt modifier */
    if (!(modifiers & WLR_MODIFIER_ALT)) {
        return false;
    }

    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        /* Alt + Enter: Launch terminal */
        wlr_log(WLR_INFO, "Launching terminal");
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", "kitty", (char *)NULL);
            _exit(EXIT_FAILURE);
        }
        return true;

    case XKB_KEY_q:
    case XKB_KEY_Q:
        /* Alt + Q: Close focused window */
        if (!wl_list_empty(&server->views)) {
            struct infinidesk_view *view =
                wl_container_of(server->views.next, view, link);
            wlr_log(WLR_DEBUG, "Closing focused view %p", (void *)view);
            view_close(view);
        }
        return true;

    case XKB_KEY_Escape:
        /* Alt + Escape: Exit compositor */
        wlr_log(WLR_INFO, "Exiting compositor");
        wl_display_terminate(server->wl_display);
        return true;

    case XKB_KEY_d:
    case XKB_KEY_D:
        /* Alt + D: Toggle drawing mode */
        drawing_toggle_mode(&server->drawing);
        return true;

    case XKB_KEY_c:
    case XKB_KEY_C:
        /* Alt + C: Clear all drawings */
        drawing_clear_all(&server->drawing);
        return true;

    case XKB_KEY_u:
    case XKB_KEY_U:
        /* Alt + U: Undo last stroke */
        drawing_undo_last(&server->drawing);
        return true;
        
    

    default:
        return false;
    }
}
