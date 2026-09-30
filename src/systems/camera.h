// systems/camera.h
#pragma once

#include <algorithm>
#include <cmath>

#include "components/camera.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "raylib.h"
#include "systems/ui_helpers.h"

namespace referentia::systems {

namespace ec = referentia::components;
namespace engine = motrix::engine;

/**
 * ============================================================================
 * Camera System
 * ============================================================================
 *
 * Owns navigation of the infinite board viewport:
 *   • wheel zooms about the cursor, so the world point under the pointer does
 *     not move
 *   • middle-button drag pans; space + left drag pans as an alternative for
 *     trackpad users
 *   • `=` recentres on the world origin
 *   • the viewport offset is re-pinned to the centre of the framebuffer every
 *     frame, which is what lets world<->screen be a pure function of target
 *     and zoom and keeps everything well-defined across a window resize
 *
 * All navigation is suppressed while the pointer is over a UI window, so
 * panning the board cannot be stolen by a panel.
 * ============================================================================
 */
inline void UpdateCamera2D(engine::ECS& ecs) {
  // Any panel on screen swallows wheel/pan input.
  bool mouse_over_ui = false;
  ecs.group_view<ec::UIWindowComponent>(
    [&](engine::Entity, ec::UIWindowComponent& win) {
      if (win.minimized) return;
      const Rectangle r{win.position.x, win.position.y, win.width, win.height};
      if (CheckCollisionPointRec(GetMousePosition(), ScaleRect(r)))
        mouse_over_ui = true;
    });

  const Vector2 mouse = GetMousePosition();
  const float wheel = GetMouseWheelMove();
  const bool middle_down = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE);
  // Space+left drag is the trackpad-friendly alternative to middle drag.
  const bool panning = middle_down ||
                       (IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_LEFT_BUTTON));

    // Remembered per frame rather than per camera entity, so a second camera
  // added later cannot inherit a stale cursor position from the first.
  static Vector2 last_mouse = {0.f, 0.f};

  // Pan logging is per gesture, not per frame. A drag changes the target on
  // every single frame it is held, so logging each change produced one console
  // line per frame for the entire drag -- at 240Hz that is hundreds of lines
  // for one flick of the wheel-button. Only the transitions are logged.
  static bool was_panning = false;
  static Vector2 pan_start_target = {0.f, 0.f};

  ecs.group_view<ec::CameraComponent>([&](engine::Entity entity, ec::CameraComponent& cam) {
    // Captured before mutation, so the debug readout only fires on frames where
    // the camera actually changed. Logging every frame would flood the console
    // and cost more than the frame does.
    const float prev_zoom = cam.camera.zoom;
    const Vector2 prev_target = cam.camera.target;
    const bool prev_reset = IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_EQUAL);

    const float screen_w = static_cast<float>(GetScreenWidth());
    const float screen_h = static_cast<float>(GetScreenHeight());

    // Re-pin the offset. Doing this unconditionally means the projection is
    // always correct, including on the frame a window is resized.
    cam.camera.offset = Vector2{screen_w * 0.5f, screen_h * 0.5f};
    cam.uiScale = ui_scale;

    if (prev_reset) {
      cam.camera.target = Vector2{0.f, 0.f};
      cam.camera.zoom = 1.f;
    }

    if (!mouse_over_ui) {
      if (wheel != 0.f) {
        // World point under the cursor, before and after the zoom change.
        const Vector2 before = GetScreenToWorld2D(mouse, cam.camera);

        // Multiplicative zoom with a normalised wheel step: trackpads report
        // fractional deltas that a fixed 0.1 per notch would swallow.
        const float factor = std::pow(1.1f, wheel);
        const float unclamped = cam.camera.zoom * factor;
        cam.camera.zoom = std::clamp(unclamped, ZOOM_MIN, ZOOM_MAX);
        if (unclamped != cam.camera.zoom) {
          LOG_DEBUG("[CAMERA] zoom clamped to the {}..{} range",
                    ZOOM_MIN, ZOOM_MAX);
        }

        const Vector2 after = GetScreenToWorld2D(mouse, cam.camera);

        // Shift the target by the drift so the same world point stays under
        // the same pixel.
        cam.camera.target.x += before.x - after.x;
        cam.camera.target.y += before.y - after.y;
      }

      if (panning) {
        const Vector2 delta{mouse.x - last_mouse.x, mouse.y - last_mouse.y};
        // Screen pixels converted to world units at the current zoom.
        cam.camera.target.x -= delta.x / cam.camera.zoom;
        cam.camera.target.y -= delta.y / cam.camera.zoom;
      }
    }

    if (prev_reset) {
      LOG_DEBUG("[CAMERA] entity {} reset to target (0,0) at zoom 1",
                entity.index);
      was_panning = false;
    } else if (cam.camera.zoom != prev_zoom) {
      LOG_DEBUG("[CAMERA] entity {} zoom {} -> {}, target ({},{})",
                entity.index, logger::detail::fixed(prev_zoom, 4),
                logger::detail::fixed(cam.camera.zoom, 4),
                logger::detail::fixed(cam.camera.target.x, 1),
                logger::detail::fixed(cam.camera.target.y, 1));
      was_panning = false;
    } else {
      // Gesture boundaries only. Zoom above already reports the target, and
      // zoom-at-cursor nudges it, so panning is the only remaining case.
      if (panning) {
        if (!was_panning) {
          pan_start_target = cam.camera.target;
          LOG_DEBUG("[CAMERA] entity {} pan started at target ({},{})",
                    entity.index,
                    logger::detail::fixed(pan_start_target.x, 1),
                    logger::detail::fixed(pan_start_target.y, 1));
        }
      } else if (was_panning) {
        LOG_DEBUG(
            "[CAMERA] entity {} pan ended: target ({},{}) -> ({},{}), {} world "
            "units travelled",
            entity.index, logger::detail::fixed(pan_start_target.x, 1),
            logger::detail::fixed(pan_start_target.y, 1),
            logger::detail::fixed(cam.camera.target.x, 1),
            logger::detail::fixed(cam.camera.target.y, 1),
            logger::detail::fixed(
                static_cast<double>(cam.camera.target.x - pan_start_target.x),
                1));
      }
      was_panning = panning;
    }

    last_mouse = mouse;
  });
}

}  // namespace referentia::systems
