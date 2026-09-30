// systems/board_cursor.h
//
// The board's pointer.
//
// The OS cursor is hidden while the pointer is over the window (see
// UpdateCursorVisibility in app/app.cpp), so this is the only pointer the user
// has. It is therefore a permanent part of the app rather than part of the
// temporary pan/zoom overlay, and it lives here for that reason.
//
// Screen space, not world space, and called after EndMode2D. Two consequences
// that are the whole point:
//
//   • the crosshair is the same size at every zoom. Drawn in world space it
//     would be scaled by the camera, so it would be a giant crosshair when
//     zoomed in and a dot when zoomed out.
//   • it does not move when the camera pans, which a world-space cursor does:
//     the pointer should stay under the physical mouse.
//
// Four thin arms crossing at the pointer: a plain cross, one screen pixel
// thick. Deliberately no centre dot, no gap and no contrasting outline -- a
// crosshair that occludes the thing it is pointing at is worse than one that
// briefly disappears against a light image.
#pragma once

#include "engine/globals.h"
#include "raylib.h"

namespace referentia::systems {

// Logical units, multiplied by ui_scale like the rest of the UI.
constexpr float kCursorArm = 8.f;       // distance from centre to arm tip
constexpr float kCursorThickness = 1.f;

/**
 * Draws the crosshair at the current mouse position.
 *
 * Call after EndMode2D: the camera transform must not apply to the pointer.
 */
inline void DrawBoardCursor() {
  const Vector2 p = GetMousePosition();
  const float arm = kCursorArm * ui_scale;
  const float thickness = kCursorThickness * ui_scale;

  // Arms run through the centre rather than stopping short of it, so the two
  // lines meet in a solid crossing.
  DrawLineEx({p.x - arm, p.y}, {p.x + arm, p.y}, thickness, theme::rotate_handle);
  DrawLineEx({p.x, p.y - arm}, {p.x, p.y + arm}, thickness, theme::rotate_handle);
}

}  // namespace referentia::systems
