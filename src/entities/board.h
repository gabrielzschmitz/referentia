// entities/board.h
#pragma once

#include "components/board.h"
#include "components/tags.h"
#include "engine/ecs/ecs.h"

namespace referentia::entities {
namespace ec = referentia::components;
namespace engine = motrix::engine;

/**
 * ============================================================================
 * Board Entities
 * ============================================================================
 *
 * The world root. Every node and group is a descendant of exactly one of these,
 * so the board always has a single addressable anchor and "everything on this
 * board" is answerable by walking from it rather than by matching components.
 *
 * The handle is returned and retained by AppState. This previously created the
 * entity and discarded the handle, which left an entity that was alive, had no
 * components, and was therefore invisible to every view<Ts...>() query in the
 * ECS -- a silent no-op that looked like it was working.
 * ============================================================================
 */

inline engine::Entity CreateBoardWorld(engine::ECS& ecs) {
  engine::Entity e = ecs.create_entity("board-world-root");

  ecs.add<ec::TagWorldRoot>(e);
  ecs.add<ec::BoardWorldComponent>(e);

  return e;
}

/**
 * TEMPORARY: the pan/zoom verification overlay as an entity.
 *
 * Separate from the world root so it can be found with view<TagOverlay>() and
 * switched off wholesale without special-casing the root, and so deleting it
 * when the real node renderer lands is a one-line destroy.
 */
inline engine::Entity CreateBoardDebugOverlay(engine::ECS& ecs) {
  engine::Entity e = ecs.create_entity("board-debug-overlay");

  ecs.add<ec::TagOverlay>(e);
  ecs.add<ec::BoardDebugComponent>(e);

  return e;
}

}  // namespace referentia::entities
