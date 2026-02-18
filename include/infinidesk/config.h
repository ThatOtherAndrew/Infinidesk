/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * config.h - Configuration file handling
 */

#ifndef INFINIDESK_CONFIG_H
#define INFINIDESK_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Keybind action type.
 * A keybind either triggers a built-in compositor action or executes
 * an external shell command.
 */
enum keybind_type {
    KEYBIND_ACTION, /* Built-in compositor action (e.g. "close_window") */
    KEYBIND_EXEC,   /* Execute external command (e.g. "exec:kitty") */
};

/*
 * A single keybinding definition.
 * Parsed from the [keybinds] section of the config file.
 *
 * The modifier field is a bitmask of WLR_MODIFIER_* values.
 * The key field is an XKB keysym (xkb_keysym_t is uint32_t).
 */
struct keybind {
    uint32_t modifiers; /* WLR_MODIFIER_* bitmask */
    uint32_t key;       /* XKB keysym */
    enum keybind_type type;
    char *value; /* Action name or shell command */
};

/*
 * Configuration structure.
 */
struct infinidesk_config {
    /* Array of startup commands */
    char **startup_commands;
    int startup_command_count;

    /* Output scale factor (HiDPI scaling) */
    float scale;

    /* Keybindings */
    struct keybind *keybinds;
    int keybind_count;
};

/*
 * Load configuration from the default config file.
 * Creates the config file and parent directories if they don't exist.
 *
 * Config file location: ~/.config/infinidesk/infinidesk.toml
 *
 * Returns true on success (including if file doesn't exist and was created),
 * false on error.
 */
bool config_load(struct infinidesk_config *config);

/*
 * Free resources allocated by config_load.
 */
void config_free(struct infinidesk_config *config);

/*
 * Run all startup commands from the configuration.
 * Each command is executed in a forked process.
 */
void config_run_startup_commands(struct infinidesk_config *config);

#endif /* INFINIDESK_CONFIG_H */
