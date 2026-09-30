// app/board.h
#pragma once

#include "components/board.h"
#include "components/tags.h"
#include "engine/ecs/ecs.h"
#include "entities/board.h"
#include "systems/board_debug.h"

namespace referentia::app {

/*
 * ============================================================================
 * The board
 * ============================================================================
 *
 * Referentia has exactly one board, so there is no scene registry, no
 * SceneType enum and no name table: these four functions are called directly
 * by the app loop. The indirection only ever had one entry to dispatch to, and
 * keeping it meant a `-s/--scene` flag that could only ever be passed "board".
 *
 * Image/text nodes, group frames and auto-arrange layer on top of this; the
 * empty CreateBoardUI is the hook F10 uses to rebuild the panel.
 * ============================================================================
 */

inline void BoardInit(AppState& state) {
  state.boardWorld = referentia::entities::CreateBoardWorld(state.ecs);
  state.debugOverlay =
      referentia::entities::CreateBoardDebugOverlay(state.ecs);

  LOG_DEBUG("[BOARD] initialised: world root entity {}, overlay entity {}",
            state.boardWorld.index, state.debugOverlay.index);
}

inline void BoardUpdate(AppState&, float) {}

// TEMPORARY: draws world-anchored landmarks so pan/zoom can be confirmed while
// the board has no real content. Replaced by the node renderer.
//
// The overlay is found by query rather than through state.debugOverlay: that
// way the render path does not depend on the handle staying valid, and
// destroying the overlay entity is all it takes to switch the overlay off.
inline void BoardRender(AppState& state) {
  auto& cam = state.ecs.get<referentia::components::CameraComponent>(
      state.cameraEntity);

  state.ecs.view<referentia::components::TagOverlay,
                 referentia::components::BoardDebugComponent>(
      [&](motrix::engine::Entity,
          referentia::components::TagOverlay&,
          referentia::components::BoardDebugComponent& cfg) {
        referentia::systems::DrawBoardDebugWorld(cam.camera, cfg);
      });
}

inline void BoardCreateUI(motrix::engine::ECS&) {}

}  // namespace referentia::app
