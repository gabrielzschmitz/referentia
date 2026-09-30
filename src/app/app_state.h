// app/app_state.h
#pragma once

#include "engine/ecs/ecs.h"

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
};

}  // namespace referentia::app
