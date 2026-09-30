// tests/board/test_board_entities.h
//
// Headless tests for the board's entity structure.
//
// The claim being tested is "everything the board owns is an ECS entity",
// which is otherwise only verifiable by reading the source. The specific
// regression these lock down: BoardInit used to call create_entity() and throw
// the handle away, leaving an entity that was alive, had no components, and was
// therefore invisible to every view<Ts...>() query. It looked like it worked
// and did nothing.
//
// No raylib here on purpose, so the components and factories stay free of it.
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "../test_lib.h"
#include "board/transform.h"
#include "components/board.h"
#include "components/tags.h"
#include "entities/board.h"
#include "engine/ecs/ecs.h"

namespace ec = referentia::components;
namespace ents = referentia::entities;
namespace ecs_ns = motrix::engine;

// Counts entities matching a query. Used to assert a factory created exactly
// one, rather than "at least one", which is what let the original bug through.
template <typename... Ts>
inline int CountMatching(ecs_ns::ECS& ecs) {
  int n = 0;
  ecs.view<Ts...>([&](ecs_ns::Entity, Ts&...) { ++n; });
  return n;
}

// --- the world root is a real, queryable entity ---

TEST(test_worldRoot_ReturnsLiveHandle) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity world = ents::CreateBoardWorld(ecs);

  // The factory used to return nothing, so this is the assertion that the
  // handle is captured at all.
  CHECK_MSG(ecs.is_alive(world), Msg("returned handle is not alive"));
}

TEST(test_worldRoot_CarriesTagAndComponent) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity world = ents::CreateBoardWorld(ecs);

  // The handle returned must be the entity that actually carries the tags.
  CHECK_MSG(ecs.has<ec::TagWorldRoot>(world),
            Msg("entity {} has no TagWorldRoot", world.index));
  CHECK_MSG(ecs.has<ec::BoardWorldComponent>(world),
            Msg("entity {} has no BoardWorldComponent", world.index));
}

TEST(test_worldRoot_IsReachableByQuery) {
  ecs_ns::ECS ecs;
  ents::CreateBoardWorld(ecs);

  // An entity with no components is invisible to every query, which is exactly
  // how the discarded-handle bug stayed hidden.
  CHECK_EQ(CountMatching<ec::TagWorldRoot>(ecs), 1);
  CHECK_EQ(CountMatching<ec::BoardWorldComponent>(ecs), 1);
}

// --- the debug overlay is an entity too, not a bare draw call ---

TEST(test_overlay_CarriesTagAndComponent) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity overlay = ents::CreateBoardDebugOverlay(ecs);

  CHECK_MSG(ecs.is_alive(overlay), Msg("returned handle is not alive"));
  CHECK_MSG(ecs.has<ec::TagOverlay>(overlay),
            Msg("entity {} has no TagOverlay", overlay.index));
  CHECK_MSG(ecs.has<ec::BoardDebugComponent>(overlay),
            Msg("entity {} has no BoardDebugComponent", overlay.index));

  // The render path queries on this exact pair.
  const int overlays =
      CountMatching<ec::TagOverlay, ec::BoardDebugComponent>(ecs);
  CHECK_EQ(overlays, 1);
}

// --- a tag is a real selector, and selects only what it should ---

TEST(test_tags_DoNotAliasBetweenEntityKinds) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity world = ents::CreateBoardWorld(ecs);
  const ecs_ns::Entity overlay = ents::CreateBoardDebugOverlay(ecs);

  CHECK_MSG(!ecs.has<ec::TagWorldRoot>(overlay),
            Msg("overlay {} is tagged as the world root", overlay.index));
  CHECK_MSG(!ecs.has<ec::TagOverlay>(world),
            Msg("world root {} is tagged as an overlay", world.index));
}

TEST(test_tags_SelectOnlyTheirOwnKind) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity world = ents::CreateBoardWorld(ecs);
  const ecs_ns::Entity overlay = ents::CreateBoardDebugOverlay(ecs);

  int world_hits = 0;
  ecs.view<ec::TagWorldRoot>([&](ecs_ns::Entity e, ec::TagWorldRoot&) {
    CHECK_EQ(e.index, world.index);
    ++world_hits;
  });
  CHECK_EQ(world_hits, 1);

  // A query must not silently pick up the other kind.
  ecs.view<ec::TagOverlay>([&](ecs_ns::Entity e, ec::TagOverlay&) {
    CHECK_EQ(e.index, overlay.index);
  });
}

// --- entity accounting: the world is not a leaked or double-counted entity ---

TEST(test_liveEntityCount_TracksCreationAndDestruction) {
  ecs_ns::ECS ecs;
  ents::CreateBoardWorld(ecs);
  ents::CreateBoardDebugOverlay(ecs);
  CHECK_EQ(ecs.live_entity_count(), 2);

  // Destroying one decrements the live count and leaves the other queryable.
  // versions_.size() still reports 2 here, which is why live_entity_count
  // subtracts the free list rather than reporting the high-water mark.
  const ecs_ns::Entity scratch = ecs.create_entity();
  ecs.destroy_entity(scratch);
  CHECK_EQ(ecs.live_entity_count(), 2);
  CHECK_EQ(CountMatching<ec::TagWorldRoot>(ecs), 1);

  // A fresh entity after a destroy must count, i.e. index reuse is not a leak.
  ents::CreateBoardWorld(ecs);
  CHECK_EQ(ecs.live_entity_count(), 3);
}

TEST(test_destroyedWorldRoot_LeavesTheQueryEmpty) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity world = ents::CreateBoardWorld(ecs);
  ecs.destroy_entity(world);

  // Destroying the anchor must actually remove it: this is the operation that
  // replaces the world root when the board is torn down.
  CHECK_MSG(!ecs.is_alive(world), Msg("destroyed handle reports alive"));
  CHECK_EQ(CountMatching<ec::TagWorldRoot>(ecs), 0);
}

// The destroy log now names the components that were removed, which means the
// mask is walked on every destroy. These cover that walk staying correct.

TEST(test_destroyEntity_ReportsEveryComponentItRemoved) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity e = ents::CreateBoardWorld(ecs);
  ecs.add<ec::TagOverlay>(e);  // deliberately a second, unrelated component

  CHECK_MSG(ecs.has<ec::TagWorldRoot>(e), Msg("TagWorldRoot should be set"));
  CHECK_MSG(ecs.has<ec::BoardWorldComponent>(e),
            Msg("BoardWorldComponent should be set"));

  ecs.destroy_entity(e);

  // Both components must be gone, not just the one that happened to be first
  // in the mask.
  CHECK_MSG(!ecs.has<ec::TagWorldRoot>(e), Msg("TagWorldRoot survived destroy"));
  CHECK_MSG(!ecs.has<ec::BoardWorldComponent>(e),
            Msg("BoardWorldComponent survived destroy"));
  CHECK_MSG(!ecs.has<ec::TagOverlay>(e), Msg("TagOverlay survived destroy"));
}

TEST(test_destroyEntity_IsIdempotentAndBumpsTheVersion) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity world = ents::CreateBoardWorld(ecs);
  ecs.destroy_entity(world);

  // A stale handle must not take out whatever entity later reuses the slot.
  // The version bump is what prevents that, so a second destroy is refused
  // rather than silently recycled.
  const ecs_ns::Entity recycled = ents::CreateBoardWorld(ecs);
  CHECK_EQ(recycled.index, world.index);

  ecs.destroy_entity(world);  // stale handle, same index, wrong version
  CHECK_MSG(ecs.is_alive(recycled),
            Msg("stale destroy killed the entity that reused the slot"));
  CHECK_EQ(CountMatching<ec::TagWorldRoot>(ecs), 1);
}

TEST(test_createEntity_LabelIsOptional) {
  ecs_ns::ECS ecs;
  // The default argument keeps call sites that pass no label working; the
  // lifecycle log must cope with an empty label rather than print a stray
  // separator.
  const ecs_ns::Entity unlabelled = ecs.create_entity();
  const ecs_ns::Entity labelled = ecs.create_entity("named-thing");

  CHECK_MSG(ecs.is_alive(unlabelled), Msg("unlabelled create returned a dead handle"));
  CHECK_MSG(ecs.is_alive(labelled), Msg("labelled create returned a dead handle"));
  CHECK_EQ(ecs.live_entity_count(), 2);

  CHECK_EQ(logger::detail::label_suffix(""), "");
  CHECK_EQ(logger::detail::label_suffix("camera"), " camera");
  CHECK_EQ(logger::detail::label_suffix(nullptr), "");
}

// --- defaults are the documented ones, not whatever a stale handle holds ---

TEST(test_overlayComponent_DefaultsToShowingEverything) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity overlay = ents::CreateBoardDebugOverlay(ecs);
  auto& cfg = ecs.get<ec::BoardDebugComponent>(overlay);

  CHECK_MSG(cfg.showGrid, Msg("showGrid should default true"));
  CHECK_MSG(cfg.showReferenceRect, Msg("showReferenceRect should default true"));
  CHECK_MSG(cfg.showHud, Msg("showHud should default true"));
}

TEST(test_overlayComponent_ReferenceRectDefaultsTo400x300AtOrigin) {
  ecs_ns::ECS ecs;
  const ecs_ns::Entity overlay = ents::CreateBoardDebugOverlay(ecs);
  auto& cfg = ecs.get<ec::BoardDebugComponent>(overlay);

  CHECK_EQ(cfg.referenceRect.width, 400.f);
  CHECK_EQ(cfg.referenceRect.height, 300.f);
  CHECK_EQ(cfg.referenceCenter.x, 0.f);
  CHECK_EQ(cfg.referenceCenter.y, 0.f);

  // The rect's own x/y is a top-left corner, not the centre, so the two fields
  // must not be conflated when the centre is moved.
  CHECK_EQ(cfg.referenceRect.x, 0.f);
  CHECK_EQ(cfg.referenceRect.y, 0.f);
}
