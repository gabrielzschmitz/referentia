// app/app_state.h
#pragma once

#include "app/image_import.h"
#include "engine/ecs/ecs.h"
#include "systems/image_nodes.h"

namespace referentia::app {

/**
 * ============================================================================
 * AppState
 * ============================================================================
 *
 * Owns the ECS and the handles into it.
 *
 * The handles are the only non-ECS state here, deliberately. The board's whole
 * persistent identity is a set of entity handles; component data lives in the
 * ECS and is reached through queries, so there is exactly one copy of the
 * truth and it is the thing the systems iterate over.
 *
 * Every handle defaults to INVALID_ENTITY rather than Entity{0}: index 0 is a
 * valid entity, so defaulting to it meant code that read a handle before
 * assigning it would silently operate on whichever entity happened to be
 * allocated first instead of failing loudly.
 *
 * The importer is the one exception, and it is here because it owns GPU objects
 * rather than board data. See ImageImporter::textures for why the ownership
 * record cannot live in the ECS.
 * ============================================================================
 */

struct AppState {
  /** Board world root: ancestor of every node and group. */
  motrix::engine::Entity boardWorld{motrix::engine::INVALID_ENTITY};

  /** TEMPORARY: pan/zoom verification overlay, a TagOverlay entity. */
  motrix::engine::Entity debugOverlay{motrix::engine::INVALID_ENTITY};

  /** The one board camera. */
  motrix::engine::Entity cameraEntity{motrix::engine::INVALID_ENTITY};

  motrix::engine::ECS ecs;

  /**
   * Imported images: the pending queue and every texture this session created.
   *
   * Placed after the ECS so the pointer to it can be taken in InitApp, and
   * pointed back at that same ECS there. The back-pointer is set once and never
   * reassigned; a null `ecs` makes UpdateImageImport() a no-op rather than a
   * crash, which is the right failure mode for a member of a struct that is
   * default-constructed in RunApp.
   */
  ImageImporter importer;

  /**
   * The node currently held by the pointer, if any.
   *
   * Transient input state rather than board data, so it does not live in the ECS
   * and no node "knows" it is being dragged -- it just finds its transform
   * changed. Holding it here rather than in a component is what keeps "which
   * node owns this drag" a single field instead of a per-frame query over
   * every node's flag.
   */
  referentia::systems::NodeGrab nodeGrab;
};

}  // namespace referentia::app
