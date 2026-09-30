// board/transform.h
//
// Pure world<->screen transform for the board camera, with no raylib
// dependency so it can be unit tested headlessly.
//
// A Camera2D maps
//
//     screen = (world - target) * zoom + offset
//
// and this app pins `offset` to the centre of the framebuffer every frame
// (see systems::UpdateCamera2D), which collapses the common case to
//
//     world = (screen - viewport/2) / zoom + target
//
// The offset-pinned form is used for the visible-rect maths because it makes
// panning and zooming independent: changing the target moves the view without
// changing how much world is visible.
//
// Everything here is float and header-only on purpose; the board geometry is
// small enough that there is no reason to build a separate translation unit,
// and keeping it inline avoids a call across the draw boundary per node.
#pragma once

#include <cmath>

namespace referentia::board {

struct Vec2 {
  float x = 0.f;
  float y = 0.f;
};

struct Rect {
  float x = 0.f;
  float y = 0.f;
  float width = 0.f;
  float height = 0.f;
};

/*
 * Move `r` so that its centre lands on (cx, cy).
 *
 * Used to place content centred on the world origin. The camera opens with the
 * origin at the viewport centre, so a rect given as {0, 0, w, h} would be
 * anchored by its top-left corner and hang off to one side.
 */
inline Rect CentreOn(Rect r, float cx, float cy) {
  r.x = cx - r.width * 0.5f;
  r.y = cy - r.height * 0.5f;
  return r;
}

/*
 * World rectangle currently covered by a viewport.
 *
 * Independent of the camera target: panning slides this rect, zooming scales
 * it about its centre, and neither changes the other's result.
 */
inline Rect VisibleWorldRect(float target_x, float target_y, float zoom,
                             float viewport_w, float viewport_h) {
  const float half_w = viewport_w * 0.5f / zoom;
  const float half_h = viewport_h * 0.5f / zoom;
  return {target_x - half_w, target_y - half_h, half_w * 2.f, half_h * 2.f};
}

// World point -> screen point. `offset` is the camera offset; systems pass the
// viewport centre because that is what the camera maintains.
inline Vec2 WorldToScreen(float world_x, float world_y, float target_x,
                          float target_y, float zoom, float offset_x,
                          float offset_y) {
  return {(world_x - target_x) * zoom + offset_x,
          (world_y - target_y) * zoom + offset_y};
}

// Screen point -> world point. Exact inverse of WorldToScreen.
inline Vec2 ScreenToWorld(float screen_x, float screen_y, float target_x,
                          float target_y, float zoom, float offset_x,
                          float offset_y) {
  return {(screen_x - offset_x) / zoom + target_x,
          (screen_y - offset_y) / zoom + target_y};
}

// Convenience wrappers for the offset-pinned-to-centre case.
inline Vec2 WorldToScreenCentre(float world_x, float world_y, float target_x,
                                float target_y, float zoom, float viewport_w,
                                float viewport_h) {
  return WorldToScreen(world_x, world_y, target_x, target_y, zoom,
                       viewport_w * 0.5f, viewport_h * 0.5f);
}

inline Vec2 ScreenToWorldCentre(float screen_x, float screen_y, float target_x,
                                float target_y, float zoom, float viewport_w,
                                float viewport_h) {
  return ScreenToWorld(screen_x, screen_y, target_x, target_y, zoom,
                       viewport_w * 0.5f, viewport_h * 0.5f);
}

/*
 * World units spanned by one screen pixel, and its reciprocal.
 *
 * Hit testing and handle sizes need this: a handle must stay a constant size
 * on screen while the board is zoomed, so its world size has to track the
 * zoom. `PixelsToWorld` is the conversion, `WorldToPixels` the inverse.
 */
inline float PixelsToWorld(float pixels, float zoom) { return pixels / zoom; }
inline float WorldToPixels(float world, float zoom) { return world * zoom; }

/*
 * Grid spacing in world units that keeps on-screen gaps in a readable band
 * across the whole zoom range, snapped to a 1-2-5 ladder so landmarks always
 * land on round world coordinates.
 *
 * A fixed spacing degenerates at both ends of the range: zoomed out it is a
 * solid smear, zoomed in it is a single dot. Snapping to plain decades is not
 * enough either - the gap then jumps by 10x at each threshold (e.g. 90px then
 * 9px), which reads as the grid glitching. The 1-2-5 ladder caps the step
 * between rungs at 2.5x and keeps gaps within [target_px/2.5, target_px].
 */
inline float GridSpacing(float zoom, float base = 100.f,
                         float target_px = 90.f) {
  if (zoom <= 0.f) return base;

  // World distance that would render as exactly target_px.
  const float ideal = target_px / zoom;

  // Bring ideal into [base, base*10), then snap its mantissa to 1, 2 or 5.
  const float exponent = std::floor(std::log10(ideal / base));
  const float magnitude = base * std::pow(10.f, exponent);
  const float mantissa = ideal / magnitude;

  const float step = (mantissa >= 5.f) ? 5.f : (mantissa >= 2.f) ? 2.f : 1.f;
  return magnitude * step;
}

/**
 * Whether a point lies inside a viewport.
 *
 * raylib's own IsCursorOnScreen() is GLFW-only: the Win32, SDL and Web
 * backends declare the function but never set the flag it reads, so it returns
 * false forever and anything gated on it silently never runs. The pointer
 * position is window-relative in every backend, so the test is two comparisons
 * and does not depend on which one is compiled in.
 *
 * Left/top inclusive, right/bottom exclusive. A pointer at exactly the far edge
 * is reported outside, which matches a half-open client area: the rightmost
 * pixel of the window is width-1, so x == width is already off it.
 */
inline bool IsInsideViewport(float x, float y, float width, float height) {
  return x >= 0.f && y >= 0.f && x < width && y < height;
}

}  // namespace referentia::board
