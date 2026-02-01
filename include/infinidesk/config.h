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

/*
 * Configuration structure.
 */
struct infinidesk_config {
    /* Array of startup commands */
    char **startup_commands;
    int startup_command_count;

    /* Output scale factor (HiDPI scaling) */
    float scale;
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
