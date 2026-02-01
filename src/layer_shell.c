/*
 * Infinidesk - Infinite Canvas Wayland Compositor
 * Copyright (c) 2025
 * SPDX-License-Identifier: MIT
 *
 * layer_shell.c - wlr-layer-shell-unstable-v1 protocol implementation
 */

#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>

#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include "infinidesk/layer_shell.h"
#include "infinidesk/output.h"
#include "infinidesk/server.h"

/* Forward declarations */
static void handle_layer_surface_map(struct wl_listener *listener, void *data);
static void handle_layer_surface_unmap(struct wl_listener *listener,
                                       void *data);
static void handle_layer_surface_destroy(struct wl_listener *listener,
                                         void *data);
static void handle_layer_surface_commit(struct wl_listener *listener,
                                        void *data);
static void handle_layer_surface_new_popup(struct wl_listener *listener,
                                           void *data);

void layer_shell_init(struct infinidesk_server *server) {
  /* Create the layer shell protocol global (version 4) */
  server->layer_shell = wlr_layer_shell_v1_create(server->wl_display, 4);
  if (!server->layer_shell) {
    wlr_log(WLR_ERROR, "Failed to create layer shell");
    return;
  }

  /* Listen for new layer surfaces */
  server->new_layer_surface.notify = handle_new_layer_surface;
  wl_signal_add(&server->layer_shell->events.new_surface,
                &server->new_layer_surface);

  wlr_log(WLR_INFO, "Layer shell initialised (version 4)");
}

void handle_new_layer_surface(struct wl_listener *listener, void *data) {
  struct infinidesk_server *server =
      wl_container_of(listener, server, new_layer_surface);
  struct wlr_layer_surface_v1 *layer_surface = data;

  wlr_log(WLR_DEBUG, "New layer surface: namespace=%s, layer=%d",
          layer_surface->namespace ?: "(null)", layer_surface->pending.layer);

  /*
   * If no output is specified, assign to the primary output.
   * The protocol requires us to assign an output before returning.
   */
  if (!layer_surface->output) {
    struct infinidesk_output *primary = output_get_primary(server);
    if (!primary) {
      wlr_log(WLR_ERROR, "No output available for layer surface");
      wlr_layer_surface_v1_destroy(layer_surface);
      return;
    }
    layer_surface->output = primary->wlr_output;
    wlr_log(WLR_DEBUG, "Assigned layer surface to output %s",
            primary->wlr_output->name);
  }

  /* Find our output wrapper */
  struct infinidesk_output *output = NULL;
  struct infinidesk_output *iter;
  wl_list_for_each(iter, &server->outputs, link) {
    if (iter->wlr_output == layer_surface->output) {
      output = iter;
      break;
    }
  }

  if (!output) {
    wlr_log(WLR_ERROR, "Could not find output for layer surface");
    wlr_layer_surface_v1_destroy(layer_surface);
    return;
  }

  /* Allocate our layer surface wrapper */
  struct infinidesk_layer_surface *layer = calloc(1, sizeof(*layer));
  if (!layer) {
    wlr_log(WLR_ERROR, "Failed to allocate layer surface");
    wlr_layer_surface_v1_destroy(layer_surface);
    return;
  }

  layer->server = server;
  layer->output = output;
  layer->layer_surface = layer_surface;

  /*
   * Get the appropriate scene tree for this layer.
   * Layer surfaces are rendered at fixed screen positions, not in canvas space.
   */
  enum zwlr_layer_shell_v1_layer shell_layer = layer_surface->pending.layer;
  if (shell_layer >= LAYER_SHELL_LAYER_COUNT) {
    shell_layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  }

  struct wlr_scene_tree *parent_tree = output->layer_trees[shell_layer];
  if (!parent_tree) {
    wlr_log(WLR_ERROR, "No scene tree for layer %d", shell_layer);
    free(layer);
    wlr_layer_surface_v1_destroy(layer_surface);
    return;
  }

  /* Create the scene tree for this layer surface */
  layer->scene_layer_surface =
      wlr_scene_layer_surface_v1_create(parent_tree, layer_surface);
  if (!layer->scene_layer_surface) {
    wlr_log(WLR_ERROR, "Failed to create scene layer surface");
    free(layer);
    wlr_layer_surface_v1_destroy(layer_surface);
    return;
  }

  layer->scene_tree = layer->scene_layer_surface->tree;
  layer->scene_tree->node.data = layer;
  layer_surface->data = layer;

  /* Set up event listeners */
  layer->map.notify = handle_layer_surface_map;
  wl_signal_add(&layer_surface->surface->events.map, &layer->map);

  layer->unmap.notify = handle_layer_surface_unmap;
  wl_signal_add(&layer_surface->surface->events.unmap, &layer->unmap);

  layer->destroy.notify = handle_layer_surface_destroy;
  wl_signal_add(&layer_surface->events.destroy, &layer->destroy);

  layer->commit.notify = handle_layer_surface_commit;
  wl_signal_add(&layer_surface->surface->events.commit, &layer->commit);

  layer->new_popup.notify = handle_layer_surface_new_popup;
  wl_signal_add(&layer_surface->events.new_popup, &layer->new_popup);

  /* Add to the output's layer list */
  wl_list_insert(&output->layer_surfaces[shell_layer], &layer->link);

  /* Arrange layer surfaces to send initial configure */
  layer_shell_arrange(output);

  wlr_log(WLR_DEBUG, "Created layer surface %p on output %s, layer %d",
          (void *)layer, output->wlr_output->name, shell_layer);
}

static void handle_layer_surface_map(struct wl_listener *listener, void *data) {
  (void)data;
  struct infinidesk_layer_surface *layer =
      wl_container_of(listener, layer, map);

  wlr_log(WLR_DEBUG, "Layer surface %p mapped", (void *)layer);

  /* Re-arrange to update exclusive zones */
  layer_shell_arrange(layer->output);
}

static void handle_layer_surface_unmap(struct wl_listener *listener,
                                       void *data) {
  (void)data;
  struct infinidesk_layer_surface *layer =
      wl_container_of(listener, layer, unmap);

  wlr_log(WLR_DEBUG, "Layer surface %p unmapped", (void *)layer);

  /* Re-arrange to update exclusive zones */
  layer_shell_arrange(layer->output);
}

static void handle_layer_surface_destroy(struct wl_listener *listener,
                                         void *data) {
  (void)data;
  struct infinidesk_layer_surface *layer =
      wl_container_of(listener, layer, destroy);

  wlr_log(WLR_DEBUG, "Layer surface %p destroyed", (void *)layer);

  /* Remove from output's layer list */
  wl_list_remove(&layer->link);

  /* Remove event listeners */
  wl_list_remove(&layer->map.link);
  wl_list_remove(&layer->unmap.link);
  wl_list_remove(&layer->destroy.link);
  wl_list_remove(&layer->commit.link);
  wl_list_remove(&layer->new_popup.link);

  /* Re-arrange remaining surfaces */
  layer_shell_arrange(layer->output);

  free(layer);
}

static void handle_layer_surface_commit(struct wl_listener *listener,
                                        void *data) {
  (void)data;
  struct infinidesk_layer_surface *layer =
      wl_container_of(listener, layer, commit);
  struct wlr_layer_surface_v1 *layer_surface = layer->layer_surface;

  /*
   * Handle initial commit - this is when the client first tells us
   * what it wants (size, anchors, etc.) and we must respond with a configure.
   */
  if (layer_surface->initial_commit) {
    /*
     * Check if layer changed from pending to current on initial commit.
     * Move to the correct layer tree if needed.
     */
    enum zwlr_layer_shell_v1_layer current_layer = layer_surface->current.layer;
    enum zwlr_layer_shell_v1_layer pending_layer = layer_surface->pending.layer;

    if (current_layer != pending_layer && current_layer < LAYER_SHELL_LAYER_COUNT) {
      wl_list_remove(&layer->link);
      struct wlr_scene_tree *new_parent = layer->output->layer_trees[current_layer];
      wlr_scene_node_reparent(&layer->scene_tree->node, new_parent);
      wl_list_insert(&layer->output->layer_surfaces[current_layer], &layer->link);
    }

    /* Arrange to send the initial configure with dimensions */
    layer_shell_arrange(layer->output);
    return;
  }

  if (!layer_surface->initialized) {
    return;
  }

  /*
   * Check if the layer changed - if so, we need to move to a different tree.
   */
  uint32_t committed = layer_surface->current.committed;
  if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
    enum zwlr_layer_shell_v1_layer new_layer = layer_surface->current.layer;
    if (new_layer < LAYER_SHELL_LAYER_COUNT) {
      /* Remove from old list */
      wl_list_remove(&layer->link);

      /* Move scene node to new parent tree */
      struct wlr_scene_tree *new_parent = layer->output->layer_trees[new_layer];
      wlr_scene_node_reparent(&layer->scene_tree->node, new_parent);

      /* Add to new list */
      wl_list_insert(&layer->output->layer_surfaces[new_layer], &layer->link);

      wlr_log(WLR_DEBUG, "Layer surface moved to layer %d", new_layer);
    }
  }

  /* Re-arrange if any relevant state changed */
  if (committed &
      (WLR_LAYER_SURFACE_V1_STATE_DESIRED_SIZE |
       WLR_LAYER_SURFACE_V1_STATE_ANCHOR |
       WLR_LAYER_SURFACE_V1_STATE_EXCLUSIVE_ZONE |
       WLR_LAYER_SURFACE_V1_STATE_MARGIN | WLR_LAYER_SURFACE_V1_STATE_LAYER)) {
    layer_shell_arrange(layer->output);
  }
}

static void handle_layer_surface_new_popup(struct wl_listener *listener,
                                           void *data) {
  struct infinidesk_layer_surface *layer =
      wl_container_of(listener, layer, new_popup);
  struct wlr_xdg_popup *popup = data;

  wlr_log(WLR_DEBUG, "New popup for layer surface %p", (void *)layer);

  /*
   * Create the popup in the layer surface's scene tree.
   * This is similar to how we handle XDG shell popups.
   */
  struct wlr_scene_tree *popup_tree =
      wlr_scene_xdg_surface_create(layer->scene_tree, popup->base);
  if (!popup_tree) {
    wlr_log(WLR_ERROR, "Failed to create scene tree for layer popup");
    return;
  }

  popup->base->data = popup_tree;
}

void layer_shell_arrange(struct infinidesk_output *output) {
  /*
   * Arrange all layer surfaces on this output.
   *
   * We process layers in order: background, bottom, top, overlay.
   * Each layer can claim exclusive zones from the usable area.
   */

  int width, height;
  wlr_output_effective_resolution(output->wlr_output, &width, &height);

  /* Start with the full output area */
  struct wlr_box full_area = {
      .x = 0,
      .y = 0,
      .width = width,
      .height = height,
  };

  /* Usable area is reduced by exclusive zones */
  struct wlr_box usable_area = full_area;

  /*
   * Process layers that can have exclusive zones.
   * Background and overlay layers are processed but typically don't claim
   * exclusive zones.
   */
  for (int layer_idx = 0; layer_idx < LAYER_SHELL_LAYER_COUNT; layer_idx++) {
    struct infinidesk_layer_surface *layer;
    wl_list_for_each(layer, &output->layer_surfaces[layer_idx], link) {
      struct wlr_layer_surface_v1 *layer_surface = layer->layer_surface;

      /*
       * Use the wlroots helper to configure the layer surface.
       * This calculates the surface position based on anchors and margins,
       * and updates usable_area for exclusive zones.
       */
      wlr_scene_layer_surface_v1_configure(layer->scene_layer_surface,
                                           &full_area, &usable_area);

      wlr_log(WLR_DEBUG,
              "Arranged layer surface: layer=%d, pos=(%d,%d), "
              "size=%dx%d, exclusive=%d",
              layer_idx, layer->scene_tree->node.x, layer->scene_tree->node.y,
              layer_surface->current.actual_width,
              layer_surface->current.actual_height,
              layer_surface->current.exclusive_zone);
    }
  }

  /* Store the usable area for window placement */
  output->usable_area = usable_area;

  wlr_log(WLR_DEBUG, "Output %s usable area: (%d,%d) %dx%d",
          output->wlr_output->name, usable_area.x, usable_area.y,
          usable_area.width, usable_area.height);
}

void layer_shell_get_usable_area(struct infinidesk_output *output,
                                 struct wlr_box *usable_area) {
  *usable_area = output->usable_area;
}

struct infinidesk_layer_surface *
layer_surface_at(struct infinidesk_output *output, double ox, double oy,
                 struct wlr_surface **surface, double *sx, double *sy) {
  /*
   * Search for a layer surface at the given output-local coordinates.
   * We search from overlay to background (top to bottom in z-order).
   */
  for (int layer_idx = LAYER_SHELL_LAYER_COUNT - 1; layer_idx >= 0;
       layer_idx--) {
    struct infinidesk_layer_surface *layer;
    wl_list_for_each(layer, &output->layer_surfaces[layer_idx], link) {
      if (!layer->layer_surface->surface->mapped) {
        continue;
      }

      /* Calculate position relative to the layer surface */
      double lx = ox - layer->scene_tree->node.x;
      double ly = oy - layer->scene_tree->node.y;

      /* Use wlroots helper to find surface at coordinates */
      struct wlr_surface *found =
          wlr_layer_surface_v1_surface_at(layer->layer_surface, lx, ly, sx, sy);

      if (found) {
        *surface = found;
        return layer;
      }
    }
  }

  *surface = NULL;
  return NULL;
}
