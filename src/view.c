/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * view.c - Window (view) management
 */

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdlib.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/render/pass.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "infinidesk/canvas.h"
#include "infinidesk/output.h"
#include "infinidesk/server.h"
#include "infinidesk/view.h"

/* Window decoration constants */
#define BORDER_WIDTH 3
#define CORNER_RADIUS 10

/* Border colours (RGBA) */
#define BORDER_FOCUSED_R 0.4f
#define BORDER_FOCUSED_G 0.6f
#define BORDER_FOCUSED_B 0.9f
#define BORDER_FOCUSED_A 1.0f

#define BORDER_UNFOCUSED_R 0.3f
#define BORDER_UNFOCUSED_G 0.3f
#define BORDER_UNFOCUSED_B 0.35f
#define BORDER_UNFOCUSED_A 1.0f

/* Background colour for corner masking */
#define BG_COLOUR_R 0.18f
#define BG_COLOUR_G 0.18f
#define BG_COLOUR_B 0.18f
#define BG_COLOUR_A 1.0f

/* Map/unmap animation scale (windows animate from/to this scale) */
#define MAP_ANIM_SCALE_START 0.9

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

struct infinidesk_view *view_create(struct infinidesk_server *server,
                                    struct wlr_xdg_toplevel *xdg_toplevel) {
  struct infinidesk_view *view = calloc(1, sizeof(*view));
  if (!view) {
    wlr_log(WLR_ERROR, "Failed to allocate view");
    return NULL;
  }

  view->server = server;
  view->xdg_toplevel = xdg_toplevel;
  view->id = server->next_view_id++;

  /* Create the scene tree for this view */
  view->scene_tree =
      wlr_scene_xdg_surface_create(server->view_tree, xdg_toplevel->base);
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
  wl_signal_add(&xdg_toplevel->events.request_maximize,
                &view->request_maximise);

  view->request_fullscreen.notify = handle_request_fullscreen;
  wl_signal_add(&xdg_toplevel->events.request_fullscreen,
                &view->request_fullscreen);

  view->set_title.notify = handle_set_title;
  wl_signal_add(&xdg_toplevel->events.set_title, &view->set_title);

  view->set_app_id.notify = handle_set_app_id;
  wl_signal_add(&xdg_toplevel->events.set_app_id, &view->set_app_id);

  /* Initialise focus animation state */
  view->focused = false;
  view->focus_animation = 0.0;
  view->focus_anim_start_ms = 0;
  view->focus_anim_active = false;

  /* Initialise map/unmap animation state */
  view->map_animation = 0.0;
  view->map_anim_start_ms = 0;
  view->is_animating_out = false;

  /* Add to the server's view list */
  wl_list_insert(&server->views, &view->link);

  wlr_log(WLR_DEBUG, "Created view %p", (void *)view);
  return view;
}

void view_destroy(struct infinidesk_view *view) {
  wlr_log(WLR_DEBUG, "Destroying view %p", (void *)view);

  wl_list_remove(&view->link);

  wl_list_remove(&view->map.link);
  wl_list_remove(&view->unmap.link);
  wl_list_remove(&view->destroy.link);
  wl_list_remove(&view->commit.link);
  wl_list_remove(&view->request_move.link);
  wl_list_remove(&view->request_resize.link);
  wl_list_remove(&view->request_maximise.link);
  wl_list_remove(&view->request_fullscreen.link);
  wl_list_remove(&view->set_title.link);
  wl_list_remove(&view->set_app_id.link);

  free(view);
}

/* Helper to get current time in milliseconds */
static uint32_t get_time_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void view_focus(struct infinidesk_view *view) {
  if (!view) {
    return;
  }

  struct infinidesk_server *server = view->server;
  struct wlr_seat *seat = server->seat;
  struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
  struct wlr_surface *surface = view->xdg_toplevel->base->surface;

  if (prev_surface == surface) {
    /* Already focused */
    return;
  }

  /* Deactivate the previously focused surface and start unfocus animation */
  if (prev_surface) {
    struct wlr_xdg_toplevel *prev_toplevel =
        wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
    if (prev_toplevel && prev_toplevel->base->data) {
      struct infinidesk_view *prev_view = prev_toplevel->base->data;
      prev_view->focused = false;
      prev_view->focus_anim_start_ms = get_time_ms();
      prev_view->focus_anim_active = true;
      wlr_xdg_toplevel_set_activated(prev_toplevel, false);
    }
  }

  /* Activate the toplevel and start focus animation */
  wlr_xdg_toplevel_set_activated(view->xdg_toplevel, true);
  view->focused = true;
  view->focus_anim_start_ms = get_time_ms();
  view->focus_anim_active = true;

  /* Send keyboard focus */
  struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
  if (keyboard) {
    wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
                                   keyboard->num_keycodes,
                                   &keyboard->modifiers);
  }

  wlr_log(WLR_DEBUG, "Focused view %p", (void *)view);
}

void view_raise(struct infinidesk_view *view) {
  if (!view) {
    return;
  }

  struct infinidesk_server *server = view->server;

  /* Move view to the front of the list (top of stack) */
  wl_list_remove(&view->link);
  wl_list_insert(&server->views, &view->link);

  /* Raise the scene node to the top */
  wlr_scene_node_raise_to_top(&view->scene_tree->node);

  wlr_log(WLR_DEBUG, "Raised view %p", (void *)view);
}

void view_get_geometry(struct infinidesk_view *view, double *x, double *y,
                       int *width, int *height) {
  *x = view->x;
  *y = view->y;

  struct wlr_box geo;
  wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
  *width = geo.width;
  *height = geo.height;
}

void view_set_position(struct infinidesk_view *view, double x, double y) {
  view->x = x;
  view->y = y;
  view_update_scene_position(view);
}

void view_update_scene_position(struct infinidesk_view *view) {
  struct infinidesk_canvas *canvas = &view->server->canvas;

  /* Convert canvas coordinates to screen coordinates */
  double screen_x, screen_y;
  canvas_to_screen(canvas, view->x, view->y, &screen_x, &screen_y);

  /* Account for the XDG surface geometry offset (for CSD windows) */
  struct wlr_box geo;
  wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);

  /* Set the scene node position (integer screen coordinates) */
  wlr_scene_node_set_position(&view->scene_tree->node,
                              (int)round(screen_x) - geo.x,
                              (int)round(screen_y) - geo.y);

  /*
   * Note: wlroots scene graph doesn't support arbitrary scaling of scene trees.
   * For true visual zoom, we would need to either:
   * 1. Use custom rendering with wlr_renderer transforms
   * 2. Request clients to resize (semantic zoom)
   * 3. Use a different compositor architecture
   *
   * For now, windows maintain their native size and only positions scale.
   * This is similar to how some canvas apps handle extreme zoom levels.
   */
}

void view_move_begin(struct infinidesk_view *view, double cursor_x,
                     double cursor_y) {
  view->is_moving = true;
  view->grab_x = cursor_x;
  view->grab_y = cursor_y;
  view->grab_view_x = view->x;
  view->grab_view_y = view->y;

  wlr_log(WLR_DEBUG, "View move started at (%.1f, %.1f)", cursor_x, cursor_y);
}

void view_move_update(struct infinidesk_view *view, double cursor_x,
                      double cursor_y) {
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
  wlr_xdg_toplevel_send_close(view->xdg_toplevel);
}

void view_snap(struct infinidesk_canvas *canvas, struct infinidesk_view *view, int output_width, int output_height) {
  /* Get view dimensions */
  struct wlr_box geo;
  wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);

  /* Calculate view center in canvas coordinates */
  double view_center_x = view->x + geo.width / 2.0;
  double view_center_y = view->y + geo.height / 2.0;

  /* Store current position as animation start */
  canvas->snap_start_x = canvas->viewport_x;
  canvas->snap_start_y = canvas->viewport_y;

  /* Calculate target viewport position (view center at screen center) */
  canvas->snap_target_x = view_center_x - (output_width / 2.0) / canvas->scale;
  canvas->snap_target_y = view_center_y - (output_height / 2.0) / canvas->scale;

  /* Start animation */
  canvas->snap_anim_start_ms = get_time_ms();
  canvas->snap_anim_active = true;

  view_focus(view);
  view_raise(view);
}

void views_gather(struct infinidesk_server *server, double minimum_gap) {
    if (wl_list_empty(&server->views)) {
        return;
    }

    /* Get viewport center */
    struct infinidesk_output *output = output_get_primary(server);
    if (!output) {
        return;
    }

    int screen_width, screen_height;
    output_get_effective_resolution(output, &screen_width, &screen_height);

    double viewport_center_x, viewport_center_y;
    canvas_get_viewport_centre(&server->canvas, screen_width, screen_height,
                               &viewport_center_x, &viewport_center_y);

    /* First, calculate initial centroid */
    int count = 0;
    double centroid_x = 0.0, centroid_y = 0.0;
    struct infinidesk_view *view;

    /* Go through the list of views, and add the centre points of each view */
    wl_list_for_each(view, &server->views, link) {
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
        centroid_x += view->x + geo.width / 2.0;
        centroid_y += view->y + geo.height / 2.0;
        count++;
    }

    /* Return if there are no views */
    if (count == 0) {
        return;
    }

    /* Divide the summed centres by the number of views to get the total centre */
    centroid_x /= count;
    centroid_y /= count;

    /* Scale factor to bring views closer to the centroid (0.5 = halfway) */
    double scale_factor = 0.5;

    /* Move each view closer to the centroid by scaling its vector from the centroid */
    wl_list_for_each(view, &server->views, link) {
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);

        /* Calculate current view centre */
        double view_center_x = view->x + geo.width / 2.0;
        double view_center_y = view->y + geo.height / 2.0;

        /* Calculate vector from centroid to view centre */
        double vec_x = view_center_x - centroid_x;
        double vec_y = view_center_y - centroid_y;

        /* Calculate current distance from centroid */
        double current_distance = sqrt(vec_x * vec_x + vec_y * vec_y);

        /*
         * Calculate minimum allowed distance based on view's bounding box.
         * Use half the diagonal of the bounding box plus the minimum gap.
         * This ensures the view's edge doesn't get closer than minimum_gap to the centroid.
         */
        double half_width = geo.width / 2.0;
        double half_height = geo.height / 2.0;

        /*
         * For a more accurate minimum distance, calculate how far the edge of the
         * bounding box is from the centre along the direction of the vector.
         * This accounts for the view's aspect ratio and approach angle.
         */
        double min_distance;
        if (current_distance < 0.001) {
            /* View is already at centroid, no need to move */
            min_distance = 0.0;
        } else {
            /* Normalise the vector to get direction */
            double dir_x = vec_x / current_distance;
            double dir_y = vec_y / current_distance;

            /*
             * Calculate intersection of the direction ray with the bounding box.
             * The distance from centre to edge along direction (dir_x, dir_y) is:
             * min(half_width / |dir_x|, half_height / |dir_y|) but we need to
             * handle the case where dir_x or dir_y is zero.
             */
            double t_x = (fabs(dir_x) > 0.001) ? half_width / fabs(dir_x) : INFINITY;
            double t_y = (fabs(dir_y) > 0.001) ? half_height / fabs(dir_y) : INFINITY;
            double edge_distance = fmin(t_x, t_y);

            /* Minimum distance is edge distance plus the gap */
            min_distance = edge_distance + minimum_gap;
        }

        /* Calculate the new distance after scaling */
        double new_distance = current_distance * scale_factor;

        /* Clamp the new distance to not go below minimum */
        if (new_distance < min_distance) {
            new_distance = min_distance;
        }

        /* Calculate scale factor for this view (may differ from global scale_factor) */
        double effective_scale = (current_distance > 0.001) ? new_distance / current_distance : 1.0;

        /* Scale the vector to get new position */
        double new_center_x = centroid_x + vec_x * effective_scale;
        double new_center_y = centroid_y + vec_y * effective_scale;

        /* Set new view position (converting back from centre to top-left) */
        view->x = new_center_x - geo.width / 2.0;
        view->y = new_center_y - geo.height / 2.0;

        /* Update scene position */
        view_update_scene_position(view);
    }

    /*
     * Recalculate the centroid after gathering (it may have shifted slightly
     * due to minimum distance clamping).
     */
    double new_centroid_x = 0.0, new_centroid_y = 0.0;
    wl_list_for_each(view, &server->views, link) {
        struct wlr_box geo;
        wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);
        new_centroid_x += view->x + geo.width / 2.0;
        new_centroid_y += view->y + geo.height / 2.0;
    }
    new_centroid_x /= count;
    new_centroid_y /= count;

    /*
     * Animate the viewport to centre on the new centroid.
     * Use the effective resolution which already accounts for HiDPI scaling
     * (it returns logical pixels, not physical pixels).
     */
    struct infinidesk_canvas *canvas = &server->canvas;

    /* Store current position as animation start */
    canvas->snap_start_x = canvas->viewport_x;
    canvas->snap_start_y = canvas->viewport_y;

    /*
     * Calculate target viewport position so the centroid is at screen centre.
     * screen_centre = (centroid - viewport) * canvas->scale
     * We want: screen_width/2 = (centroid_x - viewport_x) * scale
     * Therefore: viewport_x = centroid_x - (screen_width/2) / scale
     */
    canvas->snap_target_x = new_centroid_x - (screen_width / 2.0) / canvas->scale;
    canvas->snap_target_y = new_centroid_y - (screen_height / 2.0) / canvas->scale;

    /* Start the snap animation */
    canvas->snap_anim_start_ms = get_time_ms();
    canvas->snap_anim_active = true;

    wlr_log(WLR_DEBUG, "Gathered %d views towards centroid (%.1f, %.1f) with min gap %.1f, "
            "panning to (%.1f, %.1f)",
            count, new_centroid_x, new_centroid_y, minimum_gap,
            canvas->snap_target_x, canvas->snap_target_y);
}

/* Event handlers */

static void handle_map(struct wl_listener *listener, void *data) {
  (void)data;
  struct infinidesk_view *view = wl_container_of(listener, view, map);
  struct infinidesk_server *server = view->server;

  wlr_log(WLR_DEBUG, "View %p mapped", (void *)view);

  /*
   * Position the window at the centre of the usable area.
   * The usable area accounts for exclusive zones claimed by layer surfaces
   * (e.g., panels, docks).
   */
  struct infinidesk_output *output = output_get_primary(server);
  if (output) {
    /* Use the usable area which respects layer shell exclusive zones */
    struct wlr_box usable = output->usable_area;

    /*
     * Calculate the centre of the usable area in screen coordinates,
     * then convert to canvas coordinates for window placement.
     */
    double screen_centre_x = usable.x + usable.width / 2.0;
    double screen_centre_y = usable.y + usable.height / 2.0;

    /* Convert screen coordinates to canvas coordinates */
    double canvas_centre_x, canvas_centre_y;
    screen_to_canvas(&server->canvas, screen_centre_x, screen_centre_y,
                     &canvas_centre_x, &canvas_centre_y);

    /* Get the window size */
    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);

    /* Position window so its centre is at the usable area centre */
    view->x = canvas_centre_x - geo.width / 2.0;
    view->y = canvas_centre_y - geo.height / 2.0;

    wlr_log(WLR_DEBUG,
            "Positioned view at (%.1f, %.1f) in usable area (%d,%d %dx%d)",
            view->x, view->y, usable.x, usable.y, usable.width, usable.height);
  } else {
    /* Fallback: position at canvas origin */
    view->x = 0;
    view->y = 0;
  }

  /* Update scene position */
  view_update_scene_position(view);

  /* Start entrance animation */
  view->map_animation = 0.0;
  view->map_anim_start_ms = get_time_ms();
  view->is_animating_out = false;

  /* Focus and raise the new window */
  view_focus(view);
  view_raise(view);
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

  /*
   * Reset map animation state.
   * Note: Exit animations would require caching the last rendered texture
   * before the surface unmaps, which is more complex to implement.
   * For now, windows disappear immediately on unmap.
   */
  view->map_animation = 0.0;
  view->is_animating_out = false;
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

  if (view->xdg_toplevel->base->initial_commit) {
    /* Schedule configure for initial commit */
    wlr_xdg_toplevel_set_size(view->xdg_toplevel, 0, 0);
  }

  /*
   * Update scene position when geometry changes.
   * CSD windows (like Chrome/Firefox) may report their shadow offset
   * after the initial commit, so we need to adjust when it changes.
   */
  if (view->xdg_toplevel->base->surface->mapped) {
    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(view->xdg_toplevel->base, &geo);

    if (geo.x != view->last_geo_x || geo.y != view->last_geo_y) {
      view->last_geo_x = geo.x;
      view->last_geo_y = geo.y;
      view_update_scene_position(view);
    }
  }
}

static void handle_request_move(struct wl_listener *listener, void *data) {
  (void)data;
  struct infinidesk_view *view = wl_container_of(listener, view, request_move);

  /* Client requested interactive move - we handle this via Super+drag instead
   */
  wlr_log(WLR_DEBUG, "View %p requested move (use Super+drag)", (void *)view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
  (void)data;
  struct infinidesk_view *view =
      wl_container_of(listener, view, request_resize);

  /* Client requested interactive resize - not yet implemented */
  wlr_log(WLR_DEBUG, "View %p requested resize (not implemented)",
          (void *)view);
}

static void handle_request_maximise(struct wl_listener *listener, void *data) {
  (void)data;
  struct infinidesk_view *view =
      wl_container_of(listener, view, request_maximise);

  /* For an infinite canvas, maximise doesn't quite make sense in the
   * traditional way. For now, we just acknowledge the request. */
  wlr_log(WLR_DEBUG, "View %p requested maximise (not implemented)",
          (void *)view);
  wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void handle_request_fullscreen(struct wl_listener *listener,
                                      void *data) {
  (void)data;
  struct infinidesk_view *view =
      wl_container_of(listener, view, request_fullscreen);

  /* Fullscreen could be implemented to zoom to fill the viewport */
  wlr_log(WLR_DEBUG, "View %p requested fullscreen (not implemented)",
          (void *)view);
  wlr_xdg_surface_schedule_configure(view->xdg_toplevel->base);
}

static void handle_set_title(struct wl_listener *listener, void *data) {
  (void)data;
  struct infinidesk_view *view = wl_container_of(listener, view, set_title);

  wlr_log(WLR_DEBUG, "View %p title: %s", (void *)view,
          view->xdg_toplevel->title ?: "(null)");
}

static void handle_set_app_id(struct wl_listener *listener, void *data) {
  (void)data;
  struct infinidesk_view *view = wl_container_of(listener, view, set_app_id);

  wlr_log(WLR_DEBUG, "View %p app_id: %s", (void *)view,
          view->xdg_toplevel->app_id ?: "(null)");
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
  int geo_x; /* Geometry offset to subtract from sx/sy */
  int geo_y;
  float opacity; /* Overall opacity for map/unmap animation */
};

/*
 * Render a single surface (called for each surface in the tree).
 */
static void render_surface_iterator(struct wlr_surface *surface, int sx, int sy,
                                    void *user_data) {
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

  /*
   * Calculate destination position.
   * Subtract the geometry offset because sx/sy from
   * wlr_xdg_surface_for_each_surface are relative to the buffer origin, but we
   * want positions relative to the window content origin (which is offset by
   * geo.x/geo.y for CSD windows).
   */
  int dst_x = data->base_x + (int)round((sx - data->geo_x) * data->scale);
  int dst_y = data->base_y + (int)round((sy - data->geo_y) * data->scale);

  /* Calculate scaled destination size */
  int dst_width = (int)round(logical_width * data->scale);
  int dst_height = (int)round(logical_height * data->scale);

  /* Skip if destination has no size */
  if (dst_width <= 0 || dst_height <= 0) {
    return;
  }

  /*
   * Get the source box from the surface.
   * This accounts for viewporter cropping - when a client uses wp_viewport
   * to set a source rectangle, we must use that instead of the full texture.
   */
  struct wlr_fbox src_box;
  wlr_surface_get_buffer_source_box(surface, &src_box);

  /*
   * Choose filter mode:
   * - At scale 1.0 with buffer_scale 1: no filtering needed (pixel-perfect)
   * - Otherwise: use bilinear for smooth scaling
   */
  enum wlr_scale_filter_mode filter = WLR_SCALE_FILTER_BILINEAR;
  if (data->scale == 1.0 && buffer_scale == 1) {
    filter = WLR_SCALE_FILTER_NEAREST;
  }

  wlr_render_pass_add_texture(
      data->pass, &(struct wlr_render_texture_options){
                      .texture = texture,
                      .src_box = src_box,
                      .dst_box =
                          {
                              .x = dst_x,
                              .y = dst_y,
                              .width = dst_width,
                              .height = dst_height,
                          },
                      .alpha = &data->opacity,
                      .filter_mode = filter,
                      .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
                  });
}

/*
 * Cubic ease-out function: f(t) = 1 - (1 - t)^3
 * Starts fast, decelerates towards the end.
 */
static double ease_out_cubic(double t) {
  double inv = 1.0 - t;
  return 1.0 - (inv * inv * inv);
}

/*
 * Linear interpolation between two values.
 */
static float lerp(float a, float b, float t) { return a + (b - a) * t; }

/*
 * Linear interpolation for doubles.
 */
static double lerp_d(double a, double b, double t) { return a + (b - a) * t; }

/*
 * Render the window border with rounded corners.
 */
static void render_border(struct wlr_render_pass *pass, int x, int y, int width,
                          int height, int border_width, int corner_radius,
                          float r, float g, float b, float a) {
  /* Skip rendering if dimensions are too small */
  if (width <= 0 || height <= 0 || border_width <= 0) {
    return;
  }

  struct wlr_render_color colour = {.r = r, .g = g, .b = b, .a = a};

  /* Ensure corner radius doesn't exceed half the smallest dimension */
  int max_radius = (width < height ? width : height) / 2;
  if (corner_radius > max_radius) {
    corner_radius = max_radius;
  }
  if (corner_radius < 0) {
    corner_radius = 0;
  }

  /* If no corner radius, just draw simple rectangles */
  if (corner_radius == 0) {
    /* Top */
    wlr_render_pass_add_rect(
        pass,
        &(struct wlr_render_rect_options){
            .box = {.x = x, .y = y, .width = width, .height = border_width},
            .color = colour,
        });
    /* Bottom */
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box = {.x = x,
                                               .y = y + height - border_width,
                                               .width = width,
                                               .height = border_width},
                                       .color = colour,
                                   });
    /* Left */
    wlr_render_pass_add_rect(pass,
                             &(struct wlr_render_rect_options){
                                 .box = {.x = x,
                                         .y = y + border_width,
                                         .width = border_width,
                                         .height = height - 2 * border_width},
                                 .color = colour,
                             });
    /* Right */
    wlr_render_pass_add_rect(pass,
                             &(struct wlr_render_rect_options){
                                 .box = {.x = x + width - border_width,
                                         .y = y + border_width,
                                         .width = border_width,
                                         .height = height - 2 * border_width},
                                 .color = colour,
                             });
    return;
  }

  /* Top edge (between corners) */
  if (width > 2 * corner_radius) {
    wlr_render_pass_add_rect(pass,
                             &(struct wlr_render_rect_options){
                                 .box =
                                     {
                                         .x = x + corner_radius,
                                         .y = y,
                                         .width = width - 2 * corner_radius,
                                         .height = border_width,
                                     },
                                 .color = colour,
                             });
  }

  /* Bottom edge (between corners) */
  if (width > 2 * corner_radius) {
    wlr_render_pass_add_rect(pass,
                             &(struct wlr_render_rect_options){
                                 .box =
                                     {
                                         .x = x + corner_radius,
                                         .y = y + height - border_width,
                                         .width = width - 2 * corner_radius,
                                         .height = border_width,
                                     },
                                 .color = colour,
                             });
  }

  /* Left edge (between corners) */
  if (height > 2 * corner_radius) {
    wlr_render_pass_add_rect(pass,
                             &(struct wlr_render_rect_options){
                                 .box =
                                     {
                                         .x = x,
                                         .y = y + corner_radius,
                                         .width = border_width,
                                         .height = height - 2 * corner_radius,
                                     },
                                 .color = colour,
                             });
  }

  /* Right edge (between corners) */
  if (height > 2 * corner_radius) {
    wlr_render_pass_add_rect(pass,
                             &(struct wlr_render_rect_options){
                                 .box =
                                     {
                                         .x = x + width - border_width,
                                         .y = y + corner_radius,
                                         .width = border_width,
                                         .height = height - 2 * corner_radius,
                                     },
                                 .color = colour,
                             });
  }

  /*
   * Render rounded corners using small rectangles to approximate arcs.
   * For each row in the corner region, we draw a horizontal line segment
   * that forms part of the rounded border.
   */
  double outer_r = (double)corner_radius;
  double inner_r = (double)(corner_radius - border_width);
  if (inner_r < 0)
    inner_r = 0;

  for (int row = 0; row < corner_radius; row++) {
    /* Distance from centre of the corner arc to this row */
    double dy = corner_radius - row - 0.5;

    /* Calculate x extent of outer and inner circles at this y */
    double outer_x_extent = 0;
    if (dy <= outer_r) {
      outer_x_extent = sqrt(outer_r * outer_r - dy * dy);
    }

    double inner_x_extent = 0;
    if (dy <= inner_r) {
      inner_x_extent = sqrt(inner_r * inner_r - dy * dy);
    }

    /* The border segment starts where inner circle ends and goes to outer
     * circle */
    int seg_start = (int)floor(corner_radius - outer_x_extent);
    int seg_end = (int)ceil(corner_radius - inner_x_extent);

    /* Clamp to valid range */
    if (seg_start < 0)
      seg_start = 0;
    if (seg_end > corner_radius)
      seg_end = corner_radius;
    if (seg_end < seg_start)
      seg_end = seg_start;

    int seg_width = seg_end - seg_start;
    if (seg_width <= 0)
      continue;

    /* Top-left corner */
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box =
                                           {
                                               .x = x + seg_start,
                                               .y = y + row,
                                               .width = seg_width,
                                               .height = 1,
                                           },
                                       .color = colour,
                                   });

    /* Top-right corner */
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box =
                                           {
                                               .x = x + width - corner_radius +
                                                    (corner_radius - seg_end),
                                               .y = y + row,
                                               .width = seg_width,
                                               .height = 1,
                                           },
                                       .color = colour,
                                   });

    /* Bottom-left corner */
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box =
                                           {
                                               .x = x + seg_start,
                                               .y = y + height - 1 - row,
                                               .width = seg_width,
                                               .height = 1,
                                           },
                                       .color = colour,
                                   });

    /* Bottom-right corner */
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box =
                                           {
                                               .x = x + width - corner_radius +
                                                    (corner_radius - seg_end),
                                               .y = y + height - 1 - row,
                                               .width = seg_width,
                                               .height = 1,
                                           },
                                       .color = colour,
                                   });
  }
}

/*
 * Render corner masks to create the appearance of rounded content corners.
 * This draws background-coloured shapes over the window corners.
 */
static void render_corner_masks(struct wlr_render_pass *pass, int x, int y,
                                int width, int height, int corner_radius,
                                float bg_r, float bg_g, float bg_b,
                                float bg_a) {
  /* Skip if dimensions are too small or no rounding needed */
  if (width <= 0 || height <= 0 || corner_radius <= 0) {
    return;
  }

  struct wlr_render_color bg = {.r = bg_r, .g = bg_g, .b = bg_b, .a = bg_a};

  /* Ensure corner radius doesn't exceed half the smallest dimension */
  int max_radius = (width < height ? width : height) / 2;
  if (corner_radius > max_radius) {
    corner_radius = max_radius;
  }

  /*
   * For each corner, draw background-coloured pixels outside the arc.
   * We iterate row by row and fill the area outside the circle.
   */
  double r = (double)corner_radius;

  for (int row = 0; row < corner_radius; row++) {
    double dy = corner_radius - row - 0.5;
    double dx = 0;
    if (dy <= r) {
      dx = sqrt(r * r - dy * dy);
    }
    int fill_width = (int)floor(corner_radius - dx);

    if (fill_width <= 0)
      continue;

    /* Top-left corner mask */
    wlr_render_pass_add_rect(
        pass,
        &(struct wlr_render_rect_options){
            .box = {.x = x, .y = y + row, .width = fill_width, .height = 1},
            .color = bg,
        });

    /* Top-right corner mask */
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box = {.x = x + width - fill_width,
                                               .y = y + row,
                                               .width = fill_width,
                                               .height = 1},
                                       .color = bg,
                                   });

    /* Bottom-left corner mask */
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box = {.x = x,
                                               .y = y + height - 1 - row,
                                               .width = fill_width,
                                               .height = 1},
                                       .color = bg,
                                   });

    /* Bottom-right corner mask */
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box = {.x = x + width - fill_width,
                                               .y = y + height - 1 - row,
                                               .width = fill_width,
                                               .height = 1},
                                       .color = bg,
                                   });
  }
}

void view_render(struct infinidesk_view *view, struct wlr_render_pass *pass,
                 float output_scale) {
  struct infinidesk_canvas *canvas = &view->server->canvas;
  struct wlr_xdg_surface *xdg_surface = view->xdg_toplevel->base;

  if (!xdg_surface->surface->mapped) {
    return;
  }

  /*
   * Map animation: apply scale and opacity.
   * map_animation goes from 0.0 (just mapped) to 1.0 (fully visible).
   * Scale interpolates from MAP_ANIM_SCALE_START to 1.0.
   * Opacity interpolates from 0.0 to 1.0.
   */
  double map_anim = view->map_animation;
  double anim_scale = lerp_d(MAP_ANIM_SCALE_START, 1.0, map_anim);
  float anim_opacity = (float)map_anim;

  /*
   * Combined scale: canvas scale (zoom level) * output scale (HiDPI) *
   * animation scale. All rendering coordinates must be in physical pixels.
   */
  double base_scale = canvas->scale * output_scale;
  double combined_scale = base_scale * anim_scale;

  /* Convert canvas coordinates to screen coordinates (logical) */
  double screen_x, screen_y;
  canvas_to_screen(canvas, view->x, view->y, &screen_x, &screen_y);

  /* Convert to physical pixels */
  screen_x *= output_scale;
  screen_y *= output_scale;

  /* Account for XDG surface geometry offset (for CSD windows) */
  struct wlr_box geo;
  wlr_xdg_surface_get_geometry(xdg_surface, &geo);

  /*
   * Calculate base dimensions (without animation scale) for centre offset.
   * We want the window to scale from its centre, not top-left corner.
   */
  int base_content_width = (int)round(geo.width * base_scale);
  int base_content_height = (int)round(geo.height * base_scale);

  /* Calculate scaled dimensions (in physical pixels, with animation scale) */
  int scaled_border = (int)round(BORDER_WIDTH * combined_scale);
  int scaled_radius = (int)round(CORNER_RADIUS * combined_scale);
  int content_width = (int)round(geo.width * combined_scale);
  int content_height = (int)round(geo.height * combined_scale);

  /*
   * Calculate position offset to scale from centre.
   * The offset is half the difference between base and animated sizes.
   */
  int centre_offset_x = (base_content_width - content_width) / 2;
  int centre_offset_y = (base_content_height - content_height) / 2;

  int content_x = (int)round(screen_x) - (int)round(geo.x * combined_scale) +
                  centre_offset_x;
  int content_y = (int)round(screen_y) - (int)round(geo.y * combined_scale) +
                  centre_offset_y;

  /* Skip rendering if content is too small to be visible */
  if (content_width <= 0 || content_height <= 0) {
    return;
  }

  /* Ensure minimum values for border and radius */
  if (scaled_border < 1)
    scaled_border = 1;
  if (scaled_radius < 0)
    scaled_radius = 0;

  /* Calculate border colour based on focus animation */
  float focus_t = (float)view->focus_animation;
  float border_r = lerp(BORDER_UNFOCUSED_R, BORDER_FOCUSED_R, focus_t);
  float border_g = lerp(BORDER_UNFOCUSED_G, BORDER_FOCUSED_G, focus_t);
  float border_b = lerp(BORDER_UNFOCUSED_B, BORDER_FOCUSED_B, focus_t);
  float border_a =
      lerp(BORDER_UNFOCUSED_A, BORDER_FOCUSED_A, focus_t) * anim_opacity;

  /* Render the border (outside the content area) */
  int border_x = content_x - scaled_border;
  int border_y = content_y - scaled_border;
  int border_width = content_width + 2 * scaled_border;
  int border_height = content_height + 2 * scaled_border;

  /* Border corner radius includes the border width */
  int border_corner_radius = scaled_radius + scaled_border;

  /* Set up render data for surface content */
  struct render_data data = {
      .pass = pass,
      .view = view,
      .scale = combined_scale,
      .base_x = content_x,
      .base_y = content_y,
      .geo_x = geo.x,
      .geo_y = geo.y,
      .opacity = anim_opacity,
  };

  /*
   * Rendering order:
   * 1. Window content (rectangular texture)
   * 2. Corner masks (rounds off the content corners with background colour)
   * 3. Border (drawn on top so it's not covered by content or masks)
   *
   * This order ensures the border is always fully visible, including its
   * rounded corners, which would otherwise be covered by the rectangular
   * window texture.
   */

  /* 1. Render all surfaces in the XDG surface tree (includes subsurfaces) */
  wlr_xdg_surface_for_each_surface(xdg_surface, render_surface_iterator, &data);

  /* 2. Render corner masks over the content to create rounded corners */
  /* Note: Corner masks use fixed background colour, not affected by opacity */
  render_corner_masks(pass, content_x, content_y, content_width, content_height,
                      scaled_radius, BG_COLOUR_R, BG_COLOUR_G, BG_COLOUR_B,
                      BG_COLOUR_A);

  /* 3. Render the border on top of everything */
  render_border(pass, border_x, border_y, border_width, border_height,
                scaled_border, border_corner_radius, border_r, border_g,
                border_b, border_a);
}

void view_update_focus_animations(struct infinidesk_server *server,
                                  uint32_t time_ms) {
  struct infinidesk_view *view;
  wl_list_for_each(view, &server->views, link) {
    /* Update focus animation */
    if (view->focus_anim_active) {
      uint32_t elapsed = time_ms - view->focus_anim_start_ms;
      double progress = (double)elapsed / VIEW_FOCUS_ANIM_DURATION_MS;

      if (progress >= 1.0) {
        /* Animation complete */
        view->focus_animation = view->focused ? 1.0 : 0.0;
        view->focus_anim_active = false;
      } else {
        /* Apply cubic ease-out */
        double eased = ease_out_cubic(progress);
        if (view->focused) {
          /* Animating towards focused (0 -> 1) */
          view->focus_animation = eased;
        } else {
          /* Animating towards unfocused (1 -> 0) */
          view->focus_animation = 1.0 - eased;
        }
      }
    }

    /* Update map/entrance animation */
    if (view->map_animation < 1.0 && !view->is_animating_out) {
      uint32_t elapsed = time_ms - view->map_anim_start_ms;
      double progress = (double)elapsed / VIEW_MAP_ANIM_DURATION_MS;

      if (progress >= 1.0) {
        /* Animation complete */
        view->map_animation = 1.0;
      } else {
        /* Apply cubic ease-out for smooth entrance */
        view->map_animation = ease_out_cubic(progress);
      }
    }
  }
}

bool view_any_animating(struct infinidesk_server *server) {
  struct infinidesk_view *view;
  wl_list_for_each(view, &server->views, link) {
    if (view->focus_anim_active) {
      return true;
    }
    /* Check if map animation is still in progress */
    if (view->map_animation < 1.0 && !view->is_animating_out) {
      return true;
    }
  }
  return false;
}
