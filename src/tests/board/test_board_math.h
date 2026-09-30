// tests/board/test_board_math.h
//
// Headless tests for the board world/screen transform (src/board/transform.h).
//
// This is the maths every screen-space interaction depends on: where a dropped
// file lands, what a click selects, how large a resize handle is. The drawing
// cannot be tested here, but the geometry behind it can, and a sign error here
// would be invisible on an empty board.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include "../test_lib.h"
#include "board/transform.h"

namespace bd = referentia::board;

// A representative viewport, plus the zoom extremes the camera clamps to.
constexpr float kW = 1600.f;
constexpr float kH = 900.f;
constexpr float kZoomMin = 0.02f;
constexpr float kZoomMax = 8.f;

TEST(test_visibleWorldRect_CentredOnTarget) {
  const auto r = bd::VisibleWorldRect(100.f, -50.f, 1.f, kW, kH);
  CHECK_NEAR(r.x + r.width * 0.5f, 100.f, 1e-3f);
  CHECK_NEAR(r.y + r.height * 0.5f, -50.f, 1e-3f);
}

TEST(test_visibleWorldRect_HalvesWithZoom) {
  // Zooming in 2x must halve the visible world extent, otherwise landmarks
  // would slide off screen faster than the content does.
  const auto z1 = bd::VisibleWorldRect(0.f, 0.f, 1.f, kW, kH);
  const auto z2 = bd::VisibleWorldRect(0.f, 0.f, 2.f, kW, kH);
  CHECK_NEAR(z2.width, z1.width * 0.5f, 1e-3f);
  CHECK_NEAR(z2.height, z1.height * 0.5f, 1e-3f);
}

TEST(test_visibleWorldRect_PanningDoesNotChangeSize) {
  // Panning must slide the view, not resize it.
  const auto a = bd::VisibleWorldRect(-9999.f, 12345.f, 0.5f, kW, kH);
  const auto b = bd::VisibleWorldRect(0.f, 0.f, 0.5f, kW, kH);
  CHECK_NEAR(a.width, b.width, 1e-3f);
  CHECK_NEAR(a.height, b.height, 1e-3f);
  CHECK(a.x != b.x);
}

TEST(test_visibleWorldRect_CornersMatchScreenCorners) {
  // The visible rect must be exactly the preimage of the viewport: the world
  // point at its top-left corner is the top-left of the screen.
  const float tx = 320.f, ty = -128.f, zoom = 0.37f;
  const auto r = bd::VisibleWorldRect(tx, ty, zoom, kW, kH);
  const auto top_left = bd::ScreenToWorldCentre(0.f, 0.f, tx, ty, zoom, kW, kH);
  const auto bottom_right =
      bd::ScreenToWorldCentre(kW, kH, tx, ty, zoom, kW, kH);
  CHECK_NEAR(top_left.x, r.x, 1e-2f);
  CHECK_NEAR(top_left.y, r.y, 1e-2f);
  CHECK_NEAR(bottom_right.x, r.x + r.width, 1e-2f);
  CHECK_NEAR(bottom_right.y, r.y + r.height, 1e-2f);
}

TEST(test_worldToScreen_TargetLandsAtViewportCentre) {
  // The defining property of Camera2D: whatever the camera is aimed at sits
  // in the middle of the screen.
  for (float zoom : {kZoomMin, 0.5f, 1.f, 3.f, kZoomMax}) {
    const auto p = bd::WorldToScreenCentre(777.f, -333.f, 777.f, -333.f, zoom,
                                           kW, kH);
    CHECK_NEAR(p.x, kW * 0.5f, 1e-2f);
    CHECK_NEAR(p.y, kH * 0.5f, 1e-2f);
  }
}

TEST(test_worldToScreen_OffsetFromTargetScalesWithZoom) {
  // A point 100 world units from the target must be 100*zoom screen px away
  // from the centre. This is the check that catches a dropped *zoom.
  for (float zoom : {kZoomMin, 0.5f, 1.f, 3.f, kZoomMax}) {
    const auto p = bd::WorldToScreenCentre(100.f, 0.f, 0.f, 0.f, zoom, kW, kH);
    CHECK_NEAR(p.x - kW * 0.5f, 100.f * zoom, 1e-2f);
    CHECK_NEAR(p.y - kH * 0.5f, 0.f, 1e-2f);
  }
}

TEST(test_screenToWorld_InvertsWorldToScreen) {
  // Round-trip over the whole zoom range and a spread of camera positions.
  for (float zoom : {kZoomMin, 0.1f, 0.5f, 1.f, 3.f, kZoomMax}) {
    for (const auto target : {0.f, -5000.f, 12345.f}) {
      const auto s = bd::WorldToScreenCentre(42.f, -17.f, target, target, zoom,
                                             kW, kH);
      const auto w = bd::ScreenToWorldCentre(s.x, s.y, target, target, zoom, kW,
                                             kH);
      // Tolerance is loose at high zoom because the forward transform
      // multiplies by it, so the absolute error grows.
      CHECK_NEAR(w.x, 42.f, 1e-2f);
      CHECK_NEAR(w.y, -17.f, 1e-2f);
    }
  }
}

TEST(test_screenToWorld_CentreReturnsTarget) {
  for (float zoom : {kZoomMin, 1.f, kZoomMax}) {
    const auto w = bd::ScreenToWorldCentre(kW * 0.5f, kH * 0.5f, 900.f, -400.f,
                                           zoom, kW, kH);
    CHECK_NEAR(w.x, 900.f, 1e-2f);
    CHECK_NEAR(w.y, -400.f, 1e-2f);
  }
}

TEST(test_screenToWorld_RespectsExplicitOffset) {
  // The un-pinned form is used where the camera offset is not the viewport
  // centre, e.g. while a drag is being converted to a world delta.
  const auto p = bd::ScreenToWorld(100.f, 50.f, 10.f, 20.f, 2.f, 30.f, 40.f);
  CHECK_NEAR(p.x, (100.f - 30.f) / 2.f + 10.f, 1e-4f);
  CHECK_NEAR(p.y, (50.f - 40.f) / 2.f + 20.f, 1e-4f);
}

TEST(test_pixelConversion_AreInverses) {
  for (float zoom : {kZoomMin, 0.5f, 1.f, kZoomMax}) {
    const float world = bd::PixelsToWorld(12.f, zoom);
    CHECK_NEAR(bd::WorldToPixels(world, zoom), 12.f, 1e-3f);
  }
}

TEST(test_pixelConversion_HandleStaysConstantOnScreen) {
  // The reason PixelsToWorld exists: a 10px resize handle must measure 10
  // screen px at every zoom, which means its world size has to shrink as the
  // board zooms in.
  for (float zoom : {kZoomMin, 0.5f, 1.f, kZoomMax}) {
    const float world = bd::PixelsToWorld(10.f, zoom);
    CHECK_NEAR(bd::WorldToPixels(world, zoom), 10.f, 1e-3f);
  }
}

TEST(test_gridSpacing_LandsOn1_2_5Ladder) {
  // Landmarks must sit on round world coordinates or the grid reads as broken:
  // spacing has to be 1, 2 or 5 times a power of ten.
  for (float zoom = kZoomMin; zoom <= kZoomMax; zoom *= 1.01f) {
    const float s = bd::GridSpacing(zoom);
    const float exponent = std::log10(s);
    const float magnitude = std::pow(10.f, std::floor(exponent));
    const float mantissa = s / magnitude;
    const bool on_ladder =
        (std::fabs(mantissa - 1.f) < 1e-3f) || (std::fabs(mantissa - 2.f) < 1e-3f) ||
        (std::fabs(mantissa - 5.f) < 1e-3f);
    CHECK_MSG(on_ladder, Msg("spacing %.6f at zoom %f is off the 1-2-5 ladder", s, zoom));
  }
}

TEST(test_gridSpacing_ChangePerRungIsBounded) {
  // The reason for the 1-2-5 ladder rather than plain decades: consecutive
  // rungs must not jump by 10x, which would look like the grid glitching as
  // you cross a threshold.
  float prev = bd::GridSpacing(kZoomMin);
  for (float zoom = kZoomMin * 1.01f; zoom <= kZoomMax; zoom *= 1.01f) {
    const float s = bd::GridSpacing(zoom);
    CHECK_MSG(s <= prev, Msg("spacing grew from %.0f to %.0f at zoom %f", prev, s, zoom));
    CHECK_MSG(prev / s <= 2.6f,
              Msg("spacing jumped %.2fx at zoom %f", prev / s, zoom));
    prev = s;
  }
}

TEST(test_gridSpacing_StaysReadableAcrossZoomRange) {
  // Outside this band the grid is either a grey smear or a lone dot.
  for (float zoom = kZoomMin; zoom <= kZoomMax; zoom *= 1.1f) {
    const float on_screen = bd::WorldToPixels(bd::GridSpacing(zoom), zoom);
    CHECK_MSG(on_screen >= 8.f, Msg("grid gap %.1f px at zoom %f", on_screen, zoom));
    CHECK_MSG(on_screen <= 900.f, Msg("grid gap %.1f px at zoom %f", on_screen, zoom));
  }
}

TEST(test_gridSpacing_SurvivesDegenerateZoom) {
  // Zero zoom would otherwise divide by zero; a negative one is nonsense but
  // must not produce NaN geometry either.
  CHECK(bd::GridSpacing(0.f) > 0.f);
  CHECK(bd::GridSpacing(-1.f) > 0.f);
}

// --- pointer / viewport containment ---
//
// Gates hiding the OS cursor. If this reports false while the pointer is over
// the window, the user gets a cursor with no crosshair; if it reports true when
// the pointer has left, the crosshair draws outside the window and the user
// cannot reach their other applications.

TEST(test_isInsideViewport_AcceptsInteriorPoints) {
  CHECK(bd::IsInsideViewport(0.f, 0.f, kW, kH));
  CHECK(bd::IsInsideViewport(kW * 0.5f, kH * 0.5f, kW, kH));
  CHECK(bd::IsInsideViewport(kW - 1.f, kH - 1.f, kW, kH));
}

TEST(test_isInsideViewport_RejectsPointsOutside) {
  // Off each edge, in both directions.
  CHECK(!bd::IsInsideViewport(-1.f, kH * 0.5f, kW, kH));
  CHECK(!bd::IsInsideViewport(kW, kH * 0.5f, kW, kH));
  CHECK(!bd::IsInsideViewport(kW * 0.5f, -1.f, kW, kH));
  CHECK(!bd::IsInsideViewport(kW * 0.5f, kH, kW, kH));

  // The negative coordinates are the ones that actually occur: GLFW reports
  // positions outside the client area as negative rather than clamping.
  CHECK(!bd::IsInsideViewport(-32000.f, -32000.f, kW, kH));
}

TEST(test_isInsideViewport_TreatsFarEdgeAsOutside) {
  // Half-open on the right and bottom: the last in-window pixel is width-1, so
  // exactly width is already outside. Getting this wrong hides the cursor in a
  // one-pixel strip along the edge.
  CHECK(!bd::IsInsideViewport(kW, 0.f, kW, kH));
  CHECK(!bd::IsInsideViewport(0.f, kH, kW, kH));
  CHECK(bd::IsInsideViewport(kW - 1.f, 0.f, kW, kH));
}

TEST(test_isInsideViewport_ZeroSizedViewportHasNoInterior) {
  // A minimised or not-yet-sized window must not claim the pointer, or the
  // cursor stays hidden with no window to draw the crosshair in.
  CHECK(!bd::IsInsideViewport(0.f, 0.f, 0.f, 0.f));
  CHECK(!bd::IsInsideViewport(-1.f, -1.f, 0.f, kH));
}
