// components/camera.h
#pragma once

#include <string_view>

#include "raylib.h"

namespace referentia::components {

/**
 * Board camera.
 *
 * The board is an unbounded plane, so this carries no notion of a world
 * extent. `camera.target` is the world point shown at the centre of the
 * viewport and `camera.zoom` the current scale; `camera.offset` is pinned to
 * the screen centre by the camera system every frame, which is what makes
 * world-to-screen a pure function of target and zoom.
 */
struct CameraComponent {
  static constexpr std::string_view Name = "Camera";

  Camera2D camera{};

  /**
   * UI scale multiplier, kept on the camera so world-space systems can convert
   * a screen-space constant (handle radius, minimum hit target) into world
   * units by dividing by zoom and multiplying by this.
   */
  float uiScale = 1.f;

  CameraComponent() {
    camera.target = {0.f, 0.f};
    // Recomputed from the framebuffer size every frame by the camera system.
    camera.offset = {0.f, 0.f};
    camera.rotation = 0.f;
    camera.zoom = 1.f;
  }
};

}  // namespace referentia::components
