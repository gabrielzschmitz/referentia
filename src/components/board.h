// components/board.h
#pragma once

#include <string_view>

#include "board/transform.h"

namespace referentia::components {

/**
 * Board world state.
 *
 * Sits on the single world-root entity (TagWorldRoot). The board plane is
 * unbounded, so this deliberately holds no extent or size: it exists to give
 * the world an addressable anchor and to hold board-level settings that are
 * not properties of any one node.
 */
struct BoardWorldComponent {
  static constexpr std::string_view Name = "BoardWorld";
};

/**
 * TEMPORARY: the pan/zoom verification overlay.
 *
 * This was previously a hardcoded set of constants passed straight into
 * DrawBoardDebugWorld, which meant the overlay was not addressable, could not
 * be toggled, and could not be inspected. It is a component on a TagOverlay
 * entity so the render system reads it through a query like everything else.
 *
 * Removed once the real node renderer lands; the screen HUD survives that as
 * the diagnostics panel.
 */
struct BoardDebugComponent {
  static constexpr std::string_view Name = "BoardDebug";

  /** Adaptive 1-2-5 world grid. */
  bool showGrid = true;

  /** World-anchored reference rectangle, for confirming pan and zoom. */
  bool showReferenceRect = true;
  board::Rect referenceRect{0.f, 0.f, 400.f, 300.f};

  /** Screen-space numeric readout and control hints. */
  bool showHud = true;

  /**
   * The reference rectangle is centred on this world point. Kept separate from
   * `referenceRect.x/y` so the rect's position stays its top-left corner, as
   * consumers of Rect expect everywhere else in the codebase.
   *
   * board::Vec2 rather than raylib's Vector2: this component is plain data
   * describing board geometry, which keeps it free of raylib and therefore
   * includable from the headless test target. The renderer converts at the
   * draw boundary.
   */
  board::Vec2 referenceCenter{0.f, 0.f};
};

}  // namespace referentia::components
