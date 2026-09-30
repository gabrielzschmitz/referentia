// entities/camera.h
#pragma once

#include <algorithm>

#include "components/camera.h"
#include "components/tags.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "raylib.h"

namespace referentia::entities {
namespace ec = referentia::components;
namespace engine = motrix::engine;

/**
 * ============================================================================
 * Camera Entities
 * ============================================================================
 *
 * Creates the board camera. The board is infinite, so we do not centre any
 * fixed canvas extent; the target starts at the world origin. The camera
 * system keeps the offset locked to the viewport centre.
 * ============================================================================
 */

inline engine::Entity CreateCamera(engine::ECS& ecs) {
  engine::Entity e = ecs.create_entity("camera");

  ec::CameraComponent cam;
  cam.camera.target = Vector2{0.f, 0.f};
  cam.camera.rotation = 0.f;
  cam.camera.zoom = 1.f;

  const int window_width = GetScreenWidth();
  const int window_height = GetScreenHeight();
  cam.camera.offset = Vector2{static_cast<float>(window_width) * 0.5f,
                              static_cast<float>(window_height) * 0.5f};

  cam.uiScale = ui_scale;

  ecs.add<ec::CameraComponent>(e, cam);

  // Category, so a query can select the camera without knowing it also carries
  // a CameraComponent. The camera system iterates view<TagCamera,
  // CameraComponent>(); the extra tag is what makes "the camera" expressible
  // without a hardcoded handle in the app loop.
  ecs.add<ec::TagCamera>(e);

  LOG_DEBUG("[ENTITY] CreateCamera -> entity {} (zoom {}, offset {},{} target "
            "{},{})",
            e.index, cam.camera.zoom, cam.camera.offset.x, cam.camera.offset.y,
            cam.camera.target.x, cam.camera.target.y);

  return e;
}

}  // namespace referentia::entities
