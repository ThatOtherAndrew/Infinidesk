/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * view.c - Window (view) management
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <math.h>

#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/render/pass.h>
#include <wlr/util/log.h>
#include <wlr/xwayland/xwayland.h>
#include <xcb/xproto.h>

#include "infinidesk/view.h"
#include "infinidesk/server.h"
#include "infinidesk/canvas.h"
#include "infinidesk/output.h"

/* Forward declarations for event handlers */
static void handle_map(struct wl_listener *listener, void *data);
static void handle_unmap(struct wl_listener *listener, void *data);
static void handle_destroy(struct wl_listener *listener, void *data);
static void handle_commit(struct wl_listener *listener, void *data);
static void handle_request_move(struct wl_listener *listener, void *data);
static void handle_request_resize(struct wl_listener *listener, void *data);
static void handle_request_maximise(struct wl_listener *listener, void *data);
static void handle_request_fullscreen(struct wl_listener *listener, void *data);
static void handle_set_title(struct wl_listener *listener, void *data);
static void handle_set_app_id(struct wl_listener *listener, void *data);

/* XWayland event handlers */
static void handle_xwayland_associate(struct wl_listener *listener, void *data);
static void handle_xwayland_dissociate(struct wl_listener *listener, void *data);
static void handle_xwayland_map(struct wl_listener *listener, void *data);
static void handle_xwayland_unmap(struct wl_listener *listener, void *data);
static void handle_xwayland_destroy(struct wl_listener *listener, void *data);
static void handle_xwayland_request_configure(struct wl_listener *listener, void *data);
static void handle_xwayland_request_activate(struct wl_listener *listener, void *data);
static void handle_xwayland_set_class(struct wl_listener *listener, void *data);

struct infinidesk_view *view_create(struct infinidesk_server *server,
                                    struct wlr_xdg_toplevel *xdg_toplevel)
{
    struct infinidesk_view *view = calloc(1, sizeof(*view));
    if (!view) {
        wlr_log(WLR_ERROR, "Failed to allocate view");
        return NULL;
    }

    view->server = server;
    view->type = INFINIDESK_VIEW_XDG;
    view->xdg_toplevel = xdg_toplevel;

    /* Create the scene tree for this view */
    view->scene_tree = wlr_scene_xdg_surface_create(
        server->view_tree, xdg_toplevel->base);
    if (!view->scene_tree) {
        wlr_log(WLR_ERROR, "Failed to create scene tree for view");
        free(view);
        return NULL;
    }

    /* Store a reference to the view in the scene tree */
    view->scene_tree->node.data = view;

    /* Also store in the toplevel for easy access */
    xdg_toplevel->base->data = view;

    /* Set up surface event listeners */
    view->map.notify = handle_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &view->map);

    view->unmap.notify = handle_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &view->unmap);

    view->destroy.notify = handle_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &view->destroy);

    view->commit.notify = handle_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &view->commit);

    /* Set up toplevel event listeners */
    view->request_move.notify = handle_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &view->request_move);

    view->request_resize.notify = handle_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize, &view->request_resize);

    view->request_maximise.notify = handle_request_maximise;
    wl_signal_add(&xdg_toplevel->events.request_maximize, &view->request_maximise);

    view->request_fullscreen.notify = handle_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen, &view->request_fullscreen);

    view->set_title.notify = handle_set_title;
    wl_signal_add(&xdg_toplevel->events.set_title, &view->set_title);

    view->set_app_id.notify = handle_set_app_id;
    wl_signal_add(&xdg_toplevel->events.set_app_id, &view->set_app_id);

    /* Add to the server's view list */
    wl_list_insert(&server->views, &view->link);

    wlr_log(WLR_DEBUG, "Created view %p", (void *)view);
    return view;
}

struct wlr_surface *view_get_wlr_surface(struct infinidesk_view *view) {
    switch (view->type) {
    case INFINIDESK_VIEW_XDG:
        return view->xdg_toplevel->base->surface;
    case INFINIDESK_VIEW_XWAYLAND:
        return view->xwayland_surface->surface;
    default:
        return NULL;
    }
}

struct infinidesk_view *view_create_xwayland(struct infinidesk_server *server,
                                              struct wlr_xwayland_surface *xwayland_surface)
{
    struct infinidesk_view *view = calloc(1, sizeof(*view));
    if (!view) {
        wlr_log(WLR_ERROR, "Failed to allocate view");
        return NULL;
    }

    view->server = server;
    view->type = INFINIDESK_VIEW_XWAYLAND;
    view->xwayland_surface = xwayland_surface;

    /* Store a reference to the view in the XWayland surface */
    xwayland_surface->data = view;

    /* Set up XWayland-specific event listeners */
    view->associate.notify = handle_xwayland_associate;
    wl_signal_add(&xwayland_surface->events.associate, &view->associate);

    view->dissociate.notify = handle_xwayland_dissociate;
    wl_signal_add(&xwayland_surface->events.dissociate, &view->dissociate);

    view->destroy.notify = handle_xwayland_destroy;
    wl_signal_add(&xwayland_surface->events.destroy, &view->destroy);

    view->request_configure.notify = handle_xwayland_request_configure;
    wl_signal_add(&xwayland_surface->events.request_configure, &view->request_configure);

    view->request_activate.notify = handle_xwayland_request_activate;
    wl_signal_add(&xwayland_surface->events.request_activate, &view->request_activate);

    view->request_move.notify = handle_request_move;
    wl_signal_add(&xwayland_surface->events.request_move, &view->request_move);

    view->request_resize.notify = handle_request_resize;
    wl_signal_add(&xwayland_surface->events.request_resize, &view->request_resize);

    view->request_maximise.notify = handle_request_maximise;
    wl_signal_add(&xwayland_surface->events.request_maximize, &view->request_maximise);

    view->request_fullscreen.notify = handle_request_fullscreen;
    wl_signal_add(&xwayland_surface->events.request_fullscreen, &view->request_fullscreen);

    view->set_title.notify = handle_set_title;
    wl_signal_add(&xwayland_surface->events.set_title, &view->set_title);

    view->set_class.notify = handle_xwayland_set_class;
    wl_signal_add(&xwayland_surface->events.set_class, &view->set_class);

    /* Add to the server's view list */
    wl_list_insert(&server->views, &view->link);

    wlr_log(WLR_DEBUG, "Created XWayland view %p", (void *)view);
    return view;
}

void view_destroy(struct infinidesk_view *view) {
    wlr_log(WLR_DEBUG, "Destroying view %p", (void *)view);

    wl_list_remove(&view->link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_maximise.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->set_title.link);

    if (view->type == INFINIDESK_VIEW_XDG) {
        wl_list_remove(&view->map.link);
        wl_list_remove(&view->unmap.link);
        wl_list_remove(&view->commit.link);
        wl_list_remove(&view->set_app_id.link);
    } else if (view->type == INFINIDESK_VIEW_XWAYLAND) {
        wl_list_remove(&view->associate.link);
        wl_list_remove(&view->dissociate.link);
        wl_list_remove(&view->request_configure.link);
        wl_list_remove(&view->request_activate.link);
        wl_list_remove(&view->set_class.link);

        /* Only remove map/unmap/commit if surface was associated */
        if (view->xwayland_surface->surface) {
            wl_list_remove(&view->map.link);
            wl_list_remove(&view->unmap.link);
            wl_list_remove(&view->commit.link);
        }
    }

    free(view);
}

void view_focus(struct infinidesk_view *view) {
    if (!view) {
        return;
    }

    struct infinidesk_server *server = view->server;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface *surface = view_get_wlr_surface(view);

    if (!surface) {
        /* Surface not yet available (XWayland not associated yet) */
        return;
    }

    if (prev_surface == surface) {
        /* Already focused */
        return;
    }

    /* Deactivate the previously focused surface */
    if (prev_surface) {
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev_toplevel) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
        }
        /* Also check for XWayland surfaces */
        struct wlr_xwayland_surface *prev_xwayland =
            wlr_xwayland_surface_try_from_wlr_surface(prev_surface);
        if (prev_xwayland) {
            wlr_xwayland_surface_activate(prev_xwayland, false);
        }
    }

    /* Move view to the front of the list (top of stack) */
    wl_list_remove(&view->link);
    wl_list_insert(&server->views, &view->link);

    /* Raise the scene node to the top */
    if (view->scene_tree) {
        wlr_scene_node_raise_to_top(&view->scene_tree->node);
    }

    /* Activate the toplevel */
    if (view->type == INFINIDESK_VIEW_XDG) {
        wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
    } else if (view->type == INFINIDESK_VIEW_XWAYLAND) {
        wlr_xwayland_surface_activate(view->xwayland_surface, true);
        wlr_xwayland_surface_restack(view->xwayland_surface, NULL, XCB_STACK_MODE_ABOVE);
    }

    /* Send keyboard focus */
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }

    wlr_log(WLR_DEBUG, "Focused view %p", (void *)view);
}

void view_get_geometry(struct infinidesk_view *view,
                       double *x, double *y,
                       int *width, int *height)
{
    *x = view->x;
    *y = view->y;

    if (view->type == INFINIDESK_VIEW_XDG) {
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
        *width = geo.width;
        *height = geo.height;
    } else if (view->type == INFINIDESK_VIEW_XWAYLAND) {
        *width = view->xwayland_surface->width;
        *height = view->xwayland_surface->height;
    }
}

void view_set_position(struct infinidesk_view *view, double x, double y) {
    view->x = x;
    view->y = y;
    view_update_scene_position(view);
}

void view_update_scene_position(struct infinidesk_view *view) {
    if (!view->scene_tree) {
        /* Scene tree not created yet (XWayland not associated) */
        return;
    }

    struct infinidesk_canvas *canvas = &view->server->canvas;

    /* Convert canvas coordinates to screen coordinates */
    double screen_x, screen_y;
    canvas_to_screen(canvas, view->x, view->y, &screen_x, &screen_y);

    if (view->type == INFINIDESK_VIEW_XDG) {
        /* Account for the XDG surface geometry offset (for CSD windows) */
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);

        /* Set the scene node position (integer screen coordinates) */
        wlr_scene_node_set_position(&view->scene_tree->node,
            (int)round(screen_x) - geo.x,
            (int)round(screen_y) - geo.y);
    } else if (view->type == INFINIDESK_VIEW_XWAYLAND) {
        /* XWayland surfaces don't have geometry offset */
        wlr_scene_node_set_position(&view->scene_tree->node,
            (int)round(screen_x), (int)round(screen_y));

        /* Also configure the XWayland surface */
        wlr_xwayland_surface_configure(view->xwayland_surface,
            (int16_t)round(screen_x), (int16_t)round(screen_y),
            view->xwayland_surface->width, view->xwayland_surface->height);
    }
}

void view_move_begin(struct infinidesk_view *view,
                     double cursor_x, double cursor_y)
{
    view->is_moving = true;
    view->grab_x = cursor_x;
    view->grab_y = cursor_y;
    view->grab_view_x = view->x;
    view->grab_view_y = view->y;

    wlr_log(WLR_DEBUG, "View move started at (%.1f, %.1f)", cursor_x, cursor_y);
}

void view_move_update(struct infinidesk_view *view,
                      double cursor_x, double cursor_y)
{
    if (!view->is_moving) {
        return;
    }

    /* Calculate cursor delta in canvas space */
    double delta_x = cursor_x - view->grab_x;
    double delta_y = cursor_y - view->grab_y;

    /* Move view by the delta */
    view->x = view->grab_view_x + delta_x;
    view->y = view->grab_view_y + delta_y;

    /* Update scene position */
    view_update_scene_position(view);
}

void view_move_end(struct infinidesk_view *view) {
    if (view->is_moving) {
        wlr_log(WLR_DEBUG, "View move ended at (%.1f, %.1f)", view->x, view->y);
    }
    view->is_moving = false;
}

void view_close(struct infinidesk_view *view) {
    if (view->type == INFINIDESK_VIEW_XDG) {
        wlr_xdg_toplevel_send_close(view->xdg_toplevel);
    } else if (view->type == INFINIDESK_VIEW_XWAYLAND) {
        wlr_xwayland_surface_close(view->xwayland_surface);
    }
}

/* Event handlers */

static void handle_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, map);
    struct infinidesk_server *server = view->server;

    wlr_log(WLR_DEBUG, "View %p mapped", (void *)view);

    /* Position the window at the centre of the current viewport */
    struct infinidesk_output *output = output_get_primary(server);
    if (output) {
        int output_width, output_height;
        output_get_effective_resolution(output, &output_width, &output_height);

        /* Get the centre of the viewport in canvas coordinates */
        double centre_x, centre_y;
        canvas_get_viewport_centre(&server->canvas,
                                   output_width, output_height,
                                   &centre_x, &centre_y);

        /* Get the window size */
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);

        /* Position window so its centre is at the viewport centre */
        view->x = centre_x - geo.width / 2.0;
        view->y = centre_y - geo.height / 2.0;

        wlr_log(WLR_DEBUG, "Positioned view at (%.1f, %.1f)", view->x, view->y);
    } else {
        /* Fallback: position at canvas origin */
        view->x = 0;
        view->y = 0;
    }

    /* Update scene position */
    view_update_scene_position(view);

    /* Focus the new window */
    view_focus(view);
}

static void handle_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, unmap);

    wlr_log(WLR_DEBUG, "View %p unmapped", (void *)view);

    /* If this view was being moved, end the move */
    if (view->is_moving) {
        view_move_end(view);
    }

    /* Clear cursor grab if this view was grabbed */
    if (view->server->grabbed_view == view) {
        view->server->grabbed_view = NULL;
        view->server->cursor_mode = INFINIDESK_CURSOR_PASSTHROUGH;
    }
}

static void handle_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, destroy);

    wlr_log(WLR_DEBUG, "View %p destroyed", (void *)view);

    view_destroy(view);
}

static void handle_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, commit);

    /* Update scene position in case geometry changed */
    if (view->xdg_toplevel->base->initial_commit) {
        /* Schedule configure for initial commit */
        wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
    }
}

static void handle_request_move(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, request_move);

    /* Client requested interactive move - we handle this via Super+drag instead */
    wlr_log(WLR_DEBUG, "View %p requested move (use Super+drag)", (void *)view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, request_resize);

    /* Client requested interactive resize - not yet implemented */
    wlr_log(WLR_DEBUG, "View %p requested resize (not implemented)", (void *)view);
}

static void handle_request_maximise(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, request_maximise);

    /* For an infinite canvas, maximise doesn't quite make sense in the
     * traditional way. For now, we just acknowledge the request. */
    wlr_log(WLR_DEBUG, "View %p requested maximise (not implemented)", (void *)view);
    wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, request_fullscreen);

    /* Fullscreen could be implemented to zoom to fill the viewport */
    wlr_log(WLR_DEBUG, "View %p requested fullscreen (not implemented)", (void *)view);
    wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void handle_set_title(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, set_title);

    wlr_log(WLR_DEBUG, "View %p title: %s",
            (void *)view, view->xdg_toplevel->title ?: "(null)");
}

static void handle_set_app_id(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, set_app_id);

    wlr_log(WLR_DEBUG, "View %p app_id: %s",
            (void *)view, view->xdg_toplevel->app_id ?: "(null)");
}

/* XWayland event handlers */

static void handle_xwayland_associate(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, associate);
    struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;

    wlr_log(WLR_DEBUG, "XWayland surface %p associated", (void *)xwayland_surface);

    /* Now that we have a wlr_surface, create the scene tree */
    view->scene_tree = wlr_scene_subsurface_tree_create(
        view->server->view_tree, xwayland_surface->surface);
    if (!view->scene_tree) {
        wlr_log(WLR_ERROR, "Failed to create scene tree for XWayland view");
        return;
    }

    /* Store a reference to the view in the scene tree */
    view->scene_tree->node.data = view;

    /* Set up surface event listeners now that we have a surface */
    view->map.notify = handle_xwayland_map;
    wl_signal_add(&xwayland_surface->surface->events.map, &view->map);

    view->unmap.notify = handle_xwayland_unmap;
    wl_signal_add(&xwayland_surface->surface->events.unmap, &view->unmap);

    view->commit.notify = handle_commit;
    wl_signal_add(&xwayland_surface->surface->events.commit, &view->commit);
}

static void handle_xwayland_dissociate(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, dissociate);

    wlr_log(WLR_DEBUG, "XWayland surface %p dissociated", (void *)view->xwayland_surface);

    /* Remove surface event listeners */
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
}

static void handle_xwayland_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, map);
    struct infinidesk_server *server = view->server;

    wlr_log(WLR_DEBUG, "XWayland view %p mapped", (void *)view);

    /* Position the window at the centre of the current viewport */
    struct infinidesk_output *output = output_get_primary(server);
    if (output) {
        int output_width, output_height;
        output_get_effective_resolution(output, &output_width, &output_height);

        /* Get the centre of the viewport in canvas coordinates */
        double centre_x, centre_y;
        canvas_get_viewport_centre(&server->canvas,
                                   output_width, output_height,
                                   &centre_x, &centre_y);

        /* Get the window size */
        int width = view->xwayland_surface->width;
        int height = view->xwayland_surface->height;

        /* Position window so its centre is at the viewport centre */
        view->x = centre_x - width / 2.0;
        view->y = centre_y - height / 2.0;

        wlr_log(WLR_DEBUG, "Positioned XWayland view at (%.1f, %.1f)", view->x, view->y);
    } else {
        /* Fallback: position at canvas origin */
        view->x = 0;
        view->y = 0;
    }

    /* Update scene position */
    view_update_scene_position(view);

    /* Focus the new window */
    view_focus(view);
}

static void handle_xwayland_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, unmap);

    wlr_log(WLR_DEBUG, "XWayland view %p unmapped", (void *)view);

    /* If this view was being moved, end the move */
    if (view->is_moving) {
        view_move_end(view);
    }

    /* Clear cursor grab if this view was grabbed */
    if (view->server->grabbed_view == view) {
        view->server->grabbed_view = NULL;
        view->server->cursor_mode = INFINIDESK_CURSOR_PASSTHROUGH;
    }
}

static void handle_xwayland_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, destroy);

    wlr_log(WLR_DEBUG, "XWayland view %p destroyed", (void *)view);

    view_destroy(view);
}

static void handle_xwayland_request_configure(struct wl_listener *listener, void *data) {
    struct infinidesk_view *view = wl_container_of(listener, view, request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;

    wlr_log(WLR_DEBUG, "XWayland view %p requested configure: %dx%d+%d+%d",
            (void *)view, event->width, event->height, event->x, event->y);

    /* Acknowledge the configure request */
    wlr_xwayland_surface_configure(view->xwayland_surface,
        event->x, event->y, event->width, event->height);
}

static void handle_xwayland_request_activate(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, request_activate);

    wlr_log(WLR_DEBUG, "XWayland view %p requested activate", (void *)view);

    /* Focus the view */
    view_focus(view);
}

static void handle_xwayland_set_class(struct wl_listener *listener, void *data) {
    (void)data;
    struct infinidesk_view *view = wl_container_of(listener, view, set_class);

    wlr_log(WLR_DEBUG, "XWayland view %p class: %s",
            (void *)view, view->xwayland_surface->class ?: "(null)");
}

/*
 * Surface iterator data for rendering.
 */
struct render_data {
    struct wlr_render_pass *pass;
    struct infinidesk_view *view;
    double scale;
    int base_x;
    int base_y;
};

/*
 * Render a single surface (called for each surface in the tree).
 */
static void render_surface_iterator(struct wlr_surface *surface,
                                    int sx, int sy, void *user_data)
{
    struct render_data *data = user_data;

    /* Get the texture for this surface */
    struct wlr_texture *texture = wlr_surface_get_texture(surface);
    if (!texture) {
        return;
    }

    /*
     * Surface dimensions in logical coordinates.
     * The texture may be larger if client uses buffer scale > 1.
     */
    int logical_width = surface->current.width;
    int logical_height = surface->current.height;
    int buffer_scale = surface->current.scale;

    /* Skip surfaces with no size */
    if (logical_width <= 0 || logical_height <= 0) {
        return;
    }

    /* Sanity check buffer scale */
    if (buffer_scale <= 0) {
        buffer_scale = 1;
    }

    /* Calculate destination position */
    int dst_x = data->base_x + (int)round(sx * data->scale);
    int dst_y = data->base_y + (int)round(sy * data->scale);

    /* Calculate scaled destination size */
    int dst_width = (int)round(logical_width * data->scale);
    int dst_height = (int)round(logical_height * data->scale);

    /* Skip if destination has no size */
    if (dst_width <= 0 || dst_height <= 0) {
        return;
    }

    /*
     * Set up source box to use the full texture.
     * The texture dimensions are logical_size * buffer_scale.
     */
    struct wlr_fbox src_box = {
        .x = 0,
        .y = 0,
        .width = logical_width * buffer_scale,
        .height = logical_height * buffer_scale,
    };

    /*
     * Choose filter mode:
     * - At scale 1.0 with buffer_scale 1: no filtering needed (pixel-perfect)
     * - Otherwise: use bilinear for smooth scaling
     */
    enum wlr_scale_filter_mode filter = WLR_SCALE_FILTER_BILINEAR;
    if (data->scale == 1.0 && buffer_scale == 1) {
        filter = WLR_SCALE_FILTER_NEAREST;
    }

    wlr_render_pass_add_texture(data->pass, &(struct wlr_render_texture_options){
        .texture = texture,
        .src_box = src_box,
        .dst_box = {
            .x = dst_x,
            .y = dst_y,
            .width = dst_width,
            .height = dst_height,
        },
        .filter_mode = filter,
        .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
    });
}

void view_render(struct infinidesk_view *view, struct wlr_render_pass *pass) {
    struct infinidesk_canvas *canvas = &view->server->canvas;
    struct wlr_xdg_surface *xdg_surface = view->xdg_toplevel->base;

    if (!xdg_surface->surface->mapped) {
        return;
    }

    /* Convert canvas coordinates to screen coordinates */
    double screen_x, screen_y;
    canvas_to_screen(canvas, view->x, view->y, &screen_x, &screen_y);

    /* Account for XDG surface geometry offset (for CSD windows) */
    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(xdg_surface, &geo);

    /* Set up render data */
    struct render_data data = {
        .pass = pass,
        .view = view,
        .scale = canvas->scale,
        .base_x = (int)round(screen_x) - (int)round(geo.x * canvas->scale),
        .base_y = (int)round(screen_y) - (int)round(geo.y * canvas->scale),
    };

    /* Render all surfaces in the XDG surface tree (includes subsurfaces) */
    wlr_xdg_surface_for_each_surface(xdg_surface, render_surface_iterator, &data);
}
