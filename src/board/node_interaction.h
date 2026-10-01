// board/node_interaction.h
//
// Pointer interaction for placed nodes: picking, dragging to move, and dragging
// a corner to rotate.
//
// Raylib-free and header-only for the same reason as board/transform.h: this is
// where the geometry is decided, it is pure arithmetic, and the only way to be
// sure a rotation lands where the user aimed is to assert on numbers rather than
// to squint at a screen. Every function here is unit tested in
// tests/board/test_node_interaction.h; the system that feeds it raylib's mouse
// state is not, because that part is a translation with nothing to decide.
//
// The one rule that shapes the whole file: a node's rect is always its
// *unrotated* box, and `rotation` is a separate angle about that box's centre.
// Baking the rotation into the rect would make the stored size meaningless,
// because the bounding box of a rotated rectangle is not the rectangle. Keeping
// them separate means the image keeps its aspect ratio when it turns, which is
// the entire point of a reference board -- a rotated 16:9 screenshot must still
// look like a 16:9 screenshot, not a letterboxed mess.
#pragma once

#include <cmath>

#include "board/transform.h"

namespace referentia::board {

/**
 * What a held pointer is doing to a node.
 *
 * One grab at a time across the whole board, not one per node. Two nodes cannot
 * follow one pointer, and a per-node flag would have to be reconciled every frame
 * to work out which one wins; a single active grab makes "who owns this drag"
 * a field rather than a query.
 *
 * Rotate is a corner held between finger and cursor: the corner tracks the
 * pointer. Spin is a free rotation with no corner held -- the pointer can be
 * anywhere, and the node turns by however far the pointer has swung around the
 * centre. They are different modes rather than one mode with a corner that may be
 * absent because the anchor differs: a held corner anchors on the corner, a free
 * rotation anchors on the pointer at the press, and the two produce visibly
 * different motion from the first pixel of the drag.
 */
enum class GrabMode {
  None,
  Move,
  Rotate,
  Spin,
};

/**
 * The corners of a node, in a fixed order.
 *
 * Order is fixed and meaningful, because the grab records *which* corner was
 * taken and the rotation maths needs the same corner index to match on the other
 * side of the drag:
 *
 *     0 top-left     1 top-right
 *     3 bottom-left  2 bottom-right
 *
 * Laid out the way a 2x2 matrix reads, going clockwise, so corner k is at
 * `(-1, -1), (1, -1), (1, 1), (-1, 1)` scaled by the half-extents. y grows
 * downward in board space, matching screen coordinates.
 */
enum NodeCorner {
  kCornerTopLeft = 0,
  kCornerTopRight = 1,
  kCornerBottomRight = 2,
  kCornerBottomLeft = 3,
  kCornerCount = 4,
};

/** Centre of a rect. Named because it is needed in nearly every function here. */
inline Vec2 RectCentre(const Rect& r) {
  return {r.x + r.width * 0.5f, r.y + r.height * 0.5f};
}

/** Rotates `p` about `centre` by `radians`, anticlockwise on screen. */
inline Vec2 RotateAbout(Vec2 centre, Vec2 p, float radians) {
  if (radians == 0.f) return p;
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  const float dx = p.x - centre.x;
  const float dy = p.y - centre.y;
  return {centre.x + dx * c - dy * s, centre.y + dx * s + dy * c};
}

/** The unrotated position of corner `corner` on `r`. */
inline Vec2 CornerOrigin(const Rect& r, int corner) {
  const float left = (corner == kCornerTopLeft || corner == kCornerBottomLeft)
                         ? r.x
                         : r.x + r.width;
  const float top = (corner == kCornerTopLeft || corner == kCornerTopRight)
                        ? r.y
                        : r.y + r.height;
  return {left, top};
}

/**
 * Where corner `corner` actually is, with the node's rotation applied.
 *
 * This is the single source of truth for both drawing the handles and
 * hit-testing them. If the renderer used one arrangement and the hit test
 * another, the handles would be drawn somewhere the pointer does not reach, and
 * since the handles are drawn in world space the error would scale with zoom.
 */
inline Vec2 NodeCorner(const Rect& r, float rotation, int corner) {
  return RotateAbout(RectCentre(r), CornerOrigin(r, corner), rotation);
}

/**
 * Whether a world point is inside a node, honouring its rotation.
 *
 * Tested by rotating the point back by the negated angle rather than by rotating
 * the four corners and doing a polygon test: the inverse rotation turns the
 * rotated box into the unrotated rect, so it reduces to two comparisons and
 * cannot get the winding wrong. Costs one sin/cos pair, which is nothing next
 * to the four polygon edges it replaces.
 */
inline bool HitNodeBody(Vec2 point, const Rect& r, float rotation) {
  const Vec2 local = RotateAbout(RectCentre(r), point, -rotation);
  return local.x >= r.x && local.x <= r.x + r.width && local.y >= r.y &&
         local.y <= r.y + r.height;
}

/**
 * The nearest corner within `radius` of `point`, or -1 for none.
 *
 * Returns the single closest corner rather than every corner within range. When
 * two handles are closer together than the grab radius -- which happens on any
 * node narrower than twice the radius, and on every node when zoomed far out --
 * both are inside the circle, and a grab that started on one corner and ended up
 * rotating about the other would snap the node to a new angle mid-gesture. Only
 * one corner can own a grab, so only one is tested.
 *
 * Ties resolve to the lower index, which is deterministic; a tie means the point
 * is almost exactly between two handles, where either answer is defensible and
 * flip-flopping between them is not.
 */
inline int HitNodeCorner(Vec2 point, const Rect& r, float rotation,
                         float radius) {
  int best = -1;
  float best_dist = radius * radius;
  for (int i = 0; i < kCornerCount; ++i) {
    const Vec2 corner = NodeCorner(r, rotation, i);
    const float dx = corner.x - point.x;
    const float dy = corner.y - point.y;
    const float dist = dx * dx + dy * dy;
    if (dist <= best_dist) {
      best_dist = dist;
      best = i;
    }
  }
  return best;
}

/** Angle from `centre` to `p`, in radians, atan2 convention (y down). */
inline float AngleTo(Vec2 centre, Vec2 p) {
  return std::atan2(p.y - centre.y, p.x - centre.x);
}

/**
 * Shortest signed difference between two angles, in (-pi, pi].
 *
 * The subtraction alone is not enough. Rotating from 350 degrees to 10 degrees
 * is a 20 degree turn, but the raw difference is -340, and using that directly
 * would spin the node almost all the way round instead of nudging it. Every
 * angle comparison in a drag needs this, because the pointer crosses the atan2
 * branch cut at the -x axis constantly -- any drag that passes to the left of
 * the centre does.
 */
inline float AngleDelta(float from, float to) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  constexpr float kPi = 3.14159265358979323846f;
  float d = std::fmod(to - from + kPi, kTwoPi);
  if (d < 0.f) d += kTwoPi;
  d -= kPi;
  // The subtraction lands exactly on -pi for a half-turn, which is the one
  // value outside the documented half-open range. Leaving it there means
  // NormaliseAngle(3pi) returns -pi, so a node turned to exactly 180 degrees
  // stores -180 and then rotates the "wrong" way when the drag continues past
  // it. Both spellings are the same angle; +pi is the one in range.
  if (d <= -kPi) d += kTwoPi;
  return d;
}

/** Normalises any angle into (-pi, pi]. */
inline float NormaliseAngle(float radians) {
  return AngleDelta(0.f, radians);
}

/**
 * How far a node has been dragged, as a world-space delta.
 *
 * Measured from the pointer position at the grab, not from the pointer position
 * last frame. Combined with MoveBy applied to the *grab* rect, that makes a drag
 * absolute rather than incremental: however many frames and however small the
 * steps, the node lands under exactly the pointer displacement the user made.
 * Deriving the step from the node's current position instead would let the node
 * chase the pointer, since any drift becomes part of the next step's input and
 * the error compounds.
 *
 * There is deliberately no "offset from the grabbed point to the node's centre"
 * argument. A translation moves the grabbed point by the same amount as the
 * centre, so that offset cancels exactly, and carrying it here is what used to
 * shove the image to the far side of the cursor: the caller applied the returned
 * value to the grab rect, so the offset was applied twice -- once as the delta
 * and once as the displacement it was already folded into. The grabbed point
 * stays under the pointer because the delta is absolute, not because of an
 * offset that has to be remembered and reapplied.
 */
inline Vec2 DragDelta(Vec2 pointer_now, Vec2 pointer_grab) {
  return {pointer_now.x - pointer_grab.x, pointer_now.y - pointer_grab.y};
}

/** Applies a drag delta to a node's rect, leaving its rotation alone. */
inline Rect MoveBy(Rect r, Vec2 delta) {
  r.x += delta.x;
  r.y += delta.y;
  return r;
}

/**
 * The rotation a corner drag is asking for.
 *
 * The grabbed corner is made to follow the pointer, which is the only rule that
 * feels right: the user is holding a specific point on the image and expects that
 * point to track the pointer. The alternative -- the image's axis following the
 * pointer -- makes the corner slip out from under the cursor, because the corner
 * sits at a fixed radius from the centre and rotating about the centre cannot
 * move it along that radius.
 *
 * So the answer is "the rotation the node had when the grab started, plus
 * however far the pointer has swung around the centre since". Both halves are
 * needed: dropping the starting rotation would snap the node to a canonical
 * angle the instant a corner was grabbed, and dropping the swing would make
 * rotation impossible.
 *
 * `grab_angle` is the centre-to-corner angle captured at the grab, and
 * `start_rotation` the node's rotation at that moment. The difference is taken
 * through AngleDelta because the pointer crosses the atan2 branch cut at the
 * -x axis constantly -- any drag that passes to the left of the centre does --
 * and the raw difference there is nearly a full turn in the wrong direction.
 *
 * The rect is not touched: rotating is about the centre, and the centre is
 * unchanged, so a rotated node stays exactly where it was dropped.
 */
inline float RotationForCornerDrag(Vec2 centre, Vec2 pointer_now,
                                   float grab_angle, float start_rotation) {
  const float swing = AngleDelta(grab_angle, AngleTo(centre, pointer_now));
  return NormaliseAngle(start_rotation + swing);
}

/**
 * The rotation a free drag -- one with no corner held -- is asking for.
 *
 * Same "start rotation plus swing since the grab" rule as the corner drag, with
 * the anchor taken from the pointer instead of from a corner: the angle from the
 * centre to wherever the pointer was when the button went down. The two agree at
 * the instant of the press, which is the point: a press on the body must not turn
 * the image by the difference between the pointer's angle and some corner's.
 * After that they diverge, and correctly so -- a held corner has to stay under
 * the cursor, while a free rotation has nothing to stay under it and only owes
 * the pointer its swing.
 *
 * The pointer swinging over the centre is the one case that has no answer: the
 * angle is undefined there, and the instant before and the instant after are a
 * half turn apart. So the centre is not this function's problem to solve -- the
 * caller skips the update while the pointer is inside a dead zone around the
 * centre, which leaves the node holding its last angle until the pointer is out
 * the other side. Feeding a pointer at the centre in here would make the node
 * spin wildly.
 *
 * As with the corner drag, the rect is untouched: the node turns about its centre
 * and stays exactly where it was.
 */
inline float RotationForFreeDrag(Vec2 centre, Vec2 pointer_now,
                                 Vec2 pointer_at_grab, float start_rotation) {
  const float swing =
      AngleDelta(AngleTo(centre, pointer_at_grab), AngleTo(centre, pointer_now));
  return NormaliseAngle(start_rotation + swing);
}

}  // namespace referentia::board
