// systems/board_debug.h
//
// TEMPORARY verification overlay for the empty board.
//
// Phase 1 ships no content, which makes the camera impossible to confirm by
// hand: panning an empty background and zooming an empty background both look
// like "nothing happened". This draws world-anchored landmarks whose screen
// behaviour must track the camera exactly, plus a numeric readout so a wrong
// projection shows up as wrong numbers rather than a judgement call.
//
// Landmarks, chosen so each isolates one part of the transform:
//   • a world grid       -> pan is unbounded, spacing tracks zoom
//   • a labelled square  -> zoom changes apparent size; 1:1 at zoom 1
//   • the origin cross   -> the world origin stays put under pan
//   • HUD readout        -> zoom/target/visible rect/cursor, numerically
//   • projection check   -> this module's transform vs raylib's, live
//
// The transform itself lives in board/transform.h and is unit tested; this
// file only draws. Delete it and its two call sites once nodes exist.
#pragma once

#include <algorithm>
#include <cmath>

#include "app/fonts.h"
#include "board/transform.h"
#include "components/board.h"
#include "components/camera.h"
#include "engine/globals.h"
#include "raylib.h"

namespace referentia::systems {

namespace ec = referentia::components;
namespace board = referentia::board;

// Readout sizes. These are logical units, multiplied by ui_scale at draw time
// like the rest of the UI, so the HUD stays legible on a HiDPI display instead
// of shrinking into the physical pixel grid.
constexpr float kHudFontSize = 19.f;
constexpr float kHudLineGap = 4.f;
constexpr float kHintFontSize = 16.f;
// World units, not screen pixels: this scales with zoom alongside the square.
constexpr float kRefLabelSize = 26.f;

inline float ViewportW() { return static_cast<float>(GetScreenWidth()); }
inline float ViewportH() { return static_cast<float>(GetScreenHeight()); }

// World rectangle currently visible. Delegates to the tested helper.
inline board::Rect VisibleWorldRect(const Camera2D& cam) {
  return board::VisibleWorldRect(cam.target.x, cam.target.y, cam.zoom,
                                 ViewportW(), ViewportH());
}

// World-space landmarks. Call inside BeginMode2D/EndMode2D.
//
// Settings come from the overlay's BoardDebugComponent rather than from
// constants baked into this function, so what is drawn is ECS state that can
// be inspected and toggled instead of being invisible here.
inline void DrawBoardDebugWorld(const Camera2D& cam,
                                const ec::BoardDebugComponent& cfg) {
  const board::Rect view = VisibleWorldRect(cam);

  // Shared by the grid and the reference square, so hoisted above both: one
  // screen pixel expressed in world units, which keeps line weights a constant
  // hairline at any zoom instead of ballooning as you zoom in.
  const float hairline = board::PixelsToWorld(1.f, cam.zoom);
  const Color axis = theme::board_grid_axis;

  // --- grid ---
  if (cfg.showGrid) {
    const float spacing = board::GridSpacing(cam.zoom);
    // Fade as it densifies so a zoomed-out board does not turn grey.
    const float fade = std::clamp(spacing * cam.zoom / 90.f, 0.25f, 1.f);
    const Color dot = Fade(theme::board_grid_dot, fade);

    // Cap the step count: at the 0.02 zoom floor a 1px-accurate walk would be
    // hundreds of thousands of draw calls.
    constexpr int kMaxSteps = 600;
    const float x0 = view.x - view.width;
    const float x1 = view.x + view.width * 2.f;
    const float y0 = view.y - view.height;
    const float y1 = view.y + view.height * 2.f;

    int i = 0;
    for (float x = std::floor(view.x / spacing) * spacing;
         x <= x1 && i < kMaxSteps; x += spacing, ++i) {
      const bool on_axis = std::fabs(x) < spacing * 0.001f;
      DrawLineEx({x, y0}, {x, y1}, hairline, on_axis ? axis : dot);
    }

    i = 0;
    for (float y = std::floor(view.y / spacing) * spacing;
         y <= y1 && i < kMaxSteps; y += spacing, ++i) {
      const bool on_axis = std::fabs(y) < spacing * 0.001f;
      DrawLineEx({x0, y}, {x1, y}, hairline, on_axis ? axis : dot);
    }
  }  // showGrid

  // --- reference square, centred on the world origin ---
  // Drawn in world space on purpose: it must measure 400x300 screen px at
  // zoom 1 and scale linearly from there.
  //
  // Centred on the world origin, not anchored to it: the camera opens with the
  // origin at the viewport centre, so a top-left-anchored rect would hang off
  // to the right and read as misplaced.
  if (cfg.showReferenceRect) {
    const float kRefW = cfg.referenceRect.width;
    const float kRefH = cfg.referenceRect.height;
    const board::Rect centred = board::CentreOn(
      {0.f, 0.f, kRefW, kRefH}, cfg.referenceCenter.x, cfg.referenceCenter.y);
    const Rectangle ref{centred.x, centred.y, centred.width, centred.height};
    DrawRectangleRec(ref, Fade(theme::selection_fill, 0.80f));
    DrawRectangleLinesEx(ref, hairline, theme::selection_outline);

    // Size label, in world units so it scales alongside the square and makes a
    // wrong zoom immediately legible. Centred over the top edge.
    const std::string ref_label =
      TextFormat("%dx%d world", (int)kRefW, (int)kRefH);
    const Vector2 ref_label_size = MeasureTextEx(
      GetFont(FontWeight::Medium), ref_label.c_str(), kRefLabelSize, 1.f);
    DrawTextEx(
      GetFont(FontWeight::Medium), ref_label.c_str(),
      {ref.x + (kRefW - ref_label_size.x) * 0.5f, ref.y - kRefLabelSize * 1.4f},
      kRefLabelSize, 1.f, theme::node_label);

    // Diagonals through the square make it obvious that its centre is the origin.
    DrawLineEx({ref.x, ref.y}, {ref.x + ref.width, ref.y + ref.height},
               hairline, Fade(theme::node_label_dim, 0.5f));
    DrawLineEx({ref.x, ref.y + ref.height}, {ref.x + ref.width, ref.y},
               hairline, Fade(theme::node_label_dim, 0.5f));
  }  // showReferenceRect

  // --- origin cross ---
  const float t = 10.f;
  DrawLineEx({-t, 0.f}, {t, 0.f}, hairline, axis);
  DrawLineEx({0.f, -t}, {0.f, t}, hairline, axis);

  // The pointer itself is drawn in screen space by systems/board_cursor.h, since
  // the OS cursor is hidden. Its world position is reported numerically in the
  // HUD below, which is what verifies screen->world here: a wrong conversion
  // shows up as a cursor position that disagrees with the landmarks.
}

/*
 * Screen-space readout. Call after EndMode2D.
 *
 * The `transform` line is the important one: it re-derives screen->world both
 * with board::ScreenToWorld and with raylib's GetScreenToWorld2D and reports
 * the disagreement. raylib goes through a matrix inversion, this module uses
 * the closed form, and the two must agree or every screen-space hit test in
 * the app (drop position, click selection, resize handles) is subtly wrong.
 */
inline void DrawBoardDebugHud(const Camera2D& cam,
                              const ec::BoardDebugComponent& cfg) {
  if (!cfg.showHud) return;

  const board::Rect view = VisibleWorldRect(cam);
  const Vector2 cursor = GetMousePosition();

  const Vector2 mine = GetScreenToWorld2D(cursor, cam);
  const board::Vec2 ours =
    board::ScreenToWorldCentre(cursor.x, cursor.y, cam.target.x, cam.target.y,
                               cam.zoom, ViewportW(), ViewportH());
  const float drift = std::hypot(mine.x - ours.x, mine.y - ours.y);

  const std::string text = TextFormat(
    "zoom: %.3f\ntarget: %.1f, %.1f\nvisible: %.1f, %.1f (%.1f x "
    "%.1f)\ncursor: %.1f, %.1f\ngrid: %.0f; world: (%.0f px)\ntransform drift: "
    "%.4f px",
    cam.zoom, cam.target.x, cam.target.y, view.x, view.y, view.width,
    view.height, mine.x, mine.y, board::GridSpacing(cam.zoom),
    board::WorldToPixels(board::GridSpacing(cam.zoom), cam.zoom), drift);

  const float size = kHudFontSize * ui_scale;
  const float spacing = kHudLineGap * ui_scale;
  const Vector2 text_size =
    MeasureTextEx(GetFont(FontWeight::Regular), text.c_str(), size, spacing);
  const Rectangle panel{8.f * ui_scale, 8.f * ui_scale,
                        text_size.x + 14.f * ui_scale,
                        text_size.y + 14.f * ui_scale};

  DrawRectangleRec(panel, Fade(BLACK, 0.75f));
  DrawRectangleLinesEx(panel, 1.f, Fade(WHITE, 0.24f));
  // Drift beyond a pixel means the analytic model no longer matches the
  // renderer, so the readout is flagged rather than quietly wrong.
  DrawTextEx(GetFont(FontWeight::Regular), text.c_str(),
             {panel.x + 7.f * ui_scale, panel.y + 7.f * ui_scale}, size,
             spacing, drift > 1.f ? RED : theme::node_label);

  const char* hint =
    "middle/space: pan   wheel: zoom   equal: reset   "
    "F11: fps   ESC/q: quit";
  const float hint_size = kHintFontSize * ui_scale;
  const Font hint_font = GetFont(FontWeight::Medium, FontSlant::Italic);
  const Vector2 hint_measured = MeasureTextEx(hint_font, hint, hint_size, 1.f);
  DrawTextEx(hint_font, hint,
             {8.f * ui_scale, ViewportH() - hint_measured.y - 10.f * ui_scale},
             hint_size, 1.f, Fade(theme::node_label_dim, 0.90f));
}

}  // namespace referentia::systems
