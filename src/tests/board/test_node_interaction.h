// tests/board/test_node_interaction.h
//
// Headless tests for node pointer interaction: rotated corner positions,
// hit-testing, and the move and rotate drag maths.
//
// This is where the correctness of the feature actually lives, and it is all
// checkable without a GPU -- which matters, because the alternative is a
// rotation bug that only shows up when the image is not axis-aligned, on a
// machine with a mouse, at a zoom level nobody tested. The specific traps these
// lock down:
//
//   • corners and hit-testing must agree, or the handles are drawn where the
//     pointer cannot reach them, and the error scales with zoom
//   • the angle delta must cross the atan2 branch cut, so a drag past the -x
//     side of the centre rotates 20 degrees rather than 340
//   • a grab must not make the node jump, so a drag continues from the rotation
//     the node already had
//
// Nothing here calls raylib, so none of it can pass by accident on a machine
// that happens to have a display.
#pragma once

#include "../test_lib.h"
#include "board/node_interaction.h"
#include "board/transform.h"

namespace ni = referentia::board;

namespace {

constexpr float kEps = 0.001f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = 1.57079632679489661923f;

}  // namespace

// --- corners ---

TEST(test_nodeCorner_axisAlignedSitsOnRectCorners) {
  const ni::Rect r{10.f, 20.f, 100.f, 50.f};

  // With no rotation the corners are the rect's own corners, in the documented
  // clockwise order.
  CHECK_NEAR(ni::NodeCorner(r, 0.f, ni::kCornerTopLeft).x, 10.f, kEps);
  CHECK_NEAR(ni::NodeCorner(r, 0.f, ni::kCornerTopLeft).y, 20.f, kEps);
  CHECK_NEAR(ni::NodeCorner(r, 0.f, ni::kCornerTopRight).x, 110.f, kEps);
  CHECK_NEAR(ni::NodeCorner(r, 0.f, ni::kCornerTopRight).y, 20.f, kEps);
  CHECK_NEAR(ni::NodeCorner(r, 0.f, ni::kCornerBottomRight).x, 110.f, kEps);
  CHECK_NEAR(ni::NodeCorner(r, 0.f, ni::kCornerBottomRight).y, 70.f, kEps);
  CHECK_NEAR(ni::NodeCorner(r, 0.f, ni::kCornerBottomLeft).x, 10.f, kEps);
  CHECK_NEAR(ni::NodeCorner(r, 0.f, ni::kCornerBottomLeft).y, 70.f, kEps);
}

TEST(test_nodeCorner_quarterTurnSwapsAxes) {
  const ni::Rect r{0.f, 0.f, 100.f, 40.f};
  // The top-left corner sits 50 left and 20 up of the centre (50,20). A quarter
  // turn clockwise on screen (y grows downward) takes it to 20 right and 50 up,
  // i.e. (70,-30) -- the offsets swap, but not to 20/50: it is the *long* axis
  // that ends up horizontal.
  const ni::Vec2 c = ni::NodeCorner(r, kHalfPi, ni::kCornerTopLeft);
  CHECK_NEAR(c.x, 70.f, kEps);
  CHECK_NEAR(c.y, -30.f, kEps);
}

TEST(test_nodeCorner_preservesDistanceFromCentre) {
  // A rotated handle must stay on the rect's circumscribed circle, or the
  // handle visibly slides as the node turns.
  const ni::Rect r{-30.f, 10.f, 60.f, 20.f};
  const ni::Vec2 centre = ni::RectCentre(r);
  for (int corner = 0; corner < ni::kCornerCount; ++corner) {
    const ni::Vec2 unrotated = ni::NodeCorner(r, 0.f, corner);
    const float want = std::hypot(unrotated.x - centre.x, unrotated.y - centre.y);
    for (float deg = 0.f; deg < 360.f; deg += 45.f) {
      const ni::Vec2 rotated =
        ni::NodeCorner(r, deg * kPi / 180.f, corner);
      const float got = std::hypot(rotated.x - centre.x, rotated.y - centre.y);
      CHECK_NEAR(got, want, kEps);
    }
  }
}

TEST(test_nodeCorner_fullTurnReturnsToStart) {
  const ni::Rect r{5.f, 7.f, 33.f, 21.f};
  for (int corner = 0; corner < ni::kCornerCount; ++corner) {
    const ni::Vec2 a = ni::NodeCorner(r, 0.f, corner);
    const ni::Vec2 b = ni::NodeCorner(r, 2.f * kPi, corner);
    CHECK_NEAR(a.x, b.x, 0.01f);
    CHECK_NEAR(a.y, b.y, 0.01f);
  }
}

// --- body hit test ---

TEST(test_hitNodeBody_insideAndOutside) {
  const ni::Rect r{0.f, 0.f, 100.f, 100.f};
  CHECK(ni::HitNodeBody({50.f, 50.f}, r, 0.f));
  CHECK(ni::HitNodeBody({0.f, 0.f}, r, 0.f));
  CHECK(ni::HitNodeBody({100.f, 100.f}, r, 0.f));
  CHECK(!ni::HitNodeBody({101.f, 50.f}, r, 0.f));
  CHECK(!ni::HitNodeBody({50.f, -1.f}, r, 0.f));
  CHECK(!ni::HitNodeBody({-1.f, 50.f}, r, 0.f));
}

TEST(test_hitNodeBody_rotationMovesTheHitArea) {
  // A 100x20 bar, rotated a quarter turn so it stands up: now 20 wide and 100
  // tall about the same centre.
  const ni::Rect r{0.f, 0.f, 100.f, 20.f};

  // A point near the bar's left end is inside it unrotated...
  CHECK(ni::HitNodeBody({10.f, 10.f}, r, 0.f));
  // ...and outside once the bar is vertical, because the long axis has turned
  // away from it. A hit test that ignored rotation would say yes here.
  CHECK(!ni::HitNodeBody({10.f, 10.f}, r, kHalfPi));

  // And the converse, which is the one that matters for feel: the point the bar
  // now covers was empty space before the turn.
  CHECK(!ni::HitNodeBody({50.f, -30.f}, r, 0.f));
  CHECK(ni::HitNodeBody({50.f, -30.f}, r, kHalfPi));
}

TEST(test_hitNodeBody_pointInBoundingBoxButOutsideRotatedNode) {
  // Axis-aligned bounding boxes of rotated rects are grossly conservative.
  // A point in the axis-aligned bbox is not necessarily inside the rotated one.
  const ni::Rect r{0.f, 0.f, 100.f, 20.f};
  const float diag = kPi / 4.f;

  // For 45 degrees about (50,10), the axis-aligned bounding box of the bar
  // includes a point far up the left edge of that box -- but that point is
  // outside the bar itself.
  const ni::Vec2 outside_but_in_bbox{-7.f, -7.f};
  const bool in_bbox = outside_but_in_bbox.x >= -20.f &&
                       outside_but_in_bbox.x <= 120.f &&
                       outside_but_in_bbox.y >= -20.f &&
                       outside_but_in_bbox.y <= 40.f;
  CHECK(in_bbox);
  CHECK(!ni::HitNodeBody(outside_but_in_bbox, r, diag));
}

// --- corner hit test ---

TEST(test_hitNodeCorner_findsEachCornerWithinRadius) {
  const ni::Rect r{0.f, 0.f, 100.f, 60.f};
  const float radius = 8.f;
  for (int corner = 0; corner < ni::kCornerCount; ++corner) {
    const ni::Vec2 c = ni::NodeCorner(r, 0.f, corner);
    CHECK_EQ(ni::HitNodeCorner(c, r, 0.f, radius), corner);
  }
}

TEST(test_hitNodeCorner_noneWhenTooFar) {
  const ni::Rect r{0.f, 0.f, 100.f, 60.f};
  CHECK_EQ(ni::HitNodeCorner({50.f, 30.f}, r, 0.f, 8.f), -1);
  // Just outside the radius of the top-left corner.
  CHECK_EQ(ni::HitNodeCorner({-9.f, -9.f}, r, 0.f, 8.f), -1);
}

TEST(test_hitNodeCorner_agreesWithDrawnPosition) {
  // The regression that matters: the renderer draws handles at NodeCorner(...),
  // so if the hit test disagreed by even a pixel at some rotation, the handle
  // would be drawn where the pointer is not. Checked across the full turn
  // because the disagreement would only appear at particular angles.
  const ni::Rect r{12.f, -5.f, 90.f, 40.f};
  const float radius = 10.f;
  for (float deg = 0.f; deg < 360.f; deg += 15.f) {
    const float rad = deg * kPi / 180.f;
    for (int corner = 0; corner < ni::kCornerCount; ++corner) {
      const ni::Vec2 drawn = ni::NodeCorner(r, rad, corner);
      // Nudge towards the rect centre so the point is unambiguously inside this
      // corner's circle and outside the opposite one.
      const ni::Vec2 centre = ni::RectCentre(r);
      const ni::Vec2 probe{drawn.x + (centre.x - drawn.x) * 0.02f,
                           drawn.y + (centre.y - drawn.y) * 0.02f};
      CHECK_EQ(ni::HitNodeCorner(probe, r, rad, radius), corner);
    }
  }
}

TEST(test_hitNodeCorner_returnsOneWhenCornersOverlap) {
  // Narrower than twice the grab radius: every handle is inside every other
  // handle's circle. Exactly one must win, or a grab that started on one corner
  // could change which corner it is rotating about mid-gesture.
  const ni::Rect r{0.f, 0.f, 6.f, 6.f};
  const float radius = 20.f;
  const int hit = ni::HitNodeCorner({3.f, 3.f}, r, 0.f, radius);
  CHECK(hit >= 0);
  CHECK(hit < ni::kCornerCount);
  // Deterministic: the same point resolves to the same corner every time.
  CHECK_EQ(ni::HitNodeCorner({3.f, 3.f}, r, 0.f, radius), hit);
}

// --- angle maths ---

TEST(test_angleDelta_smallTurnsAreSmall) {
  CHECK_NEAR(ni::AngleDelta(0.f, 0.1f), 0.1f, kEps);
  CHECK_NEAR(ni::AngleDelta(0.f, -0.1f), -0.1f, kEps);
  CHECK_NEAR(ni::AngleDelta(1.f, 1.f), 0.f, kEps);
}

TEST(test_angleDelta_crossesTheBranchCut) {
  // The trap: 350 -> 10 degrees is a 20 degree turn, not -340. Without this the
  // node spins almost all the way round when a drag passes the -x side.
  const float from = 350.f * kPi / 180.f;
  const float to = 10.f * kPi / 180.f;
  const float delta = ni::AngleDelta(from, to);
  CHECK_NEAR(delta, 20.f * kPi / 180.f, 0.001f);
}

TEST(test_angleDelta_alwaysShortest) {
  // Whatever the inputs, the result is the short way round.
  for (float a = -kPi; a < kPi; a += 0.37f) {
    for (float b = -kPi; b < kPi; b += 0.53f) {
      const float d = ni::AngleDelta(a, b);
      CHECK(d > -kPi - kEps);
      CHECK(d <= kPi + kEps);
    }
  }
}

TEST(test_normaliseAngle_wrapsIntoRange) {
  CHECK_NEAR(ni::NormaliseAngle(0.f), 0.f, kEps);
  CHECK_NEAR(ni::NormaliseAngle(2.f * kPi), 0.f, 0.01f);
  // Half-open (-pi, pi]. A node turned to exactly 180 degrees must store +pi,
  // not -pi, or continuing the drag rotates the wrong way.
  CHECK_NEAR(ni::NormaliseAngle(3.f * kPi), kPi, 0.001f);
  CHECK_NEAR(ni::NormaliseAngle(kPi), kPi, 0.001f);
  // 350 degrees is -10.
  CHECK_NEAR(ni::NormaliseAngle(350.f * kPi / 180.f), -10.f * kPi / 180.f,
             0.001f);
  // Over a spread of inputs, always in range.
  for (float a = -20.f; a < 20.f; a += 0.41f) {
    const float got = ni::NormaliseAngle(a);
    CHECK(got > -kPi);
    CHECK(got <= kPi);
  }
}

// --- move drag ---

TEST(test_dragDelta_zeroWhenPointerHasNotMoved) {
  const ni::Vec2 grab{10.f, 10.f};
  const ni::Vec2 delta = ni::DragDelta(grab, grab);
  CHECK_NEAR(delta.x, 0.f, kEps);
  CHECK_NEAR(delta.y, 0.f, kEps);
}

TEST(test_dragDelta_isThePointerDisplacementAndNothingElse) {
  // The delta is a plain displacement, with no grab offset folded in. An offset
  // added here would be applied on top of the displacement when the caller moves
  // the grab rect, which is what used to push the image to the far side of the
  // cursor: grabbing near one edge shoved the node twice as far as the pointer
  // had moved, in the direction away from the press.
  const ni::Vec2 now = ni::DragDelta({60.f, 25.f}, {20.f, 30.f});
  CHECK_NEAR(now.x, 40.f, kEps);
  CHECK_NEAR(now.y, -5.f, kEps);
}

TEST(test_dragDelta_keepsTheGrabbedPointUnderThePointer) {
  // The offset between the grabbed point and the node's centre is not carried
  // explicitly: a translation moves both by the same amount, so it cancels. What
  // keeps the grabbed point under the pointer is that the delta is measured from
  // the grab, not from the last frame.
  const ni::Rect start{0.f, 0.f, 200.f, 100.f};
  const ni::Vec2 pointer_grab{35.f, 20.f};  // not the centre
  const ni::Vec2 grabbed_point{35.f, 20.f};
  const ni::Vec2 pointer_end{95.f, 80.f};

  const ni::Rect moved = ni::MoveBy(start, ni::DragDelta(pointer_end, pointer_grab));
  // Where the grabbed point ended up, and where the pointer is: the same place.
  const ni::Vec2 moved_point{grabbed_point.x + (moved.x - start.x),
                             grabbed_point.y + (moved.y - start.y)};
  CHECK_NEAR(moved_point.x, pointer_end.x, kEps);
  CHECK_NEAR(moved_point.y, pointer_end.y, kEps);
  // And the node did not jump so the centre is under the cursor instead.
  const ni::Vec2 centre = ni::RectCentre(moved);
  CHECK(std::abs(centre.x - pointer_end.x) > kEps);
  CHECK(std::abs(centre.y - pointer_end.y) > kEps);
}

TEST(test_moveBy_translatesWithoutResizing) {
  const ni::Rect r{10.f, 10.f, 40.f, 20.f};
  const ni::Rect moved = ni::MoveBy(r, {5.f, -3.f});
  CHECK_NEAR(moved.x, 15.f, kEps);
  CHECK_NEAR(moved.y, 7.f, kEps);
  CHECK_NEAR(moved.width, 40.f, kEps);
  CHECK_NEAR(moved.height, 20.f, kEps);
}

TEST(test_moveBy_doesNotDisturbTheCentreRelativeToTheGrab) {
  // A real drag is many small steps. If the delta were recomputed from the
  // node's *current* position each frame, the error would compound: the node
  // would drift further than the pointer, which is the classic "the thing
  // runs away from the cursor" bug. The delta is measured from the grab each
  // frame and applied to the *grab* rect, so stepping to the end must land
  // exactly where one big step would.
  const ni::Rect start{0.f, 0.f, 20.f, 20.f};
  const ni::Vec2 pointer_grab{5.f, 5.f};
  const ni::Vec2 pointer_end{35.f, 25.f};

  ni::Rect stepped = start;
  for (int i = 1; i <= 10; ++i) {
    const float t = static_cast<float>(i) / 10.f;
    const ni::Vec2 pointer{pointer_grab.x + (pointer_end.x - pointer_grab.x) * t,
                           pointer_grab.y + (pointer_end.y - pointer_grab.y) * t};
    // Applied to the *grab* rect each frame, not accumulated onto the last one.
    stepped = ni::MoveBy(start, ni::DragDelta(pointer, pointer_grab));
  }

  const ni::Rect one_step = ni::MoveBy(start, ni::DragDelta(pointer_end, pointer_grab));
  CHECK_NEAR(stepped.x, one_step.x, kEps);
  CHECK_NEAR(stepped.y, one_step.y, kEps);
}

TEST(test_dragDelta_tenStepsMatchOneStep) {
  // The same property, asserted on the delta itself rather than the rect, so a
  // failure points at the arithmetic instead of at MoveBy.
  const ni::Vec2 grab{12.f, -4.f};
  const ni::Vec2 end{90.f, 60.f};

  const ni::Vec2 one = ni::DragDelta(end, grab);
  ni::Vec2 accumulated{0.f, 0.f};
  for (int i = 1; i <= 10; ++i) {
    const float t = static_cast<float>(i) / 10.f;
    const ni::Vec2 p{grab.x + (end.x - grab.x) * t, grab.y + (end.y - grab.y) * t};
    const ni::Vec2 step = ni::DragDelta(p, grab);
    // Each frame's delta is absolute-from-grab, so the last one is the answer.
    accumulated = step;
  }
  CHECK_NEAR(accumulated.x, one.x, kEps);
  CHECK_NEAR(accumulated.y, one.y, kEps);
}

// --- rotate drag ---

TEST(test_rotationForCornerDrag_noSwingKeepsTheStartRotation) {
  const ni::Vec2 centre{50.f, 50.f};
  const ni::Vec2 corner = ni::NodeCorner({0.f, 0.f, 100.f, 100.f}, 0.f,
                                         ni::kCornerTopLeft);
  const float start = 0.7f;
  // The pointer has not moved from where the corner was.
  CHECK_NEAR(ni::RotationForCornerDrag(centre, corner,
                                       ni::AngleTo(centre, corner), start),
             start, kEps);
}

TEST(test_rotationForCornerDrag_continuesFromTheStartRotation) {
  // The regression: a grab on an already-rotated node must not snap it to a
  // canonical angle. Rotating by a known swing from a non-zero start has to
  // come back as start + swing.
  const ni::Vec2 centre{0.f, 0.f};
  const ni::Rect r{-50.f, -50.f, 100.f, 100.f};
  const float start = 0.9f;

  const ni::Vec2 corner = ni::NodeCorner(r, start, ni::kCornerTopRight);
  const float grab_angle = ni::AngleTo(centre, corner);

  // Swing the pointer 30 degrees anticlockwise around the centre.
  const float swing = 30.f * kPi / 180.f;
  const ni::Vec2 pointer{60.f * std::cos(grab_angle + swing),
                         60.f * std::sin(grab_angle + swing)};

  const float got =
    ni::RotationForCornerDrag(centre, pointer, grab_angle, start);
  CHECK_NEAR(got, ni::NormaliseAngle(start + swing), 0.001f);
}

TEST(test_rotationForCornerDrag_cornerFollowsThePointer) {
  // The defining property: after the drag, the grabbed corner is pointing at
  // the pointer. Tested for all four corners and several start angles, because
  // a per-corner sign error is the obvious way to get this wrong.
  for (int corner = 0; corner < ni::kCornerCount; ++corner) {
    for (float start_deg = 0.f; start_deg < 360.f; start_deg += 90.f) {
      const float start = start_deg * kPi / 180.f;
      const ni::Rect r{-60.f, -30.f, 120.f, 60.f};
      const ni::Vec2 centre = ni::RectCentre(r);

      const ni::Vec2 grabbed = ni::NodeCorner(r, start, corner);
      const float radius = std::hypot(grabbed.x - centre.x, grabbed.y - centre.y);
      const float grab_angle = ni::AngleTo(centre, grabbed);

      const float swing = 0.8f;
      const ni::Vec2 pointer{centre.x + radius * std::cos(grab_angle + swing),
                             centre.y + radius * std::sin(grab_angle + swing)};

      const float rotation =
        ni::RotationForCornerDrag(centre, pointer, grab_angle, start);

      // The corner's angle after the drag must equal where the pointer is.
      const float corner_angle_after =
        ni::AngleTo(centre, ni::NodeCorner(r, rotation, corner));
      const float pointer_angle = ni::AngleTo(centre, pointer);
      CHECK_NEAR(ni::AngleDelta(pointer_angle, corner_angle_after), 0.f, 0.001f);
    }
  }
}

TEST(test_rotationForCornerDrag_doesNotMoveTheNode) {
  // Rotation is about the centre, so the rect is untouched and the node stays
  // exactly where it was dropped. Asserted because a system that recomputed the
  // rect from rotated corners would drift the image on every frame of the drag.
  const ni::Rect r{17.f, -3.f, 80.f, 40.f};
  const ni::Rect after = ni::MoveBy(r, {0.f, 0.f});
  CHECK_NEAR(after.x, r.x, kEps);
  CHECK_NEAR(after.y, r.y, kEps);
}

TEST(test_rotationForCornerDrag_preservesSizeAndAspect) {
  // The reason rotation is stored separately from the rect: a 16:9 node that
  // turns must still be 16:9, not squeezed into its bounding box.
  const ni::Rect r{0.f, 0.f, 160.f, 90.f};
  const ni::Vec2 centre = ni::RectCentre(r);
  const float start = 0.3f;
  const ni::Vec2 grabbed = ni::NodeCorner(r, start, ni::kCornerTopLeft);
  const float radius = std::hypot(grabbed.x - centre.x, grabbed.y - centre.y);
  const float grab_angle = ni::AngleTo(centre, grabbed);
  const float rotation = ni::RotationForCornerDrag(
    centre, {centre.x + radius * std::cos(1.9f), centre.y + radius * std::sin(1.9f)},
    grab_angle, start);

  // The stored rect -- the thing that decides how the texture is drawn -- is
  // still 16:9.
  CHECK_NEAR(r.width / r.height, 16.f / 9.f, 0.001f);
  // And the drawn corners keep their radius, so the image is not distorted.
  const ni::Vec2 after = ni::NodeCorner(r, rotation, ni::kCornerTopLeft);
  CHECK_NEAR(std::hypot(after.x - centre.x, after.y - centre.y), radius, 0.01f);
}
