// systems/perf_panel.h
#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "app/fonts.h"
#include "engine/globals.h"
#include "raylib.h"
#include "systems/frame_history.h"
#include "systems/ui_helpers.h"

namespace referentia::systems {

/**
 * ============================================================================
 * Performance Panel
 * ============================================================================
 *
 * The top-right diagnostics cluster: a frame-rate readout, and optionally a
 * graph of recent frame times directly underneath it.
 *
 * Two independent toggles, because they answer two different questions:
 *
 *   F11  frame rate     "is this running smoothly right now?"
 *   F8   frame graph    "what did the last four seconds actually look like?"
 *
 * A single frame rate number hides exactly the case worth catching. At 60Hz a
 * 90ms stall is 11 dropped frames, and the average still reads a cheerful 58
 * for the whole second it happened in. The graph shows the shape.
 *
 * The graph plots milliseconds, not frames per second, because what matters is
 * distance from the budget, and milliseconds are linear in distance from a
 * fixed line. FPS compresses every bad frame into a spike at the bottom of the
 * chart, which is precisely where you cannot read it.
 *
 * The budget line is drawn in, so "over budget" is visible as a crossing rather
 * than something to be estimated from a number in a corner.
 *
 * All layout below is in screen pixels. MeasureUiText reports *logical* widths
 * and DrawUiText scales by ui_scale, so every measurement is converted once on
 * the way out; mixing the two silently misplaces the unit label at any ui_scale
 * other than 1, which is the value the app actually ships with.
 *
 * ============================================================================
 */

/** Samples plotted across the graph's width; thinned when it is very wide. */
inline int GraphStepFor(float width_px) {
  return std::max(1, static_cast<int>(width_px) / FrameTimeHistory::kCapacity);
}

/**
 * Vertical scale for the graph, in milliseconds.
 *
 * Anchored to the display's frame budget with headroom above it, so the budget
 * line sits at a fixed height and a normal frame is a low flat band. Auto-
 * scaling to the current maximum was the obvious alternative and it is worse:
 * one hitch rescales the axis, the whole visible history squashes, and the
 * graph lies about how large the event was exactly when you are looking at it.
 * The ceiling grows immediately when a frame exceeds it and decays back, so the
 * plot returns to its normal range on its own instead of staying zoomed out.
 */
inline float GraphCeilingMs(const FrameTimeHistory& history, float budget_ms,
                            float& smoothed_ceiling) {
  // Two budgets of headroom: a single dropped frame lands mid-height, clearly
  // over the line but not off the top of the plot.
  const float anchored = budget_ms * 2.f;
  const float observed = history.MaxMs() * 1.15f;
  const float target = std::max(anchored, observed);

  if (target > smoothed_ceiling) {
    smoothed_ceiling = target;
  } else {
    smoothed_ceiling += (target - smoothed_ceiling) * 0.02f;
  }
  return std::max(smoothed_ceiling, 1.f);
}

/**
 * Draws the panel. `ceiling` carries the graph's smoothed vertical scale between
 * frames; pass the same variable on every call.
 *
 * The readout falls back to the frame budget when the buffer is empty, so the
 * first frame after startup shows a number rather than a blank.
 */
inline void DrawPerfPanel(const FrameTimeHistory& history, int refresh_hz,
                          bool draw_readout, bool draw_graph, float& ceiling) {
  if (!draw_readout && !draw_graph) return;

  // Logical -> screen conversion, applied to every measurement exactly once.
  const auto text_w = [&](const std::string& t, float size,
                          FontWeight w = FontWeight::Regular) {
    return MeasureUiText(t, size, w) * ui_scale;
  };

  const float pad = 9.f * ui_scale;
  const float gap = 7.f * ui_scale;
  const float margin = 12.f * ui_scale;

  // Frame budget in ms. Without a known refresh rate there is no real budget to
  // rule a line at, so the axis falls back to 60Hz and the cap is reported as
  // unknown rather than the plot silently asserting a made-up number.
  const float budget_ms =
    refresh_hz > 0 ? 1000.f / static_cast<float>(refresh_hz) : 1000.f / 60.f;
  const bool budget_known = refresh_hz > 0;

  const int fps = GetFPS();
  const float frame_ms = history.empty() ? budget_ms : history.Latest();

  // The frame rate is the headline, not a billboard, but it still has to be
  // readable at a glance: the value uses the Bold face and the secondary text
  // the SemiBold one so the readout keeps its hierarchy at the larger sizes.
  const float value_size = 21.f;
  const float unit_size = 15.f;
  const float metric_size = 15.f;
  const float graph_w = 216.f * ui_scale;
  const float graph_h = 50.f * ui_scale;

  const std::string fps_text = std::to_string(fps);
  const std::string unit_text = "fps";
  // One decimal: the interesting frame times are single-digit milliseconds on a
  // high-refresh display, and an integer readout turns 3.9 and 4.1 into the same
  // "4 ms".
  const std::string ms_text =
    TextFormat("%.1f ms", static_cast<double>(frame_ms));

  const float value_w = text_w(fps_text, value_size, FontWeight::Bold);
  const float unit_w = text_w(unit_text, unit_size, FontWeight::Medium);
  const float ms_w = text_w(ms_text, metric_size, FontWeight::SemiBold);

  // Em box of the value font, in screen px: the tallest row in the panel.
  const float value_line_h =
    MeasureUiTextEx(fps_text, value_size, 0.f, FontWeight::Bold).y * ui_scale;

  // Left group (`NN fps`) and the frame time are pushed to opposite edges, with
  // a gap between them so they never touch as the numbers grow.
  const float readout_w = value_w + 4.f * ui_scale + unit_w +
                          16.f * ui_scale + ms_w;

  // Right-anchored: the panel is laid out backwards from the screen edge so the
  // readout does not jump sideways as the frame rate crosses 9 -> 10 -> 100.
  const float body_w = draw_graph ? std::max(readout_w, graph_w) : readout_w;

  const float graph_top = value_line_h + gap;
  const float body_h = draw_graph ? graph_top + graph_h : value_line_h;

  const float panel_w = body_w + pad * 2.f;
  const float panel_h = body_h + pad * 2.f;
  const float panel_x = static_cast<float>(GetScreenWidth()) - panel_w - margin;
  const float panel_y = margin;
  const float body_x = panel_x + pad;
  const float body_y = panel_y + pad;

  // --- Panel --------------------------------------------------------------
  // Translucent, so the board grid still reads through it. Bordered in the
  // reference square's accent, tying the diagnostics to the board's one
  // highlight colour instead of inventing a second palette.
  DrawRectangleRec(Rectangle{panel_x, panel_y, panel_w, panel_h},
                   WithAlpha(theme::window_background, 235));
  DrawRectangleLinesEx(Rectangle{panel_x, panel_y, panel_w, panel_h}, 1.f,
                       WithAlpha(theme::selection_outline, 70));

  // --- Readout ------------------------------------------------------------
  if (draw_readout) {
    const float small_y = body_y + value_line_h * 0.5f;
    const float unit_y =
      small_y -
      MeasureUiTextEx(unit_text, unit_size, 0.f, FontWeight::Medium).y *
        ui_scale * 0.5f;
    const float ms_y =
      small_y -
      MeasureUiTextEx(ms_text, metric_size, 0.f, FontWeight::SemiBold).y *
        ui_scale * 0.5f;

    DrawUiText(FontWeight::Bold, fps_text, {body_x, body_y}, value_size,
               theme::node_label);
    DrawUiText(FontWeight::Medium, unit_text,
               {body_x + value_w + 4.f * ui_scale, unit_y}, unit_size,
               theme::node_label_dim);

    // The frame time carries the same weight of colour as the headline: it is
    // the reading the graph is plotted from, not an afterthought.
    DrawUiText(FontWeight::SemiBold, ms_text, {body_x + body_w - ms_w, ms_y},
               metric_size, theme::node_label);
  }

  // --- Graph --------------------------------------------------------------
  if (draw_graph) {
    const float top = body_y + graph_top;
    const float floor_y = top + graph_h;
    const float scale_ceiling = GraphCeilingMs(history, budget_ms, ceiling);

    // Budget line. Dashed, because a solid rule reads as a data series and this
    // is a threshold.
    const float budget_y = floor_y - (budget_ms / scale_ceiling) * graph_h;
    if (budget_y > top && budget_y < floor_y) {
      for (float x = body_x; x < body_x + graph_w; x += 6.f * ui_scale) {
        const float seg = std::min(3.f * ui_scale, body_x + graph_w - x);
        DrawLineV({x, budget_y}, {x + seg, budget_y}, WithAlpha(theme::node_label, 70));
      }
      // Label the threshold rather than making the reader infer it from a rule.
      // Suppressed when the cap is unknown: printing a budget line against an
      // assumed 60Hz would assert something the app never established.
      if (budget_known) {
        const std::string label =
          std::to_string(static_cast<int>(std::lround(budget_ms))) + " ms budget";
        const float label_size = 11.f;
        const float label_w = text_w(label, label_size);
        DrawUiText(label, {body_x + graph_w - label_w, top - 11.f * ui_scale},
                   label_size, WithAlpha(theme::node_label_dim, 190));
      }
    }

    // Area plot: one vertical stroke per retained sample, newest on the right.
    // Strokes rather than a spline on purpose. With 240 samples across 200px a
    // spline draws a curve through points it cannot resolve, and it overshoots
    // below the floor on a single-frame dip -- a value that does not exist.
    const int step = GraphStepFor(graph_w);
    const float plot_w = graph_w - 1.f * ui_scale;
    const int last = history.size() - 1;

    for (int i = 0; i < history.size(); i += step) {
      const float ms = history.At(i);
      const float t = static_cast<float>(i) / static_cast<float>(std::max(1, last));
      const float x = body_x + t * plot_w;
      const float h = std::clamp(ms / scale_ceiling, 0.f, 1.f) * graph_h;

      // Over budget is the only colour here that carries meaning, and it is the
      // reason to look, so it gets the accent amber against the usual violet.
      const Color c = ms > budget_ms ? WithAlpha(theme::rotate_handle, 220)
                                     : WithAlpha(theme::selection_outline, 190);
      DrawLineV({x, floor_y}, {x, floor_y - h}, c);
    }
  }
}

}  // namespace referentia::systems
