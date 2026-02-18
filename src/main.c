/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * main.c - Entry point
 */

#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "infinidesk/config.h"
#include "infinidesk/server.h"

static struct infinidesk_server server = {0};

static void print_usage(const char *prog_name) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  -s, --startup <cmd>  Command to run at startup\n"
            "  -d, --debug          Enable debug logging\n"
            "  -h, --help           Show this help message\n"
            "\n"
            "Infinidesk is an infinite canvas Wayland compositor.\n"
            "\n"
            "Default keybindings (configurable in "
            "~/.config/infinidesk/infinidesk.toml):\n"
            "  Super + Return     Launch terminal (kitty)\n"
            "  Super + Q          Close focused window\n"
            "  Super + Escape     Exit compositor\n"
            "  Super + D          Toggle drawing mode\n"
            "  Super + G          Gather windows\n"
            "  Alt + Tab          Window switcher\n"
            "  Super + Left-drag  Move window\n"
            "  Super + Right-drag Pan canvas\n"
            "  Super + Scroll     Zoom canvas\n"
            "  Ctrl + Alt + F1-12 Switch VT\n",
            prog_name);
}

static void handle_signal(int sig) {
    (void)sig;
    wl_display_terminate(server.wl_display);
}

int main(int argc, char *argv[]) {
    char *startup_cmd = NULL;
    enum wlr_log_importance log_level = WLR_INFO;

    static struct option long_options[] = {
        {"startup", required_argument, NULL, 's'},
        {"debug", no_argument, NULL, 'd'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "s:dh", long_options, NULL)) != -1) {
        switch (opt) {
        case 's':
            startup_cmd = optarg;
            break;
        case 'd':
            log_level = WLR_DEBUG;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    wlr_log_init(log_level, NULL);
    wlr_log(WLR_INFO, "Starting Infinidesk");

    /* Set up signal handlers */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Initialise the server */
    if (!server_init(&server)) {
        wlr_log(WLR_ERROR, "Failed to initialise server");
        return EXIT_FAILURE;
    }

    /* Load configuration file (before server_start so output scale is set) */
    struct infinidesk_config config;
    if (!config_load(&config)) {
        wlr_log(WLR_ERROR, "Failed to load config, continuing with defaults");
        /* server.output_scale already set to 1.0f in server_init */
    } else {
        server.output_scale = config.scale;

        /*
         * Transfer keybind ownership from config to server.
         * We steal the pointer so config_free() won't free it while
         * the server is still running.
         */
        server.keybinds = config.keybinds;
        server.keybind_count = config.keybind_count;
        config.keybinds = NULL;
        config.keybind_count = 0;
    }

    /* Start the backend */
    if (!server_start(&server)) {
        wlr_log(WLR_ERROR, "Failed to start server");
        config_free(&config);
        server_finish(&server);
        return EXIT_FAILURE;
    }

    /* Run startup commands from config file */
    config_run_startup_commands(&config);

    /* Run command-line startup command if specified (in addition to config) */
    if (startup_cmd) {
        wlr_log(WLR_INFO, "Running command-line startup command: %s",
                startup_cmd);
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (char *)NULL);
            _exit(EXIT_FAILURE);
        }
    }

    /* Run the event loop */
    wlr_log(WLR_INFO, "Running compositor");
    server_run(&server);

    /* Clean up */
    wlr_log(WLR_INFO, "Shutting down");
    config_free(&config);
    server_finish(&server);

    return EXIT_SUCCESS;
}
