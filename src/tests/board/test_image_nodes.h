// tests/board/test_image_nodes.h
//
// Headless tests for the image node's ECS structure.
//
// The claim being tested is "a placed image is a queryable entity carrying its
// transform, its texture and its source", which is otherwise only checkable by
// running the app and looking at the board. The specific regression locked down
// is the one from CreateBoardWorld: a factory that creates an entity and
// discards the handle leaves something that is alive, has no components, and is
// therefore invisible to every view<Ts...>() query -- a silent no-op that looks
// like it worked. Here the factory must add all three components and return a
// handle the caller can keep.
//
// The texture id is set by hand rather than by loading a file, because a real
// load needs an OpenGL context and this binary links no raylib. The id is an
// int in a plain struct precisely so that the node's structure can be tested
// without one.
#pragma once

#include <string>
#include <vector>

#include "../test_lib.h"
#include "board/image_source.h"
#include "board/transform.h"
#include "components/node.h"
#include "components/tags.h"
#include "entities/node.h"
#include "tests/ecs/ecs_count.h"
#include "engine/ecs/ecs.h"

namespace ec = referentia::components;
namespace ents = referentia::entities;
namespace img = referentia::board;
namespace ecs_ns = motrix::engine;

using test_ecs::CountMatching;

// Named wrappers rather than template arguments at the CHECK_EQ call sites: a
// template argument list's commas are indistinguishable from macro argument
// separators to the preprocessor, so CHECK_EQ(Count<Tag, Transform>(ecs), 1)
// would not expand at all and would fail as an undeclared function.

// The query DrawImageNodes() runs, i.e. what a placed image must match.
inline int CountImageNodes(ecs_ns::ECS& ecs) {
  return CountMatching<ec::TagNode, ec::NodeTransform,
                       ec::ImageNodeComponent>(ecs);
}
inline int CountNodeTags(ecs_ns::ECS& ecs) {
  return CountMatching<ec::TagNode>(ecs);
}
inline int CountNodeTransforms(ecs_ns::ECS& ecs) {
  return CountMatching<ec::NodeTransform>(ecs);
}
inline int CountImageNodeComponents(ecs_ns::ECS& ecs) {
  return CountMatching<ec::ImageNodeComponent>(ecs);
}

// --- a placed image is a real, queryable entity ---

TEST(test_imageNode_factoryReturnsLiveHandle) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity node = ents::CreateImageNode(
      ecs, ec::NodeTransform::Centred(0.f, 0.f, 64.f, 64.f),
      ec::ImageNodeComponent(7, 64, 64, "ref.png"));

  CHECK(node != ecs_ns::INVALID_ENTITY);
  CHECK(ecs.is_alive(node));
}

TEST(test_imageNode_isQueryableByAllThreeComponents) {
  ecs_ns::ECS ecs;
  ents::CreateImageNode(
      ecs, ec::NodeTransform::Centred(10.f, 20.f, 100.f, 50.f),
      ec::ImageNodeComponent(3, 100, 50, "/refs/hero.png"));

  // The three-component query is what DrawImageNodes() selects on, so if the
  // factory added only some of these it would render nothing at all while every
  // individual count still looked plausible.
  CHECK_EQ(CountImageNodes(ecs), 1);
  CHECK_EQ(CountNodeTransforms(ecs), 1);
  CHECK_EQ(CountImageNodeComponents(ecs), 1);
  // TagNode is a zero-sized tag, so it has no fields to read: counting the
  // query is the only way to assert it is present.
  CHECK_EQ(CountNodeTags(ecs), 1);
}

TEST(test_imageNode_factoryCreatesExactlyOneEntity) {
  // "At least one" would pass even if the factory also created a stray entity
  // that nothing can see.
  ecs_ns::ECS ecs;
  const size_t before = ecs.live_entity_count();

  ents::CreateImageNode(ecs, ec::NodeTransform::Centred(0.f, 0.f, 8.f, 8.f),
                        ec::ImageNodeComponent(1, 8, 8, "a.png"));

  CHECK_EQ(ecs.live_entity_count() - before, size_t{1});
}

// --- the transform is stored as placed ---

TEST(test_imageNode_storesThePlacementUnchanged) {
  ecs_ns::ECS ecs;
  const img::Rect placed = img::ImagePlacement(400, 300, {250.f, -125.f});

  const ecs_ns::Entity node =
      ents::CreateImageNode(ecs, ec::NodeTransform(placed),
                            ec::ImageNodeComponent(2, 400, 300, "b.png"));

  const ec::NodeTransform& t = ecs.get<ec::NodeTransform>(node);
  CHECK_NEAR(t.rect.x, placed.x, 0.0001);
  CHECK_NEAR(t.rect.y, placed.y, 0.0001);
  CHECK_NEAR(t.rect.width, placed.width, 0.0001);
  CHECK_NEAR(t.rect.height, placed.height, 0.0001);
  CHECK_NEAR(t.rotation, 0.f, 0.0001);
}

TEST(test_imageNode_centredHelperMatchesCentreOn) {
  // The two paths to a centred node must agree, or an image dropped on the board
  // and one placed by the dialog would differ by a translation for no reason.
  ecs_ns::ECS ecs;
  const ecs_ns::Entity node = ents::CreateImageNode(
      ecs, ec::NodeTransform::Centred(1000.f, 500.f, 400.f, 300.f),
      ec::ImageNodeComponent(4, 400, 300, "c.png"));

  const img::Rect direct = img::ImagePlacement(400, 300, {1000.f, 500.f});
  const ec::NodeTransform& t = ecs.get<ec::NodeTransform>(node);
  CHECK_NEAR(t.rect.x, direct.x, 0.0001);
  CHECK_NEAR(t.rect.y, direct.y, 0.0001);
}

// --- the payload is stored as given ---

TEST(test_imageNode_storesTextureAndSource) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity node = ents::CreateImageNode(
      ecs, ec::NodeTransform::Centred(0.f, 0.f, 32.f, 32.f),
      ec::ImageNodeComponent(42, 32, 32, "/home/gabriel/refs/hero.png"));

  const ec::ImageNodeComponent& img = ecs.get<ec::ImageNodeComponent>(node);
  CHECK_EQ(img.texture_id, 42u);
  CHECK_EQ(img.pixel_width, 32);
  CHECK_EQ(img.pixel_height, 32);
  // The source is what the node's label is built from later; losing it here
  // would make an imported image unfindable on the board.
  CHECK_EQ(img.source, std::string("/home/gabriel/refs/hero.png"));
}

TEST(test_imageNode_pixelSizeIsIndependentOfPlacement) {
  // A clamped 4000px image is drawn at 2000 world units, but it is still a
  // 4000px image and the source's true size has to survive for the label and for
  // any later resize-to-original.
  ecs_ns::ECS ecs;
  const ecs_ns::Entity node = ents::CreateImageNode(
      ecs, ec::NodeTransform::Centred(0.f, 0.f, 2000.f, 1500.f),
      ec::ImageNodeComponent(5, 4000, 3000, "big.jpg"));

  const ec::NodeTransform& t = ecs.get<ec::NodeTransform>(node);
  const ec::ImageNodeComponent& img = ecs.get<ec::ImageNodeComponent>(node);
  CHECK_NEAR(t.rect.width, 2000.f, 0.0001);
  CHECK_EQ(img.pixel_width, 4000);
  CHECK_EQ(img.pixel_height, 3000);
}

TEST(test_imageNode_webSourceHasNoPath) {
  // On the web the bytes have no path, and the node's source is a name. A node
  // whose source is an invented path would fail if anything ever opened it.
  ecs_ns::ECS ecs;
  const ecs_ns::Entity node = ents::CreateImageNode(
      ecs, ec::NodeTransform::Centred(0.f, 0.f, 10.f, 10.f),
      ec::ImageNodeComponent(6, 10, 10, "pasted.png"));

  const ec::ImageNodeComponent& img = ecs.get<ec::ImageNodeComponent>(node);
  CHECK_EQ(img.source, std::string("pasted.png"));
}

// --- several images coexist ---

TEST(test_imageNode_multipleNodesAreIndependent) {
  // The import path creates one node per file, so a batch has to produce
  // separate entities with separate payloads. A factory that re-touched the
  // previous node would make a multi-file drop land as one image.
  ecs_ns::ECS ecs;
  std::vector<ecs_ns::Entity> nodes;
  for (int i = 0; i < 3; ++i) {
    nodes.push_back(ents::CreateImageNode(
        ecs, ec::NodeTransform::Centred(static_cast<float>(i * 100), 0.f,
                                        80.f, 80.f),
        ec::ImageNodeComponent(static_cast<unsigned int>(i + 1), 80, 80,
                               "img" + std::to_string(i) + ".png")));
  }

  CHECK_EQ(CountImageNodes(ecs), 3);

  for (size_t i = 0; i < nodes.size(); ++i) {
    const ec::ImageNodeComponent& img = ecs.get<ec::ImageNodeComponent>(nodes[i]);
    CHECK_EQ(img.texture_id, static_cast<unsigned int>(i + 1));
    CHECK_NEAR(ecs.get<ec::NodeTransform>(nodes[i]).rect.x,
               static_cast<float>(i * 100) - 40.f, 0.0001);
  }
}
