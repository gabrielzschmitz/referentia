// entities/node.h
#pragma once

#include "board/transform.h"
#include "components/node.h"
#include "components/tags.h"
#include "engine/ecs/ecs.h"

namespace referentia::entities {

namespace ec = referentia::components;
namespace engine = motrix::engine;

/**
 * ============================================================================
 * Node Entities
 * ============================================================================
 *
 * The atomic things on the board: an image, a text block, a shape. They are
 * what a group frames and what auto-arrange spaces out, and they are the first
 * entities on the board that are not the board itself.
 *
 * The board is deliberately flat (see README): a node is never a child of
 * another node, so moving one can never invalidate anything. There is no
 * parent field on this component and no parent/child relationship in the ECS at
 * all -- adding one would be the first step towards a hierarchy, which is the
 * thing the flat board exists to avoid. Membership is TagNode, and "everything
 * on the board" is view<TagNode>().
 * ============================================================================
 */

/**
 * Creates a placed image node.
 *
 * Returns the handle, and the caller keeps it. BoardInit once called
 * create_entity() here and threw the result away, which left an entity that was
 * alive but had no components, therefore invisible to every view<Ts...>() query
 * in the ECS: a silent no-op that looked like it was working. The handle also
 * lets the import layer record the texture for unloading, which is the other
 * thing that needs to be tied to a specific node.
 */
inline engine::Entity CreateImageNode(engine::ECS& ecs,
                                      const ec::NodeTransform& transform,
                                      const ec::ImageNodeComponent& image) {
  engine::Entity e = ecs.create_entity("image-node");

  ecs.add<ec::TagNode>(e);
  ecs.add<ec::NodeTransform>(e, transform);
  ecs.add<ec::ImageNodeComponent>(e, image);

  return e;
}

}  // namespace referentia::entities
