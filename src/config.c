/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * config.c - Configuration file handling
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wlr/util/log.h>

#include "infinidesk/config.h"

#define CONFIG_DIR ".config/infinidesk"
#define CONFIG_FILE "infinidesk.toml"
#define MAX_LINE_LENGTH 4096
#define INITIAL_COMMANDS_CAPACITY 8

static const char *DEFAULT_CONFIG =
    "# Infinidesk configuration file\n"
    "\n"
    "# Output scale factor for HiDPI displays (e.g., 1.0, 1.5, 2.0)\n"
    "scale = 1.0\n"
    "\n"
    "# Startup commands are executed when the compositor starts.\n"
    "# Each command runs in its own shell process.\n"
    "startup = [\n"
    "]\n";

/*
 * Create parent directories recursively.
 */
static bool create_directories(const char *path) {
    char *path_copy = strdup(path);
    if (!path_copy) {
        return false;
    }

    char *p = path_copy;
    while (*p) {
        /* Skip leading slashes */
        while (*p == '/')
            p++;

        /* Find the next slash */
        char *slash = strchr(p, '/');
        if (!slash)
            break;

        /* Temporarily terminate at this point */
        *slash = '\0';

        /* Create this directory level */
        if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
            wlr_log(WLR_ERROR, "Failed to create directory %s: %s", path_copy,
                    strerror(errno));
            free(path_copy);
            return false;
        }

        /* Restore the slash and continue */
        *slash = '/';
        p = slash + 1;
    }

    free(path_copy);
    return true;
}

/*
 * Get the full path to the config file.
 * Caller must free the returned string.
 */
static char *get_config_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        wlr_log(WLR_ERROR, "HOME environment variable not set");
        return NULL;
    }

    size_t len =
        strlen(home) + 1 + strlen(CONFIG_DIR) + 1 + strlen(CONFIG_FILE) + 1;
    char *path = malloc(len);
    if (!path) {
        return NULL;
    }

    snprintf(path, len, "%s/%s/%s", home, CONFIG_DIR, CONFIG_FILE);
    return path;
}

/*
 * Create the default config file if it doesn't exist.
 */
static bool ensure_config_file(const char *path) {
    /* Check if file already exists */
    if (access(path, F_OK) == 0) {
        return true;
    }

    /* Create parent directories */
    if (!create_directories(path)) {
        return false;
    }

    /* Create the file with default content */
    FILE *f = fopen(path, "w");
    if (!f) {
        wlr_log(WLR_ERROR, "Failed to create config file %s: %s", path,
                strerror(errno));
        return false;
    }

    fputs(DEFAULT_CONFIG, f);
    fclose(f);

    wlr_log(WLR_INFO, "Created default config file: %s", path);
    return true;
}

/*
 * Skip whitespace in a string, returning pointer to first non-whitespace.
 */
static char *skip_whitespace(char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/*
 * Trim trailing whitespace in place.
 */
static void trim_trailing(char *s) {
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
}

/*
 * Parse a quoted string, handling basic escape sequences.
 * Returns a newly allocated string, or NULL on error.
 */
static char *parse_quoted_string(char **cursor) {
    char *p = *cursor;

    if (*p != '"') {
        return NULL;
    }
    p++; /* Skip opening quote */

    /* Find the closing quote, handling escapes */
    char *start = p;
    size_t len = 0;
    bool has_escapes = false;

    while (*p && *p != '"') {
        if (*p == '\\' && *(p + 1)) {
            has_escapes = true;
            p += 2; /* Skip escape sequence */
            len++;
        } else {
            p++;
            len++;
        }
    }

    if (*p != '"') {
        wlr_log(WLR_ERROR, "Unterminated string in config");
        return NULL;
    }

    /* Allocate result string */
    char *result = malloc(len + 1);
    if (!result) {
        return NULL;
    }

    /* Copy with escape handling */
    if (has_escapes) {
        char *src = start;
        char *dst = result;
        while (src < p) {
            if (*src == '\\' && *(src + 1)) {
                src++;
                switch (*src) {
                case 'n':
                    *dst++ = '\n';
                    break;
                case 't':
                    *dst++ = '\t';
                    break;
                case '\\':
                    *dst++ = '\\';
                    break;
                case '"':
                    *dst++ = '"';
                    break;
                default:
                    *dst++ = *src;
                    break;
                }
                src++;
            } else {
                *dst++ = *src++;
            }
        }
        *dst = '\0';
    } else {
        memcpy(result, start, len);
        result[len] = '\0';
    }

    *cursor = p + 1; /* Skip closing quote */
    return result;
}

/*
 * Parse the startup array from the config file.
 */
static bool parse_startup_array(FILE *f, struct infinidesk_config *config) {
    char line[MAX_LINE_LENGTH];
    int capacity = INITIAL_COMMANDS_CAPACITY;
    int count = 0;

    config->startup_commands = malloc(capacity * sizeof(char *));
    if (!config->startup_commands) {
        return false;
    }

    bool in_array = false;

    while (fgets(line, sizeof(line), f)) {
        char *p = skip_whitespace(line);

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == '#') {
            continue;
        }

        trim_trailing(p);

        /* Look for 'startup = [' */
        if (!in_array) {
            if (strncmp(p, "startup", 7) == 0) {
                p = skip_whitespace(p + 7);
                if (*p == '=') {
                    p = skip_whitespace(p + 1);
                    if (*p == '[') {
                        in_array = true;
                        p = skip_whitespace(p + 1);

                        /* Check for inline content after '[' */
                        if (*p == ']') {
                            /* Empty array */
                            break;
                        }
                        if (*p == '"') {
                            goto parse_string;
                        }
                    }
                }
            }
            continue;
        }

        /* Inside the array */
        if (*p == ']') {
            /* End of array */
            break;
        }

    parse_string:
        if (*p == '"') {
            char *cmd = parse_quoted_string(&p);
            if (cmd) {
                /* Expand array if needed */
                if (count >= capacity) {
                    capacity *= 2;
                    char **new_cmds = realloc(config->startup_commands,
                                              capacity * sizeof(char *));
                    if (!new_cmds) {
                        free(cmd);
                        return false;
                    }
                    config->startup_commands = new_cmds;
                }
                config->startup_commands[count++] = cmd;
                wlr_log(WLR_DEBUG, "Config: startup command: %s", cmd);
            }

            /* Skip comma and whitespace */
            p = skip_whitespace(p);
            if (*p == ',') {
                p = skip_whitespace(p + 1);
            }

            /* Check for more strings on same line or array end */
            if (*p == ']') {
                break;
            }
            if (*p == '"') {
                goto parse_string;
            }
        }
    }

    config->startup_command_count = count;
    return true;
}

/*
 * Parse a float value from the config line.
 */
static bool parse_float_value(const char *line, const char *key, float *value) {
    char *p = (char *)line;
    size_t key_len = strlen(key);

    if (strncmp(p, key, key_len) != 0) {
        return false;
    }

    p = skip_whitespace(p + key_len);
    if (*p != '=') {
        return false;
    }

    p = skip_whitespace(p + 1);
    char *end;
    float v = strtof(p, &end);
    if (end == p) {
        return false;
    }

    *value = v;
    return true;
}

bool config_load(struct infinidesk_config *config) {
    memset(config, 0, sizeof(*config));

    /* Set defaults */
    config->scale = 1.0f;

    char *path = get_config_path();
    if (!path) {
        return false;
    }

    /* Create config file if it doesn't exist */
    if (!ensure_config_file(path)) {
        free(path);
        return false;
    }

    /* Open and parse the config file */
    FILE *f = fopen(path, "r");
    if (!f) {
        wlr_log(WLR_ERROR, "Failed to open config file %s: %s", path,
                strerror(errno));
        free(path);
        return false;
    }

    wlr_log(WLR_INFO, "Loading config from %s", path);
    free(path);

    /* First pass: parse simple key-value pairs */
    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), f)) {
        char *p = skip_whitespace(line);

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == '#') {
            continue;
        }

        trim_trailing(p);

        /* Parse scale */
        float scale_value;
        if (parse_float_value(p, "scale", &scale_value)) {
            config->scale = scale_value;
            wlr_log(WLR_INFO, "Config: scale = %.2f", config->scale);
        }
    }

    /* Rewind and parse startup array */
    rewind(f);
    bool success = parse_startup_array(f, config);
    fclose(f);

    if (!success) {
        config_free(config);
        return false;
    }

    wlr_log(WLR_INFO, "Loaded %d startup command(s) from config",
            config->startup_command_count);
    return true;
}

void config_free(struct infinidesk_config *config) {
    if (config->startup_commands) {
        for (int i = 0; i < config->startup_command_count; i++) {
            free(config->startup_commands[i]);
        }
        free(config->startup_commands);
        config->startup_commands = NULL;
    }
    config->startup_command_count = 0;
}

void config_run_startup_commands(struct infinidesk_config *config) {
    for (int i = 0; i < config->startup_command_count; i++) {
        const char *cmd = config->startup_commands[i];
        wlr_log(WLR_INFO, "Running startup command: %s", cmd);

        pid_t pid = fork();
        if (pid == 0) {
            /* Child process */
            execl("/bin/sh", "/bin/sh", "-c", cmd, (char *)NULL);
            /* If execl returns, it failed */
            wlr_log(WLR_ERROR, "Failed to execute: %s", cmd);
            _exit(EXIT_FAILURE);
        } else if (pid < 0) {
            wlr_log(WLR_ERROR, "Failed to fork for command: %s", cmd);
        }
    }
}
