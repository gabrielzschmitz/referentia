// app/board.h
#pragma once

#include "app/screenshot.h"
#include "components/board.h"
#include "components/camera.h"
#include "components/tags.h"
#include "components/ui.h"
#include "engine/ecs/ecs.h"
#include "entities/board.h"
#include "entities/ui.h"
#include "systems/board_debug.h"
#include "systems/frame_history.h"

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
  state.debugOverlay = referentia::entities::CreateBoardDebugOverlay(state.ecs);

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
  auto& cam =
    state.ecs.get<referentia::components::CameraComponent>(state.cameraEntity);

  state.ecs.view<referentia::components::TagOverlay,
                 referentia::components::BoardDebugComponent>(
    [&](motrix::engine::Entity, referentia::components::TagOverlay&,
        referentia::components::BoardDebugComponent& cfg) {
      referentia::systems::DrawBoardDebugWorld(cam.camera, cfg);
    });
}

namespace ref = referentia::entities;
namespace ec = referentia::components;
namespace engine = motrix::engine;

/**
 * Builds the board panel that F10 toggles.
 *
 * This is the whole point of the CreateUI hook: F10 was wired to a callback that
 * did nothing, so the key that the help text advertised opened no panel, and
 * the only way to reach any of this state was the handful of function keys. The
 * panel is the discoverable surface for it, and every checkbox binds to the same
 * flag its key toggles, so the two can never disagree.
 *
 * Only state that actually exists is exposed. There is no node list, group
 * manager or export control here yet, because there is nothing behind them; a
 * greyed-out button for a feature that has not landed is worse than no button.
 */
inline void BoardCreateUI(engine::ECS& ecs) {
  // The debug overlay is located by query, not from AppState::debugOverlay, so
  // this stays valid if the handle is ever invalidated. Without one, the
  // checkboxes that drive it have nothing to bind to and are skipped rather
  // than added dead.
  engine::Entity overlay = engine::INVALID_ENTITY;
  ecs.view<ec::TagOverlay, ec::BoardDebugComponent>(
    [&](engine::Entity e, ec::TagOverlay&, ec::BoardDebugComponent&) {
      overlay = e;
    });

  const engine::Entity window =
    ref::AddWindow(ecs, {20.f, 20.f}, 250.f, 0.f, "Board");
  {
    // Horizontal inset only. The vertical rhythm -- every gap between rows,
    // groups and window edges -- is components::kRowGap, so that a gap means
    // the same thing everywhere; setting a per-window value here was what let
    // the Board panel's spacing drift away from the rest of the panel.
    ecs.get<ec::UIWindowComponent>(window).padding = 14.f;
  }

  // --- View ---------------------------------------------------------------
  // TEMPORARY: the pan/zoom verification landmarks. Removed with the debug
  // overlay once real nodes can stand in for them.
  // Left and one per line: these are lists of checkboxes, and centring rows of
  // differing widths staggers the labels down a ragged edge.
  const engine::Entity view =
    ref::AddGroup(ecs, window, "View", ec::UIGroupAlign::Left, true);

  if (overlay != engine::INVALID_ENTITY) {
    const auto [get_grid, set_grid] =
      ref::FieldBinding<ec::BoardDebugComponent, bool>(
        ecs, overlay, &ec::BoardDebugComponent::showGrid);
    ref::AddCheckbox(ecs, window, view, "Grid", get_grid, set_grid, {},
                     "Adaptive 1-2-5 world grid.");

    const auto [get_ref, set_ref] =
      ref::FieldBinding<ec::BoardDebugComponent, bool>(
        ecs, overlay, &ec::BoardDebugComponent::showReferenceRect);
    ref::AddCheckbox(ecs, window, view, "Reference square", get_ref, set_ref,
                     {}, "World-anchored square, for confirming pan and zoom.");
  }

  // --- Diagnostics --------------------------------------------------------
  // Same globals the F11 and F8 handlers flip. Left and one per line, as View.
  const engine::Entity diag =
    ref::AddGroup(ecs, window, "Diagnostics", ec::UIGroupAlign::Left, true);

  ref::AddCheckbox(
    ecs, window, diag, "Frame rate (F11)", [] { return show_fps; },
    [](bool on) { show_fps = on; }, {},
    "Frame rate and frame time, top right.");
  ref::AddCheckbox(
    ecs, window, diag, "Frame graph (F8)", [] { return show_frame_graph; },
    [](bool on) {
      show_frame_graph = on;
      // Cleared so switching the graph on shows the frames
      // after the press, not a stale window from before it was
      // last turned off.
      if (on) g_frame_history.Clear();
    },
    {}, "Graph of the last four seconds of frame times.");

  // --- Board --------------------------------------------------------------
  // Centred, and packed: the two actions belong side by side as a pair, so this
  // is the one group where centring the row is what reads as deliberate.
  const engine::Entity board = ref::AddGroup(ecs, window, "Board");

  ref::AddButton(
    ecs, window, board, "Reset view",
    [&ecs] {
      ecs.view<ec::TagCamera, ec::CameraComponent>(
        [&](engine::Entity, ec::TagCamera&, ec::CameraComponent& cam) {
          // Identical to the `=` key, which does the same assignment inline in
          // the camera system.
          cam.camera.target = {0.f, 0.f};
          cam.camera.zoom = 1.f;
        });
      LOG_DEBUG("[UI] view reset to target (0,0) at zoom 1");
    },
    "Centre on the world origin at 1:1 zoom.");

  ref::AddButton(
    ecs, window, board, "Screenshot",
    // Queued, not captured: this handler runs inside the widget pass, so a
    // direct capture would read back this frame before the panel finished
    // drawing. FlushScreenshotRequest() serves it at the end of the frame.
    [] { referentia::app::RequestScreenshot(); },
    "Same as F12. Writes to screenshots/.");

  LOG_DEBUG("[UI] board panel created (window entity {})", window.index);
}

}  // namespace referentia::app
