// tests/ecs/ecs_count.h
//
// Query-counting helpers shared by the entity tests.
//
// This is a header rather than a function in test_lib.h because it needs the
// ECS header, and test_lib.h is deliberately dependency-free so that a pure
// maths test does not drag in the entity system. Several test headers land in
// the same translation unit (test_all.cpp includes them all), so an inline
// helper defined in one of them collides with the next one that defines it.
#pragma once

#include "engine/ecs/ecs.h"

namespace test_ecs {

/**
 * Counts the entities matching a query.
 *
 * Written to assert a factory created *exactly* one, rather than "at least
 * one": the original version of this helper was only used with the weaker
 * check, which is why an entity created with no components passed every test
 * while being invisible to every view<Ts...>() query in the ECS.
 */
template <typename... Ts>
inline int CountMatching(motrix::engine::ECS& ecs) {
  int n = 0;
  ecs.view<Ts...>([&](motrix::engine::Entity, Ts&...) { ++n; });
  return n;
}

}  // namespace test_ecs
