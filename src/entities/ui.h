// entities/ui.h
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "components/ui.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/logger.h"

namespace referentia::entities {

/**
 * ============================================================================
 * GUI Entities
 * ============================================================================
 *
 * Declarative ECS GUI:
 *
 * Window
 *   ├── Group
 *   │   ├── Slider
 *   │   ├── Dropdown
 *   │   ├── Checkbox
 *   │   ├── Button
 *   │   └── Text
 *   └── NewLine
 *
 * Layout is automatic; behaviour lives in callbacks.
 *
 * ============================================================================
 */

namespace ec = referentia::components;
namespace engine = motrix::engine;

/**
 * Binds a component field on an entity through getter/setter lambdas.
 *
 * Each access re-resolves `ecs.get<Component>(owner)`, so a component the
 * SparseSet relocates when an entity is removed can never leave a dangling
 * reference. A raw `&ecs.get<C>(e).field` captured by a widget callback would
 * dangle the moment anything ahead of it in the dense array is erased.
 */
template <typename Component, typename Member>
inline std::pair<std::function<Member()>, std::function<void(Member)>>
FieldBinding(engine::ECS& ecs, engine::Entity owner, Member Component::*member) {
  return {[&ecs, owner, member]() -> Member {
            return ecs.get<Component>(owner).*member;
          },
          [&ecs, owner, member](Member value) {
            ecs.get<Component>(owner).*member = value;
          }};
}

// ------------------------------------------------------------
// Widget builders. Each creates an entity with a layout child slot and a
// resolved rect, plus the widget component and, optionally, a tooltip and a
// group membership.
// ------------------------------------------------------------

inline engine::Entity AddWindow(engine::ECS& ecs, Vector2 position, float width,
                                float height, const std::string& title,
                                bool auto_height = true) {
  engine::Entity window = ecs.create_entity("ui-window");
  ecs.add<ec::UIWindowComponent>(
    window, ec::UIWindowComponent{position, width, height, title});
  ecs.get<ec::UIWindowComponent>(window).auto_height = auto_height;
  return window;
}

inline engine::Entity AddGroup(engine::ECS& ecs, engine::Entity window,
                               const std::string& title,
                               ec::UIGroupAlign align = ec::UIGroupAlign::Center,
                               bool one_per_line = false) {
  engine::Entity group = ecs.create_entity("ui-group");
  ecs.add<ec::UILayoutChildComponent>(
    group, ec::UILayoutChildComponent{window, -1.f, 0.f});
  ecs.add<ec::UIResolvedRectComponent>(group);
  ecs.add<ec::UIGroupComponent>(
    group, ec::UIGroupComponent{title, true, align, one_per_line});
  return group;
}

inline engine::Entity AddWidget(engine::ECS& ecs, engine::Entity parent,
                                float width, float height) {
  engine::Entity e = ecs.create_entity("ui-widget");
  ecs.add<ec::UILayoutChildComponent>(
    e, ec::UILayoutChildComponent{parent, width, height});
  ecs.add<ec::UIResolvedRectComponent>(e);
  return e;
}

inline void AddText(engine::ECS& ecs, engine::Entity window,
                    engine::Entity group, const std::string& text,
                    const std::string& tooltip = {}, float width = 0.f,
                    float height = ec::kRowHeight) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UITextComponent>(e, ec::UITextComponent(text));
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddSlider(engine::ECS& ecs, engine::Entity window,
                      engine::Entity group, const std::string& label,
                      std::function<float()> get_value,
                      std::function<void(float)> set_value, float min, float max,
                      float step, std::function<void(float)> on_change = {},
                      const std::string& tooltip = {}, float height = 30.f) {
  engine::Entity e = AddWidget(ecs, window, -1.f, height);
  ecs.add<ec::UISliderComponent>(
    e, ec::UISliderComponent{label, std::move(get_value),
                             std::move(set_value), min, max, step,
                             std::move(on_change)});
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddCheckbox(engine::ECS& ecs, engine::Entity window,
                        engine::Entity group, const std::string& label,
                        std::function<bool()> get_value,
                        std::function<void(bool)> set_value,
                        std::function<void(bool)> on_change = {},
                        const std::string& tooltip = {}, float width = 0.f,
                        float height = ec::kRowHeight) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UICheckboxComponent>(
    e, ec::UICheckboxComponent{label, std::move(get_value),
                               std::move(set_value), std::move(on_change)});
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddButton(engine::ECS& ecs, engine::Entity window,
                      engine::Entity group, const std::string& label,
                      std::function<void()> on_click,
                      const std::string& tooltip = {}, float width = 0.f,
                      float height = ec::kRowHeight) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UIButtonComponent>(
    e, ec::UIButtonComponent{label, std::move(on_click)});
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddDropdown(engine::ECS& ecs, engine::Entity window,
                        engine::Entity group, const std::string& label,
                        std::vector<std::string> options,
                        std::function<int()> get_index,
                        std::function<void(int)> set_index,
                        std::function<void(const std::string&)> on_select,
                        const std::string& tooltip = {}, float width = 0.f,
                        float height = ec::kRowHeight) {
  engine::Entity e = AddWidget(ecs, window, width, height);
  ecs.add<ec::UIDropdownComponent>(
    e, ec::UIDropdownComponent{label, std::move(options), std::move(get_index),
                               std::move(set_index), std::move(on_select)});
  if (!tooltip.empty()) ecs.add<ec::UITooltipComponent>(e, tooltip);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

inline void AddNewLine(engine::ECS& ecs, engine::Entity window,
                       engine::Entity group) {
  engine::Entity e = AddWidget(ecs, window, 0.f, 0.f);
  ecs.add<ec::UINewLineComponent>(e);
  if (group.index != engine::INVALID_ENTITY.index)
    ecs.add<ec::UIGroupChildComponent>(e, group);
}

}  // namespace referentia::entities
