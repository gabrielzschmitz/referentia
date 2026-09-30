// components/ui.h
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "raylib.h"

namespace referentia::components {

// Re-exported so the referentia namespaces need not fully qualify the
// Motrix ECS types; these were previously found by enclosing-namespace
// lookup while this code lived under motrix::engine.
using ::motrix::engine::ECS;
using ::motrix::engine::Entity;
using ::motrix::engine::INVALID_ENTITY;

// ------------------------------------------------------------
// Panel rhythm
//
// One value for every vertical gap in a window: between rows, between groups,
// and at the window's top and bottom edge. Sizing them independently is what
// made the Board panel read as loosely packed rather than as a grid -- the
// defaults were 8 between rows, 12 between groups, 24 under a group title and
// half the window padding at the top -- so no two gaps matched and a reader
// could not tell which ones were structural. They live here rather than in
// systems/ui_helpers.h because the component defaults below are the values
// that have to agree with them, and components cannot depend on a system.
// A group's own top and bottom padding is deliberately not this: it is the
// title band, and it is the space between a row and the frame around it.
// ------------------------------------------------------------

/** The gap. Rows, groups and window edges all sit on this pitch. */
constexpr float kRowGap = 12.f;

/**
 * From a group's top edge to the rule under its title. The one fixed-height
 * step, because it has to clear the title text; the group's first row then
 * sits kRowGap below the rule like every other row.
 */
constexpr float kGroupRuleOffset = 20.f;

/**
 * Row height for a label with no explicit height. Sized to the base font (17)
 * plus a couple of pixels of air: at the old 20 the glyph box overflowed its own
 * row, which is why the rows read as touching even with a gap between them.
 */
constexpr float kRowHeight = 24.f;

struct UIWindowComponent {
  static constexpr std::string_view Name = "UIWindow";

  Vector2 position{20.f, 20.f};
  float width = 220.f;
  float height = 0.f;
  bool auto_height = true;
  float padding = 10.f;
  float gap = kRowGap;
  std::string title;
  bool layout_dirty = true;
  bool dragging = false;
  Vector2 drag_offset{0.f, 0.f};
  bool close_requested = false;
  bool minimized = false;

  float scroll_y = 0.f;
  float content_height = 0.f;
  bool dragging_scrollbar = false;
  float scrollbar_drag_offset_y = 0.f;

  UIWindowComponent(Vector2 pos = {20.f, 20.f}, float w = 220.f, float h = 0.f,
                    std::string t = {})
    : position(pos), width(w), height(h), title(std::move(t)) {}
};

struct UILayoutChildComponent {
  static constexpr std::string_view Name = "UILayoutChild";

  Entity parent{0};
  float preferred_width = -1.f;  // -1 = stretch
  float preferred_height = kRowHeight;

  UILayoutChildComponent(Entity parent_entity = {}, float width = -1.f,
                         float height = 20.f)
    : parent(parent_entity), preferred_width(width), preferred_height(height) {}
};

struct UIResolvedRectComponent {
  static constexpr std::string_view Name = "UIResolvedRect";

  Rectangle rect{0.f, 0.f, 0.f, 0.f};
  Rectangle scaled_rect{0.f, 0.f, 0.f, 0.f};

  explicit UIResolvedRectComponent(Rectangle r = {0.f, 0.f, 0.f, 0.f})
    : rect(r) {}
};

/**
 * Slider/checkbox/dropdown values are bound via getter/setter lambdas instead
 * of raw pointers. The SparseSet can relocate components on entity removal, so
 * a long-lived raw pointer into a component field is unsafe. Bindings capture
 * the ECS + owning entity + member pointer and re-resolve on every access
 * (see `entities::FieldBinding`).
 */
struct UISliderComponent {
  static constexpr std::string_view Name = "UISlider";

  std::string label;
  std::function<float()> get_value;
  std::function<void(float)> set_value;
  float min = 0.f;
  float max = 1.f;
  float step = 0.1f;
  bool dragging = false;
  std::function<void(float)> on_change;

  UISliderComponent(std::string text = {}, std::function<float()> getter = {},
                    std::function<void(float)> setter = {},
                    float min_value = 0.f, float max_value = 1.f,
                    float step_value = 0.1f,
                    std::function<void(float)> callback = {})
    : label(std::move(text)),
      get_value(std::move(getter)),
      set_value(std::move(setter)),
      min(min_value),
      max(max_value),
      step(step_value),
      on_change(std::move(callback)) {}
};

struct UICheckboxComponent {
  static constexpr std::string_view Name = "UICheckbox";

  std::string label;
  std::function<bool()> get_value;
  std::function<void(bool)> set_value;
  std::function<void(bool)> on_change;

  UICheckboxComponent(std::string text = {}, std::function<bool()> getter = {},
                      std::function<void(bool)> setter = {},
                      std::function<void(bool)> callback = {})
    : label(std::move(text)),
      get_value(std::move(getter)),
      set_value(std::move(setter)),
      on_change(std::move(callback)) {}
};

struct UIButtonComponent {
  static constexpr std::string_view Name = "UIButton";

  std::string label;
  std::function<void()> on_click;

  UIButtonComponent(std::string text = {},
                    std::function<void()> callback = {})
    : label(std::move(text)), on_click(std::move(callback)) {}
};

struct UITextComponent {
  static constexpr std::string_view Name = "UIText";

  std::string text;

  explicit UITextComponent(std::string value = {})
    : text(std::move(value)) {}
};

struct UIDropdownComponent {
  static constexpr std::string_view Name = "UIDropdown";

  std::string label;
  std::vector<std::string> options;
  std::function<int()> get_index;
  std::function<void(int)> set_index;
  bool expanded = false;
  std::function<void(const std::string&)> on_select;

  UIDropdownComponent(std::string lbl = {}, std::vector<std::string> opts = {},
                      std::function<int()> getter = {},
                      std::function<void(int)> setter = {},
                      std::function<void(const std::string&)> callback = {})
    : label(std::move(lbl)),
      options(std::move(opts)),
      get_index(std::move(getter)),
      set_index(std::move(setter)),
      on_select(std::move(callback)) {}
};

struct UITooltipComponent {
  static constexpr std::string_view Name = "UITooltip";

  std::string text;
  float hover_timer = 0.0f;
  float delay = 1.0f;
  bool logged_visible = false;

  explicit UITooltipComponent(std::string t = "", float d = 1.0f)
    : text(std::move(t)), delay(d) {}
};

/**
 * Where a group's rows sit inside the group frame.
 *
 * A row of checkboxes is not a shape to be centred: each row has a different
 * width, so centring them leaves a ragged left edge and the labels look
 * staggered. Lists read flush left; a short row of buttons reads better centred.
 */
enum class UIGroupAlign {
  Left,
  Center,
  Right,
};

struct UIGroupComponent {
  static constexpr std::string_view Name = "UIGroup";

  std::string title;
  bool separator = true;
  // Matched to the air above the first row, not to the top padding. The top is
  // the larger number because the title and its rule sit in it -- what is
  // actually empty between the rule and the first row is kRowGap, and that is
  // the frame's inner margin. Copying the top value here instead put 32 of
  // blank space under the last row against 12 above it, which read as a bottom
  // margin twice the top.
  float padding_top = kGroupRuleOffset + kRowGap;
  float padding_bottom = kRowGap;
  float padding_left = 4.f;
  float padding_right = 4.f;
  float spacing = kRowGap;
  /** Horizontal placement of this group's rows. */
  UIGroupAlign align = UIGroupAlign::Center;
  /**
   * Give every child its own row instead of packing them side by side. For
   * groups of checkboxes, where one row per control is what the eye expects and
   * packing them two to a line halves the room for each label.
   */
  bool one_per_line = false;

  UIGroupComponent(std::string t = {}, bool sep = true,
                   UIGroupAlign a = UIGroupAlign::Center, bool one = false)
    : title(std::move(t)), separator(sep), align(a), one_per_line(one) {}
};

struct UIGroupChildComponent {
  static constexpr std::string_view Name = "UIGroupChild";

  Entity parent_group{};
  UIGroupChildComponent(Entity parent = {}) : parent_group(parent) {}
};

struct UINewLineComponent {
  static constexpr std::string_view Name = "UINewLine";
};

}  // namespace referentia::components
