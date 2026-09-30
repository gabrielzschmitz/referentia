// systems/ui.h
#pragma once

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "components/ui.h"
#include "engine/ecs/ecs.h"
#include "raymath.h"
#include "ui_helpers.h"

namespace referentia::systems {

// Re-exported so the referentia namespaces need not fully qualify the
// Motrix ECS types; these were previously found by enclosing-namespace
// lookup while this code lived under motrix::engine.
using ::motrix::engine::ECS;
using ::motrix::engine::Entity;
using ::motrix::engine::INVALID_ENTITY;

using namespace referentia::components;

//
// Layout Helpers
//
struct RowItem {
  UILayoutChildComponent* layout;
  UIResolvedRectComponent* rect;
  float width;
};

inline void FlushRow(std::vector<RowItem>& row, float content_width,
                     float content_left, float& cursor_y, float gap_spacing,
                     bool add_gap = true, bool left_align = false) {
  if (row.empty()) return;

  float row_height = 0.f;
  float total_width = 0.f;

  for (auto& item : row) {
    row_height = std::max(row_height, item.layout->preferred_height);
    total_width += item.width;
  }

  float total_gap =
    (row.size() > 1) ? gap_spacing * float(row.size() - 1) : 0.f;

  float x = left_align ? content_left
                       : content_left +
                           (content_width - (total_width + total_gap)) * 0.5f;

  for (auto& item : row) {
    item.rect->rect = {x, cursor_y, item.width, item.layout->preferred_height};
    x += item.width + gap_spacing;
  }

  cursor_y += row_height + (add_gap ? gap_spacing : 0.f);
  row.clear();
}

// Measure a layout child and push it onto the current row, flushing the row
// first when the child does not fit. Shared by group and window layout.
inline void PlaceRowItem(ECS& ecs, Entity child,
                         UILayoutChildComponent& layout,
                         UIResolvedRectComponent& child_rect,
                         std::vector<RowItem>& row, float content_left,
                         float content_width, float spacing,
                         float& cursor_y) {
  float width = layout.preferred_width;
  bool full_row = ecs.has<UISliderComponent>(child) || width == -1.f;

  if (full_row) {
    width = content_width;
  } else {
    if (width <= 0.f) width = ResolveIntrinsicWidth(ecs, child);
    if (width <= 0.f) width = content_width * 0.5f;
  }

  float row_width = 0.f;
  for (auto& item : row) row_width += item.width;
  if (!row.empty()) row_width += (row.size() - 1) * spacing;

  float next_row_width = row_width + width + (row.empty() ? 0.f : spacing);

  if (full_row || next_row_width > content_width)
    FlushRow(row, content_width, content_left, cursor_y, spacing);

  row.push_back(RowItem{&layout, &child_rect, width});
}

//
// Layout
//
inline void LayoutGroup(ECS& ecs, Entity group, UIGroupComponent& g,
                        UIResolvedRectComponent& group_rect, float content_left,
                        float content_width, float& cursor_y) {
  float group_start_y = cursor_y + g.padding_top;
  float group_cursor_y = group_start_y;

  float inner_content_left = content_left + g.padding_left;
  float inner_content_width = content_width - g.padding_left - g.padding_right;

  std::vector<RowItem> group_row;

  ecs.group_view<UILayoutChildComponent, UIResolvedRectComponent>(
    [&](Entity child, UILayoutChildComponent& layout,
        UIResolvedRectComponent& child_rect) {
      if (!ecs.has<UIGroupChildComponent>(child)) return;
      if (ecs.get<UIGroupChildComponent>(child).parent_group != group) return;

      if (ecs.has<UINewLineComponent>(child)) {
        FlushRow(group_row, inner_content_width, inner_content_left,
                 group_cursor_y, g.spacing);
        return;
      }

      PlaceRowItem(ecs, child, layout, child_rect, group_row,
                   inner_content_left, inner_content_width, g.spacing,
                   group_cursor_y);
    });

  FlushRow(group_row, inner_content_width, inner_content_left, group_cursor_y,
           g.spacing, false);

  float group_height = group_cursor_y - group_start_y;

  group_rect.rect = {content_left - 4.f, group_start_y - g.padding_top,
                     content_width + 8.f,
                     group_height + g.padding_top + g.padding_bottom};

  cursor_y = group_rect.rect.y + group_rect.rect.height;
}

inline void LayoutStandaloneChildren(ECS& ecs, Entity window_entity,
                                     float content_left, float content_width,
                                     float& cursor_y,
                                     UIWindowComponent& window) {
  std::vector<RowItem> row;

  ecs.group_view<UILayoutChildComponent, UIResolvedRectComponent>(
    [&](Entity child, UILayoutChildComponent& layout,
        UIResolvedRectComponent& child_rect) {
      if (layout.parent != window_entity) return;
      if (ecs.has<UIGroupChildComponent>(child)) return;
      if (ecs.has<UIGroupComponent>(child)) return;

      if (ecs.has<UINewLineComponent>(child)) {
        FlushRow(row, content_width, content_left, cursor_y, window.gap);
        return;
      }

      PlaceRowItem(ecs, child, layout, child_rect, row, content_left,
                   content_width, window.gap, cursor_y);
    });

  FlushRow(row, content_width, content_left, cursor_y, window.gap, false, true);
}

inline void LayoutUI(ECS& ecs) {
  ecs.group_view<UIWindowComponent>([&](Entity window_entity,
                                        UIWindowComponent& window) {
    if (!window.layout_dirty) return;

    bool had_scrollbar = !window.auto_height &&
                         (window.content_height >
                          (window.height - kTitleBarHeight));

    float start_y = window.position.y + window.padding / 2 + kTitleBarHeight;
    float cursor_y = start_y;
    float content_left = window.position.x + window.padding;
    float content_width = window.width - 2.f * window.padding;

    if (!window.auto_height && had_scrollbar) content_width -= kScrollbarWidth;

    size_t group_count = 0;

    ecs.group_view<UIGroupComponent>(
      [&](Entity, UIGroupComponent&) { group_count++; });

    bool has_standalone_children = false;

    ecs.group_view<UILayoutChildComponent>(
      [&](Entity child, UILayoutChildComponent& layout) {
        if (layout.parent == window_entity &&
            !ecs.has<UIGroupChildComponent>(child) &&
            !ecs.has<UIGroupComponent>(child)) {
          has_standalone_children = true;
        }
      });

    size_t current_group_idx = 0;

    ecs.group_view<UIGroupComponent, UIResolvedRectComponent>(
      [&](Entity group, UIGroupComponent& g, UIResolvedRectComponent& rect) {
        LayoutGroup(ecs, group, g, rect, content_left, content_width, cursor_y);

        current_group_idx++;

        if (current_group_idx < group_count || has_standalone_children) {
          cursor_y += window.gap;
        }
      });

    LayoutStandaloneChildren(ecs, window_entity, content_left, content_width,
                             cursor_y, window);
    window.content_height = cursor_y - start_y + window.padding;

    if (window.auto_height)
      window.height = cursor_y - window.position.y + window.padding;

bool needs_scrollbar = !window.auto_height &&
                           (window.content_height >
                            (window.height - kTitleBarHeight));

    if (had_scrollbar != needs_scrollbar)
      window.layout_dirty = true;
    else
      window.layout_dirty = false;
  });
}

inline void HandleWindowScrolling(ECS& ecs,
                                  UIWindowComponent& win) {
  if (win.auto_height) return;

  float view_h = win.height - kTitleBarHeight;

  if (win.content_height <= view_h) {
    win.scroll_y = 0.f;
    return;
  }

  float max_scroll = win.content_height - view_h;

  Rectangle track_rect = {win.position.x + win.width - kScrollbarWidth,
                          win.position.y + kTitleBarHeight, kScrollbarWidth,
                          view_h};

  float thumb_h = std::fmax(10.f, view_h * (view_h / win.content_height));
  float scroll_ratio = win.scroll_y / max_scroll;

  Rectangle thumb_rect = {
    track_rect.x, track_rect.y + scroll_ratio * (track_rect.height - thumb_h),
    kScrollbarWidth, thumb_h};

  Rectangle track_scaled = ScaleRect(track_rect);
  Rectangle thumb_scaled = ScaleRect(thumb_rect);
  Vector2 mouse_screen = GetMousePosition();

  Rectangle content_rect = ScaleRect(
    {win.position.x, win.position.y + kTitleBarHeight,
     win.width - kScrollbarWidth, win.height - kTitleBarHeight});

  Vector2 mouse = GetMousePosition();

  if (CheckCollisionPointRec(mouse, content_rect) &&
      !UIConsumesMouse(ecs, mouse)) {
    float wheel = GetMouseWheelMove();
    if (wheel != 0.f) win.scroll_y -= wheel * 30.f;
  }

  if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
      CheckCollisionPointRec(mouse_screen, thumb_scaled)) {
    win.dragging_scrollbar = true;
    win.scrollbar_drag_offset_y = mouse_screen.y - thumb_scaled.y;
  }

  if (win.dragging_scrollbar) {
    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
      win.dragging_scrollbar = false;
    } else {
      float local_y_scaled =
        mouse_screen.y - win.scrollbar_drag_offset_y - track_scaled.y;
      float max_local_y_scaled = track_scaled.height - thumb_scaled.height;

      float new_ratio = Clamp(local_y_scaled / max_local_y_scaled, 0.f, 1.f);
      win.scroll_y = new_ratio * max_scroll;
    }
  }

  win.scroll_y = Clamp(win.scroll_y, 0.f, max_scroll);
}

//
// Render
//
inline void RenderGroups(ECS& ecs, Entity window_entity, float scroll_y) {
  ecs.group_view<UIGroupComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity, UIGroupComponent& g,
                                             UIResolvedRectComponent& rect,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity) return;

    Rectangle r = ScrolledResolvedRect(rect, scroll_y);

    DrawRectangleLinesEx(r, 1.f, DARKGRAY);

    if (g.separator)
      DrawLine(r.x, r.y + 18 * ui_scale, r.x + r.width, r.y + 18 * ui_scale,
               GRAY);

    if (!g.title.empty())
      DrawUiText(FontWeight::Medium, g.title,
                 {r.x + 4 * ui_scale, r.y + 2 * ui_scale}, 12.f, BLACK);
  });
}

inline void RenderSliders(ECS& ecs, Entity window_entity, float scroll_y,
                          bool& input_consumed) {
  ecs.group_view<UISliderComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity, UISliderComponent& slider,
                                             UIResolvedRectComponent& resolved,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity) return;
    if (!slider.get_value) return;

    float current_val = slider.get_value();
    Rectangle rect = ScrolledResolvedRect(resolved, scroll_y);

    Rectangle bar{rect.x, rect.y + 14 * ui_scale, rect.width, 16 * ui_scale};

    int decimal_places = (slider.step >= 0.1f)     ? 1
                         : (slider.step >= 0.01f)  ? 2
                         : (slider.step >= 0.001f) ? 3
                                                   : 4;
    char label_buf[256];
    snprintf(label_buf, sizeof(label_buf), "%s: %.*f", slider.label.c_str(),
             decimal_places, current_val);
    DrawUiText(std::string(label_buf), {rect.x, rect.y}, kBaseFontSize, BLACK);

    DrawRectangleRec(bar, {223, 223, 223, 255});
    DrawRectangleLinesEx(bar, 1 * ui_scale, BLACK);

    float t = (current_val - slider.min) / (slider.max - slider.min);
    float knob_x = bar.x + t * bar.width;
    Rectangle knob{knob_x - 4 * ui_scale, bar.y - 2 * ui_scale, 8 * ui_scale,
                   bar.height + 4 * ui_scale};

    DrawWin95Box(knob, LIGHTGRAY);

    bool hit =
      WasWidgetClicked(bar, input_consumed) ||
      WasWidgetClicked(knob, input_consumed);

    if (hit) {
      slider.dragging = true;
      input_consumed = true;
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) slider.dragging = false;

    if (slider.dragging) {
      float mouse_t = (GetMousePosition().x - bar.x) / bar.width;
      mouse_t = Clamp(mouse_t, 0.f, 1.f);
      float raw = slider.min + mouse_t * (slider.max - slider.min);
      float snapped = std::round(raw / slider.step) * slider.step;
      float new_value = Clamp(snapped, slider.min, slider.max);
      if (slider.set_value) slider.set_value(new_value);
      if (slider.on_change) slider.on_change(new_value);
    }
  });
}

inline void RenderCheckboxes(ECS& ecs, Entity window_entity, float scroll_y,
                             bool input_consumed) {
  ecs.group_view<UICheckboxComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>(
    [&](Entity e, UICheckboxComponent& checkbox,
        UIResolvedRectComponent& resolved, UILayoutChildComponent& layout) {
      if (layout.parent != window_entity) return;

      Rectangle rect = ScrolledResolvedRect(resolved, scroll_y);

      bool is_full_width = (layout.preferred_width == -1.f);
      float box_size = rect.height - 4.f * ui_scale;
      float text_w = MeasureUiText(checkbox.label, kBaseFontSize);
      float gap = 6.f * ui_scale;
      float box_y = rect.y + (rect.height - box_size) * 0.5f;

      float check_x = rect.x;
      float text_x = rect.x + box_size + gap;

      if (is_full_width) {
        DrawWin95Box(rect, LIGHTGRAY);

        float total_w = box_size + gap + text_w;
        check_x = rect.x + (rect.width - total_w) * 0.5f;
        text_x = check_x + box_size + gap;
      } else {
        DrawWin95Box({check_x, box_y, box_size, box_size}, LIGHTGRAY);
      }

      if (checkbox.get_value && checkbox.get_value()) {
        DrawLine(check_x + box_size * 0.2f, box_y + box_size * 0.5f,
                 check_x + box_size * 0.45f, box_y + box_size * 0.75f, BLACK);

        DrawLine(check_x + box_size * 0.45f, box_y + box_size * 0.75f,
                 check_x + box_size * 0.8f, box_y + box_size * 0.2f, BLACK);
      }

      DrawUiTextCenteredY(FontWeight::Regular, checkbox.label,
                          {text_x, rect.y, rect.width, rect.height},
                          kBaseFontSize, BLACK);

      if (WasWidgetClicked(rect, input_consumed)) {
        bool new_value = !(checkbox.get_value && checkbox.get_value());
        if (checkbox.set_value) checkbox.set_value(new_value);
        if (checkbox.on_change) checkbox.on_change(new_value);
      }
    });
}

inline void RenderButtons(ECS& ecs, Entity window_entity, float scroll_y,
                          bool input_consumed) {
  ecs.group_view<UIButtonComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity, UIButtonComponent& button,
                                             UIResolvedRectComponent& resolved,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity) return;

    Rectangle rect = ScrolledResolvedRect(resolved, scroll_y);

    DrawWin95Box(rect, LIGHTGRAY);
    DrawCenteredText(button.label.c_str(), rect, kBaseFontSize, BLACK);

    if (WasWidgetClicked(rect, input_consumed))
      if (button.on_click) button.on_click();
  });
}

inline void RenderText(ECS& ecs, Entity window_entity, float scroll_y) {
  ecs.group_view<UITextComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity, UITextComponent& text,
                                             UIResolvedRectComponent& resolved,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity) return;

    Rectangle rect = ScrolledResolvedRect(resolved, scroll_y);

    DrawUiTextCenteredY(FontWeight::Regular, text.text, rect, kBaseFontSize,
                        BLACK);
  });
}

inline void RenderDropdowns(ECS& ecs, Entity window_entity, float scroll_y,
                            bool& input_consumed) {
  ecs.group_view<UIDropdownComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>(
    [&](Entity e, UIDropdownComponent& dropdown,
        UIResolvedRectComponent& resolved, UILayoutChildComponent& layout) {
      if (layout.parent != window_entity) return;

      Rectangle rect = ScrolledResolvedRect(resolved, scroll_y);

      DrawWin95Box(rect, LIGHTGRAY);

      float arrow_w = 16.f * ui_scale;
      Rectangle arrow_rect = {rect.x + rect.width - arrow_w, rect.y, arrow_w,
                              rect.height};
      DrawWin95Box(arrow_rect, LIGHTGRAY);
      DrawCenteredText("\xE2\x96\xBC", arrow_rect, kBaseFontSize, BLACK);

      std::string display = dropdown.label + ": ";
      if (dropdown.get_index) {
        int idx = dropdown.get_index();
        if (idx >= 0 && idx < (int)dropdown.options.size())
          display += dropdown.options[idx];
      }
      DrawUiTextCenteredY(FontWeight::Regular, display,
                          {rect.x + 4.f * ui_scale, rect.y, rect.width,
                           rect.height},
                          kBaseFontSize, BLACK);

      if (WasWidgetClicked(rect, input_consumed)) {
        dropdown.expanded = !dropdown.expanded;
        input_consumed = true;
      }
    });
}

inline void RenderDropdownLists(ECS& ecs, Entity window_entity, float scroll_y,
                                bool& input_consumed) {
  ecs.group_view<UIDropdownComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>([&](Entity e,
                                             UIDropdownComponent& dropdown,
                                             UIResolvedRectComponent& resolved,
                                             UILayoutChildComponent& layout) {
    if (layout.parent != window_entity || !dropdown.expanded) return;

    Rectangle rect = ScrolledResolvedRect(resolved, scroll_y);
    float option_h = kDropdownOptionHeight * ui_scale;

    for (size_t i = 0; i < dropdown.options.size(); ++i) {
      Rectangle option_rect{rect.x, rect.y + rect.height + i * option_h,
                            rect.width, option_h};

      bool hover = CheckCollisionPointRec(GetMousePosition(), option_rect);

      DrawWin95Box(option_rect, hover ? Color{225, 225, 225, 255} : LIGHTGRAY);

      DrawUiTextCenteredY(FontWeight::Regular, dropdown.options[i],
                          {option_rect.x + 4 * ui_scale, option_rect.y,
                           option_rect.width, option_rect.height},
                          kBaseFontSize, BLACK);
    }
  });
}

inline void PrepassDropdownInput(ECS& ecs, bool& input_consumed) {
  if (!IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) return;

  Vector2 mouse = GetMousePosition();

  ecs.group_view<UIDropdownComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>(
    [&](Entity, UIDropdownComponent& dropdown,
        UIResolvedRectComponent& resolved, UILayoutChildComponent& layout) {
      if (!dropdown.expanded) return;

      if (!ecs.has<UIWindowComponent>(layout.parent)) return;

      auto& window = ecs.get<UIWindowComponent>(layout.parent);

      Rectangle rect = ScrolledResolvedRect(resolved, window.scroll_y);

      float option_h = kDropdownOptionHeight * ui_scale;

      for (size_t i = 0; i < dropdown.options.size(); ++i) {
        Rectangle option_rect{rect.x, rect.y + rect.height + i * option_h,
                              rect.width, option_h};

        if (CheckCollisionPointRec(mouse, option_rect)) {
          if (dropdown.set_index) dropdown.set_index((int)i);
          dropdown.expanded = false;
          input_consumed = true;

          if (dropdown.on_select) dropdown.on_select(dropdown.options[i]);
          return;
        }
      }

      Rectangle total_list_rect{rect.x, rect.y + rect.height, rect.width,
                                option_h * dropdown.options.size()};

      bool over_button = CheckCollisionPointRec(mouse, rect);
      bool over_list = CheckCollisionPointRec(mouse, total_list_rect);

      if (!over_button && !over_list) dropdown.expanded = false;
    });
}

inline void RenderTooltips(ECS& ecs, float scroll_y, bool input_consumed) {
  Vector2 mouse = GetMousePosition();
  float dt = GetFrameTime();
  std::string tooltip_to_draw = "";

  ecs.group_view<components::UITooltipComponent,
               components::UIResolvedRectComponent>(
    [&](Entity e, components::UITooltipComponent& tooltip,
        components::UIResolvedRectComponent& resolved) {
      Rectangle rect = ScrolledResolvedRect(resolved, scroll_y);
      bool is_hovering = !input_consumed && IsMouseOverUIRect(ecs, rect);
      if (is_hovering) {
        tooltip.hover_timer += dt;
        if (tooltip.hover_timer >= tooltip.delay) {
          tooltip_to_draw = tooltip.text;
          if (!tooltip.logged_visible) {
            logger::debug("[GUI] Tooltip appeared: '{}' (entity:{}:{})",
                          tooltip.text, e.index, e.version);
            tooltip.logged_visible = true;
          }
        }
      } else if (tooltip.hover_timer > 0.0f) {
        tooltip.hover_timer = 0.0f;
        tooltip.logged_visible = false;
      }
    });

  if (!tooltip_to_draw.empty()) {
    // Size in screen space, so measure at the already-scaled size.
    const float fontSize = kBaseFontSize * ui_scale;
    const float tipWidth = MeasureUiText(tooltip_to_draw, fontSize) + 10 * ui_scale;
    const float tipHeight = fontSize + 8 * ui_scale;

    // Flip the tooltip inwards when it would leave the window, so it stays
    // readable near the right and bottom edges.
    const float screen_w = static_cast<float>(GetScreenWidth());
    const float screen_h = static_cast<float>(GetScreenHeight());
    constexpr float kOffset = 12.f;
    constexpr float kMargin = 2.f;

    float tipX = mouse.x + kOffset;
    if (tipX + tipWidth > screen_w - kMargin)
      tipX = mouse.x - tipWidth - kOffset;
    if (tipX < kMargin) tipX = kMargin;

    float tipY = mouse.y + kOffset;
    if (tipY + tipHeight > screen_h - kMargin)
      tipY = mouse.y - tipHeight - kOffset;
    if (tipY < kMargin) tipY = kMargin;

    const Rectangle tipRect{tipX, tipY, tipWidth, tipHeight};

    DrawRectangleRec(tipRect, {255, 255, 225, 255});
    DrawRectangleLinesEx(tipRect, 1, BLACK);
    DrawUiTextCenteredY(FontWeight::Regular, tooltip_to_draw,
                        {tipRect.x + 5 * ui_scale, tipRect.y, tipRect.width,
                         tipRect.height},
                        fontSize, BLACK);
  }
}

// Rebuilds the panel when it has been closed. Supplied by the app so this
// system stays independent of the board wiring.
using CreateUI = std::function<void(ECS&)>;

inline void RenderWindow(ECS& ecs, bool& input_consumed,
                         const CreateUI& create_ui) {
  Vector2 mouse = GetMousePosition();

  if (IsKeyPressed(KEY_F10) || IsKeyPressed(KEY_U)) {
    bool has_window = false;
    ecs.group_view<UIWindowComponent>([&](Entity, UIWindowComponent& window) {
      has_window = true;
      window.minimized = !window.minimized;
      window.layout_dirty = true;
    });

    if (!has_window) {
      if (create_ui) create_ui(ecs);
      return;
    }
  }

  ecs.group_view<UIWindowComponent>([&](Entity e, UIWindowComponent& window) {
    if (window.minimized) {
      float btn_size = 24.f * ui_scale;
      float btn_x = GetScreenWidth() - btn_size - (10.f * ui_scale);
      float btn_y = 10.f * ui_scale;
      Rectangle restore_btn{btn_x, btn_y, btn_size, btn_size};

      DrawWin95Box(restore_btn, LIGHTGRAY);

      float margin = 6.f * ui_scale;
      Rectangle inner_rect{restore_btn.x + margin, restore_btn.y + margin,
                           restore_btn.width - (margin * 2),
                           restore_btn.height - (margin * 2)};
      DrawRectangleLinesEx(inner_rect, 1.f * ui_scale, BLACK);

      if (WasWidgetClicked(restore_btn, input_consumed)) {
        window.minimized = false;
        window.layout_dirty = true;
        input_consumed = true;
      }

      return;
    }

    const float title_h_scaled = kTitleBarHeight * ui_scale;
    const float border = 2.f * ui_scale;

    Rectangle rect = ScaleRect(
      {window.position.x, window.position.y, window.width, window.height});
    bool mouse_over_window = CheckCollisionPointRec(mouse, rect);

    const float title_h = 24.f * ui_scale;
    Rectangle close_button{rect.x + rect.width - title_h, rect.y, title_h,
                           title_h};
    Rectangle minimize_button{rect.x + rect.width - (title_h * 2), rect.y,
                              title_h, title_h};
    Rectangle title_bar = {rect.x + border, rect.y + border,
                           rect.width - (border * 2), title_h_scaled - border};

    if (WasWidgetClicked(close_button, input_consumed)) {
      window.close_requested = true;
      input_consumed = true;
    } else if (WasWidgetClicked(minimize_button, input_consumed)) {
      window.minimized = !window.minimized;
      window.layout_dirty = true;
      input_consumed = true;
    } else if (WasWidgetClicked(title_bar, input_consumed)) {
      window.dragging = true;
      window.layout_dirty = true;
      window.drag_offset = {(mouse.x / ui_scale) - window.position.x,
                            (mouse.y / ui_scale) - window.position.y};
      input_consumed = true;
    }

    if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) window.dragging = false;
    if (window.dragging) {
      window.position.x = (mouse.x / ui_scale) - window.drag_offset.x;
      window.position.y = (mouse.y / ui_scale) - window.drag_offset.y;
      window.layout_dirty = true;
    }

    DrawWin95Box(rect, LIGHTGRAY);
    DrawRectangleRec(title_bar, {0, 0, 128, 255});
    DrawUiText(FontWeight::SemiBold, window.title,
               {rect.x + 6 * ui_scale, rect.y + 4 * ui_scale}, 13.f, WHITE);

    DrawWin95Box(minimize_button, LIGHTGRAY);
    DrawCenteredText("-", minimize_button, kBaseFontSize, BLACK);

    DrawWin95Box(close_button, LIGHTGRAY);
    DrawCenteredText("\xC3\x97", close_button, kBaseFontSize, BLACK);

    if (window.close_requested) {
      std::vector<Entity> to_destroy;

      std::function<void(Entity)> collect_descendants = [&](Entity parent) {
        ecs.group_view<UILayoutChildComponent>(
          [&](Entity child, UILayoutChildComponent& layout) {
            if (layout.parent.index == parent.index &&
                layout.parent.version == parent.version) {
              collect_descendants(child);
              to_destroy.push_back(child);
            }
          });
      };

      collect_descendants(e);

      for (Entity child : to_destroy) ecs.destroy_entity(child);

      logger::info(
        "[GUI] Closed window '{}' and cleaned up {} sub-entities "
        "(entity:{}:{})",
        window.title, to_destroy.size(), e.index, e.version);
      ecs.destroy_entity(e);
      return;
    }

    if (mouse_over_window) HandleWindowScrolling(ecs, window);
    DrawWin95Scrollbar(window);

    bool has_scrollbar =
      (window.content_height > (window.height - kTitleBarHeight));
    float scrollbar_w_scaled = has_scrollbar ? (kScrollbarWidth * ui_scale) : 0.f;

    Rectangle content_area = {rect.x + border, rect.y + title_h_scaled,
                              rect.width - (border * 2) - scrollbar_w_scaled,
                              rect.height - title_h_scaled - border};

    BeginScissorMode((int)content_area.x, (int)content_area.y,
                     (int)content_area.width, (int)content_area.height + 4);

    RenderGroups(ecs, e, window.scroll_y);
    RenderSliders(ecs, e, window.scroll_y, input_consumed);
    RenderCheckboxes(ecs, e, window.scroll_y, input_consumed);
    RenderButtons(ecs, e, window.scroll_y, input_consumed);
    RenderText(ecs, e, window.scroll_y);
    RenderDropdowns(ecs, e, window.scroll_y, input_consumed);

    EndScissorMode();

    RenderDropdownLists(ecs, e, window.scroll_y, input_consumed);
    RenderTooltips(ecs, window.scroll_y, input_consumed);

    if (mouse_over_window && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
      input_consumed = true;
  });
}

inline void RenderUI(ECS& ecs, const CreateUI& create_ui = nullptr) {
  LayoutUI(ecs);

  bool input_consumed = false;

  PrepassDropdownInput(ecs, input_consumed);

  RenderWindow(ecs, input_consumed, create_ui);
}

}  // namespace referentia::systems
