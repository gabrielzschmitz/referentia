// systems/image_nodes.h
//
// Draws the board's image nodes and runs their pointer interaction. Call the
// update from the update phase and the draw from inside BeginMode2D/EndMode2D,
// between the world landmarks and the screen-space UI.
//
// A node's rect is world space and its texture is a GPU object, so this is the
// boundary: it reads the raylib-free components and issues draw calls. The
// geometry -- rotated corner positions, hit testing, and the move and rotate
// drag maths -- lives in board/node_interaction.h and is unit tested; the
// translation from raylib's mouse state into those functions is not, because it
// is a conversion with nothing left to decide.
//
// Two passes over view<TagNode, NodeTransform, ImageNodeComponent>(), so a
// second node type is a second view in its own system rather than a type switch
// here.
#pragma once

#include "board/node_interaction.h"
#include "board/transform.h"
#include "components/camera.h"
#include "components/node.h"
#include "components/tags.h"
#include "components/ui.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/logger.h"
#include "raylib.h"

namespace referentia::systems {

namespace ec = referentia::components;
namespace engine = motrix::engine;

namespace board_ns = referentia::board;

/**
 * ============================================================================
 * Node Interaction
 * ============================================================================
 *
 * What a held left button is doing to a node. One grab at a time across the
 * whole board, held in the app state rather than in a component, because it is
 * transient input state rather than a property of the node: a node does not
 * know it is being dragged, it just finds its transform changed.
 *
 * Deliberately not integrated with the camera's panning. A node drag moves the
 * node, a camera drag moves the world under it, and mixing them means every drag
 * is ambiguous. Left button belongs to the nodes; panning stays on the middle
 * button and space+left, which is what the camera system already offers.
 */
struct NodeGrab {
  /** None, or the mode the current drag is in. */
  board_ns::GrabMode mode = board_ns::GrabMode::None;

  /** The dragged node, so only that entity is written to each frame. */
  engine::Entity entity = engine::INVALID_ENTITY;

  /** Which corner is held, for a rotate. Unused for a move. */
  int corner = -1;

  /** Pointer position when the grab began, in world space. */
  board_ns::Vec2 pointer_at_grab{0.f, 0.f};

  /** The node's rect when the grab began, so a move is never cumulative. */
  board_ns::Rect rect_at_grab{0.f, 0.f, 0.f, 0.f};

  /** The node's rotation when the grab began, for a rotate. */
  float rotation_at_grab = 0.f;

  /** Centre-to-corner angle when the grab began, for a rotate. */
  float corner_angle_at_grab = 0.f;

  bool active() const { return mode != board_ns::GrabMode::None; }

  void clear() { *this = NodeGrab{}; }
};

/**
 * Grab radius of a corner handle, in screen pixels.
 *
 * A screen-space size, converted to world units per frame, for the same reason
 * the board cursor is drawn in screen space: a handle sized in world units would
 * be a giant target when zoomed in and an unhittable speck when zoomed out, and
 * the user would have to zoom to a specific level to use the node at all.
 *
 * 9px is a little over the 7px Material minimum for a pointer target, and is
 * measured as a radius so a 18px-wide circle is grabbable anywhere across it.
 */
constexpr float kHandleRadiusPx = 9.f;

/**
 * Whether the pointer is over any open panel, which suppresses node drags.
 *
 * Duplicated from the camera system's own check rather than shared, because
 * sharing it would mean ui_helpers.h in this header, and ui_helpers.h calls
 * GetFont, which is declared in app/fonts.h. A systems/ header pulling in the
 * app's font table to ask a question about rectangles inverts the dependency
 * direction: the renderer would not compile without the app.
 *
 * The check is a bounding box of the window, not a per-widget test, so it is
 * conservative: a press in the dead space inside a panel suppresses the drag.
 * That is the right way to be wrong, since a click there is not a gesture the
 * user is making about the board.
 */
inline bool PointerOverUi(engine::ECS& ecs) {
  bool over = false;
  const Vector2 mouse = GetMousePosition();
  ecs.group_view<ec::UIWindowComponent>(
    [&](engine::Entity, ec::UIWindowComponent& win) {
      if (win.minimized) return;
      const float sx = ui_scale;
      const Rectangle r{win.position.x * sx, win.position.y * sx, win.width * sx,
                        win.height * sx};
      if (CheckCollisionPointRec(mouse, r)) over = true;
    });
  return over;
}

/**
 * Applies pointer input to the nodes: picks one up, drags it, or rotates it.
 *
 * The order within a frame is the whole of the interaction:
 *
 *   1. an existing grab is applied first and unconditionally, so a node keeps
 *      following the pointer even when the pointer leaves the node, or leaves
 *      the window entirely. Dropping the grab because the cursor wandered off
 *      would make it impossible to move a node to a place it does not cover.
 *   2. otherwise a press picks the topmost node under the pointer, handles
 *      before body.
 *
 * Handles are tested before bodies because a handle sits on the node's corner,
 * which is by definition inside the body. Testing the body first would make the
 * handles unreachable: every corner press would be claimed as a body drag.
 *
 * The topmost node is the last one the ECS view yields, not the first, since
 * nodes are created in import order and drawn in that same order -- so the most
 * recently imported image is the one on top, which is what a user stacking
 * reference images expects.
 */
inline void UpdateImageNodes(engine::ECS& ecs, NodeGrab& grab,
                             const Camera2D& cam) {
  const bool pointer_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  const Vector2 mouse = GetMousePosition();
  const board_ns::Vec2 pointer = board_ns::ScreenToWorldCentre(
      mouse.x, mouse.y, cam.target.x, cam.target.y, cam.zoom,
      static_cast<float>(GetScreenWidth()),
      static_cast<float>(GetScreenHeight()));

  if (grab.active()) {
    if (!pointer_down) {
      // Released: the grab ends here, and nothing is written on the final frame.
      // Ending without applying means a click that never moved the pointer does
      // not nudge the node by a rounding error.
      grab.clear();
      return;
    }

    ecs.view<ec::TagNode, ec::NodeTransform, ec::ImageNodeComponent>(
      [&](engine::Entity entity, ec::TagNode&, ec::NodeTransform& transform,
          ec::ImageNodeComponent&) {
        if (entity != grab.entity) return;
        if (grab.mode == board_ns::GrabMode::Move) {
          // Absolute from the grab, applied to the grab rect, so the grabbed point
          // stays under the pointer for the whole drag and never compounds.
          transform.rect = board_ns::MoveBy(
              grab.rect_at_grab,
              board_ns::DragDelta(pointer, grab.pointer_at_grab));
        } else {
          transform.rotation = board_ns::RotationForCornerDrag(
              board_ns::RectCentre(transform.rect), pointer,
              grab.corner_angle_at_grab, grab.rotation_at_grab);
        }
      });
    return;
  }

  // No grab in progress. A press over a panel belongs to the panel.
  if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || PointerOverUi(ecs)) return;

  const float pick_radius = board_ns::PixelsToWorld(kHandleRadiusPx, cam.zoom);

  // Handles first, across all nodes, so a handle on a node underneath still
  // wins against the body of one drawn on top: a handle is a small target and
  // losing it under an overlapping image is maddening.
  engine::Entity best_entity = engine::INVALID_ENTITY;
  int best_corner = -1;
  float best_dist = pick_radius * pick_radius;
  board_ns::Rect best_rect{0.f, 0.f, 0.f, 0.f};

  ecs.view<ec::TagNode, ec::NodeTransform, ec::ImageNodeComponent>(
    [&](engine::Entity entity, ec::TagNode&, ec::NodeTransform& transform,
        ec::ImageNodeComponent& node) {
      if (node.texture_id == 0 || transform.rect.width <= 0.f ||
          transform.rect.height <= 0.f)
        return;
      const board_ns::Rect r = transform.rect;
      for (int c = 0; c < board_ns::kCornerCount; ++c) {
        const board_ns::Vec2 corner = board_ns::NodeCorner(r, transform.rotation, c);
        const float dx = corner.x - pointer.x;
        const float dy = corner.y - pointer.y;
        const float dist = dx * dx + dy * dy;
        if (dist <= best_dist) {
          best_dist = dist;
          best_corner = c;
          best_entity = entity;
          best_rect = r;
        }
      }
    });

  if (best_corner >= 0) {
    grab.mode = board_ns::GrabMode::Rotate;
    grab.corner = best_corner;
    grab.entity = best_entity;
    grab.pointer_at_grab = pointer;
    grab.rect_at_grab = best_rect;

    // Both anchors come from the node as it is *now*, rotation included. The
    // corner angle is taken at the node's real rotation and the starting
    // rotation recorded alongside, because a zeroed starting rotation would snap
    // the image to a canonical angle the moment the corner is grabbed.
    ecs.view<ec::TagNode, ec::NodeTransform, ec::ImageNodeComponent>(
      [&](engine::Entity entity, ec::TagNode&, ec::NodeTransform& transform,
          ec::ImageNodeComponent&) {
        if (entity != best_entity) return;
        grab.rotation_at_grab = transform.rotation;
        grab.corner_angle_at_grab = board_ns::AngleTo(
            board_ns::RectCentre(transform.rect),
            board_ns::NodeCorner(transform.rect, transform.rotation, best_corner));
      });
    return;
  }

  // No handle: the press may be on a node's body, which moves it.
  engine::Entity body_entity = engine::INVALID_ENTITY;
  board_ns::Rect body_rect{0.f, 0.f, 0.f, 0.f};
  ecs.view<ec::TagNode, ec::NodeTransform, ec::ImageNodeComponent>(
    [&](engine::Entity entity, ec::TagNode&, ec::NodeTransform& transform,
        ec::ImageNodeComponent& node) {
      if (node.texture_id == 0) return;
      if (!board_ns::HitNodeBody(pointer, transform.rect, transform.rotation))
        return;
      // Last one wins: later nodes are drawn on top, so the topmost is picked.
      body_entity = entity;
      body_rect = transform.rect;
    });

  if (body_entity == engine::INVALID_ENTITY) return;

  grab.mode = board_ns::GrabMode::Move;
  grab.corner = -1;
  grab.entity = body_entity;
  grab.pointer_at_grab = pointer;
  grab.rect_at_grab = body_rect;
}

/**
 * Draws every image node, a border around it, and a circle on each corner.
 *
 * The border and the handles are what make this a reference tool rather than a
 * picture viewer. A reference image is almost never the thing being drawn: it
 * sits under or beside something being drawn on top of it, and without an edge
 * there is no telling where the reference ends. The four circles mark the
 * corners you can grab, and drawing them rotated with the image is what makes
 * rotation discoverable at all -- handles that stayed axis-aligned while the
 * image turned would be pointing at empty space.
 */
inline void DrawImageNodes(engine::ECS& ecs, const Camera2D& cam) {
  // Handles and border are sized in screen pixels, so they need the zoom to
  // convert to world units. Inside BeginMode2D a world-unit size is multiplied
  // by the zoom on the way out, which cancels the conversion.
  const float handle_radius = board_ns::PixelsToWorld(kHandleRadiusPx, cam.zoom);
  const float line_thickness = board_ns::PixelsToWorld(1.f, cam.zoom);

  // The view is over all four types, and the tag comes first in the parameter
  // list to match: the renderer selects on TagNode, so a node that somehow lost
  // it would not be drawn even with a valid transform and texture.
  ecs.view<ec::TagNode, ec::NodeTransform, ec::ImageNodeComponent>(
    [&](engine::Entity, ec::TagNode&, ec::NodeTransform& transform,
        ec::ImageNodeComponent& node) {
      // An empty rect or a null id means the image failed to load and a node was
      // created anyway (see app/image_import.h). Drawing nothing is correct
      // here; the failure is already reported by the import layer, and drawing a
      // placeholder box instead would suggest the image arrived.
      if (node.texture_id == 0 || transform.rect.width <= 0.f ||
          transform.rect.height <= 0.f)
        return;

      const board_ns::Rect r = transform.rect;
      const board_ns::Vec2 centre = board_ns::RectCentre(r);

      // DrawTexturePro, not DrawTextureV, and the difference is the whole point
      // of the node having a rect.
      //
      // DrawTextureV would draw the texture at one pixel per world unit and
      // ignore r.width and r.height entirely, so the placement maths in
      // board/image_source.h -- uniform scale down, the 2000-unit cap -- would
      // have no effect on anything the user can see, and a 6000px photo would
      // simply be enormous.
      //
      // It also cannot be used with a reconstructed Texture2D. DrawTextureV
      // passes source = {0, 0, texture.width, texture.height}, and with those
      // left at zero the texcoords are 0/0 and the quad is degenerate, so
      // nothing is drawn at all -- the node would be invisible rather than
      // mis-sized, which is why the size bug alone would not have been noticed
      // as a size bug.
      //
      // DrawTexturePro divides the source rect by texture.width/height to get
      // its texcoords, so those two fields have to be the real texture size.
      // The importer knows it (it loaded the image) and already stores it on the
      // node, so it is read from there rather than from a Texture2D: a full
      // Texture2D in the component would put a GL type in a header that the
      // headless test target includes.
      //
      // The rotation pivot is dest.x/dest.y, and the quad's top-left is placed at
      // `dest - origin` in *both* of raylib's branches, rotated or not. So the
      // pivot goes at the node's centre and the origin at the same half-size:
      // the pair makes the quad's top-left land on the rect's top-left and turn
      // about the centre. Passing the rect's own top-left as the pivot instead
      // is the same call one width and height off, and it is off in a way no
      // unit test catches, because DrawTexturePro is not what
      // board/node_interaction.h tests -- the image simply renders up and to the
      // left of its own border and handles.
      //
      // mipmaps is not read by DrawTexturePro. It is set to 1 rather than left
      // at 0 only so the struct is not a lie about being a texture.
      const Texture2D texture{node.texture_id, node.pixel_width,
                              node.pixel_height, 1};
      const Rectangle source{0.f, 0.f, static_cast<float>(node.pixel_width),
                             static_cast<float>(node.pixel_height)};
      const Rectangle dest{centre.x, centre.y, r.width, r.height};
      const Vector2 origin{r.width * 0.5f, r.height * 0.5f};
      // DrawTexturePro takes degrees, the component stores radians. Every node
      // placed so far is at 0, so this is the identity today; it is converted
      // rather than passed through so the first rotated node is not off by 57x.
      DrawTexturePro(texture, source, dest, origin,
                     transform.rotation * RAD2DEG, WHITE);

      // Border, as four lines between the rotated corners rather than
      // DrawRectangleLinesEx, which is axis-aligned and would draw a box that
      // does not match the image the moment it is turned. Computed from the same
      // NodeCorner the hit test uses, so what is drawn and what is grabbable
      // cannot drift apart.
      //
      // Half-unit outset so the line sits just outside the image rather than
      // eating into it. Without it a 1px image is entirely border.
      const float outset = line_thickness * 0.5f;
      const float half_w = r.width * 0.5f + outset;
      const float half_h = r.height * 0.5f + outset;
      const board_ns::Rect border_rect{centre.x - half_w, centre.y - half_h,
                                       half_w * 2.f, half_h * 2.f};
      for (int c = 0; c < board_ns::kCornerCount; ++c) {
        const int next = (c + 1) % board_ns::kCornerCount;
        const board_ns::Vec2 a =
          board_ns::NodeCorner(border_rect, transform.rotation, c);
        const board_ns::Vec2 b =
          board_ns::NodeCorner(border_rect, transform.rotation, next);
        DrawLineEx({a.x, a.y}, {b.x, b.y}, line_thickness, theme::node_label_dim);
      }

      // Corner handles, at the image's real corners rather than the outset
      // border's, so a handle sits on the pixel it moves.
      for (int c = 0; c < board_ns::kCornerCount; ++c) {
        const board_ns::Vec2 corner = board_ns::NodeCorner(r, transform.rotation, c);
        DrawCircleV({corner.x, corner.y}, handle_radius, theme::selection_handle);
      }
    });

  // The camera is a parameter so this reads as a world-space draw that the
  // caller has already framed, rather than one that reaches out and finds the
  // camera itself. It is used for the screen-pixel to world-unit conversion, so
  // the handles keep their size at any zoom.
}

}  // namespace referentia::systems
