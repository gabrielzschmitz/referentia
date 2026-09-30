// systems/ui_helpers.h
#pragma once

#include <cmath>
#include <string>

#include "components/ui.h"
#include "engine/globals.h"

namespace referentia::systems {

// Re-exported so the referentia namespaces need not fully qualify the
// Motrix ECS types; these were previously found by enclosing-namespace
// lookup while this code lived under motrix::engine.
using ::motrix::engine::ECS;
using ::motrix::engine::Entity;
using ::motrix::engine::INVALID_ENTITY;

// Logical (pre-scale) layout constants shared by every UI draw/layout path.
constexpr float kTitleBarHeight = 34.f;
constexpr float kScrollbarWidth = 14.f;
// Base label size for buttons, checkboxes, sliders, text and dropdowns. 12 read
// as a fine-print form, and 15 was still on the small side at 1x, so this is 17:
// the panel is a tool you glance at while looking at the board, not a document.
// Every consumer multiplies this by ui_scale, and the intrinsic-width
// measurements below use the same constant, so changing it rescales labels and
// widgets together.
constexpr float kBaseFontSize = 17.f;
/** Title-bar caption buttons. Their own size, not the body size: the dash and
 * the cross are the whole glyph, so at body size they read as specks. */
constexpr float kCaptionFontSize = 23.f;
constexpr float kDropdownOptionHeight = 32.f;

/**
 * Draw size must not exceed the rasterisation size (FONT_ATLAS_SIZE in
 * app/fonts.h). Drawing larger than the atlas upsamples a bilinear texture and
 * text goes soft permanently, and raising kBaseFontSize past the atlas silently
 * is how that happens: the build stays clean and the regression only shows up
 * on screen. Asserted rather than logged, because every text draw multiplies
 * this constant by ui_scale, which can legitimately push a single glyph above
 * the atlas for a heavily magnified display -- that is resampling a few glyphs
 * and is fine, whereas the base size exceeding the atlas is not.
 */
static_assert(kBaseFontSize <= 4096,
              "kBaseFontSize must not exceed the font atlas size; see "
              "FONT_ATLAS_SIZE in app/fonts.h");

// ------------------------------------------------------------
// Text
//
// Every string in the UI goes through these so the whole layer uses Work Sans
// rather than raylib's built-in font, and so the font size is expressed in
// logical (pre-scale) units everywhere. `DrawText`/`MeasureText` operate on
// raylib's default font at integer sizes, which is why they are not used here.
//
// Every helper takes a FontWeight, and the weight a string is measured at is
// required to be the weight it is drawn at. Bolder faces are wider, so
// measuring a title with Regular and drawing it with SemiBold overflows the
// box by the difference -- which is a bug that only shows up on screen, in a
// truncated title, and never in a build log.
// ------------------------------------------------------------

// Width of `text` at a logical font size, before ui_scale is applied.
inline float MeasureUiText(const std::string& text, float fontSize,
                           FontWeight weight = FontWeight::Regular,
                           FontSlant slant = FontSlant::Upright) {
  return MeasureTextEx(GetFont(weight, slant), text.c_str(), fontSize, 0.f).x;
}

inline float MeasureUiText(const char* text, float fontSize,
                           FontWeight weight = FontWeight::Regular,
                           FontSlant slant = FontSlant::Upright) {
  return MeasureTextEx(GetFont(weight, slant), text, fontSize, 0.f).x;
}

inline Vector2 MeasureUiTextEx(const std::string& text, float fontSize,
                               float spacing,
                               FontWeight weight = FontWeight::Regular) {
  return MeasureTextEx(GetFont(weight), text.c_str(), fontSize, spacing);
}

// Draw `text` with its top-left corner at `position`. `fontSize` is logical;
// ui_scale is applied here.
inline void DrawUiText(FontWeight weight, const std::string& text,
                       Vector2 position, float fontSize, Color color,
                       float spacing = 0.f,
                       FontSlant slant = FontSlant::Upright) {
  DrawTextEx(GetFont(weight, slant), text.c_str(), position,
             fontSize * ui_scale, spacing * ui_scale, color);
}

inline void DrawUiText(const std::string& text, Vector2 position, float fontSize,
                       Color color, float spacing = 0.f,
                       FontWeight weight = FontWeight::Regular,
                       FontSlant slant = FontSlant::Upright) {
  DrawUiText(weight, text, position, fontSize, color, spacing, slant);
}

// Draw text vertically centred inside `rect` (not scaled: `rect` is expected
// to already be in screen space).
inline void DrawUiTextCenteredY(FontWeight weight, const std::string& text,
                                Rectangle rect, float fontSize, Color color,
                                FontSlant slant = FontSlant::Upright) {
  const Vector2 size =
    MeasureTextEx(GetFont(weight, slant), text.c_str(), fontSize, 0.f);
  // The glyph box is taller than the cap height, so bias by the difference
  // between the full em box and the measured ascent to sit optically centred.
  const float y = rect.y + (rect.height - size.y) * 0.5f + size.y * 0.18f;
  DrawTextEx(GetFont(weight, slant), text.c_str(), {rect.x, y}, fontSize, 0.f,
             color);
}

// Draw text centred both horizontally and vertically inside a rect.
inline void DrawCenteredText(const char* text, Rectangle rect, float fontSize,
                             Color color,
                             FontWeight weight = FontWeight::Medium,
                             FontSlant slant = FontSlant::Upright) {
  const float text_w = MeasureUiText(text, fontSize, weight, slant);
  DrawUiTextCenteredY(weight, text,
                      {rect.x + (rect.width - text_w) * 0.5f, rect.y, text_w,
                       rect.height},
                      fontSize, color, slant);
}

// Scale a rectangle by ui_scale
inline Rectangle ScaleRect(Rectangle rect) {
  return {rect.x * ui_scale, rect.y * ui_scale, rect.width * ui_scale,
          rect.height * ui_scale};
}

// Scale a cached resolved rect
// Scale a cached resolved rect
inline Rectangle ScaleRectCached(
  components::UIResolvedRectComponent& resolved) {
  if (resolved.scaled_rect.width != resolved.rect.width ||
      resolved.scaled_rect.height != resolved.rect.height ||
      resolved.scaled_rect.x != resolved.rect.x ||
      resolved.scaled_rect.y != resolved.rect.y) {
    resolved.scaled_rect = {
      resolved.rect.x * ui_scale, resolved.rect.y * ui_scale,
      resolved.rect.width * ui_scale, resolved.rect.height * ui_scale};
  }
  return resolved.scaled_rect;
}

// Resolve a rect and shift it by the window's scroll offset.
inline Rectangle ScrolledResolvedRect(
  components::UIResolvedRectComponent& resolved, float scroll_y) {
  Rectangle rect = ScaleRectCached(resolved);
  rect.y -= (scroll_y * ui_scale);
  return rect;
}

// True when the left mouse button was pressed this frame over `rect` and no
// other widget has already claimed the click.
inline bool WasWidgetClicked(const Rectangle& rect, bool input_consumed) {
  return !input_consumed && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
         CheckCollisionPointRec(GetMousePosition(), rect);
}

// ------------------------------------------------------------
// Surfaces
//
// The app has exactly one surface style: a dark, slightly translucent panel
// with a hairline accent border. The widget layer and the performance panel
// share these helpers so the board window is visibly the same material as the
// readout, rather than two systems that happen to be on screen together.
//
// Replaces the previous Windows-95 bevel (white top-left, dark bottom-right,
// LIGHTGRAY fill). A bevel implies raised physical buttons; a reference canvas
// is flat and dark, and the bevel read as a foreign element on top of it.
// ------------------------------------------------------------

/**
 * Set a colour's alpha from a 0..255 value.
 *
 * raylib's Fade() takes a 0..1 fraction, and for an already-opaque colour it
 * returns the colour unchanged: 255 - (255 - 255) * a == 255. Every alpha in
 * this layer is written 0..255, so Fade(node_label, 20) was not an 8% tint but
 * solid opaque white -- the reason a widget hover painted a white block. Reach
 * for this helper; keep Fade for genuine 0..1 fractions.
 */
inline Color WithAlpha(Color color, unsigned char alpha) {
  color.a = alpha;
  return color;
}

/** Panel background and border, matching systems/perf_panel.h. */
inline void DrawPanelSurface(Rectangle rect) {
  DrawRectangleRec(rect, WithAlpha(theme::window_background, 242));
  DrawRectangleLinesEx(rect, 1.f, WithAlpha(theme::selection_outline, 60));
}

/** A hairline separator, used under titles and between group rows. */
inline void DrawHairline(float x0, float y, float x1) {
  DrawLine(x0, y, x1, y, WithAlpha(theme::node_label, 28));
}

/**
 * Interactive control surface: checkbox box, button, dropdown field, slider
 * track. Resting controls are the same dark as the panel with a hairline
 * outline, so they read as holes cut into the window rather than lighter
 * patches on top of it. Hover lifts the fill a little -- a lighter *dark*, not a
 * white flash, so the control still belongs to the panel -- and `active`
 * (checked / pressed) tints toward the accent.
 */
inline void DrawWidgetSurface(Rectangle rect, bool hovered = false,
                              bool active = false) {
  const Color fill = active    ? WithAlpha(theme::selection_outline, 80)
                     : hovered ? WithAlpha(theme::node_label, 20)
                               : theme::board_background;
  const Color border = (active || hovered)
                         ? WithAlpha(theme::selection_outline, 180)
                         : WithAlpha(theme::node_label, 70);
  DrawRectangleRec(rect, fill);
  DrawRectangleLinesEx(rect, 1.f, border);
}

inline void DrawPanelScrollbar(components::UIWindowComponent& win) {
  if (win.auto_height || win.content_height <= (win.height - kTitleBarHeight))
    return;

  float view_h = win.height - kTitleBarHeight;
  float max_scroll = win.content_height - view_h;

  Rectangle track_rect = {win.position.x + win.width - kScrollbarWidth,
                          win.position.y + kTitleBarHeight, kScrollbarWidth,
                          view_h};

  float thumb_h = std::fmax(20.f, view_h * (view_h / win.content_height));
  float scroll_ratio = win.scroll_y / max_scroll;

  Rectangle thumb_rect = {
    track_rect.x, track_rect.y + scroll_ratio * (track_rect.height - thumb_h),
    kScrollbarWidth, thumb_h};

  Rectangle track_scaled = ScaleRect(track_rect);
  Rectangle thumb_scaled = ScaleRect(thumb_rect);

  // Dark track, accent-tinted thumb: a scrollbar is a control, so it uses the
  // same two colours as every other control.
  DrawRectangleRec(track_scaled, WithAlpha(theme::node_label, 10));
  DrawRectangleRec(thumb_scaled, WithAlpha(theme::selection_outline, 120));
}

// Resolve intrinsic width for UI components
inline float ResolveIntrinsicWidth(ECS& ecs, Entity entity) {
  using namespace referentia::components;

  if (ecs.has<UIButtonComponent>(entity)) {
    auto& btn = ecs.get<UIButtonComponent>(entity);
    float text_w = MeasureUiText(btn.label, kBaseFontSize);
    return text_w + 16.f;
  }

  if (ecs.has<UICheckboxComponent>(entity)) {
    auto& cb = ecs.get<UICheckboxComponent>(entity);

    float text_w = MeasureUiText(cb.label, kBaseFontSize);

    float box_size = 16.f;

    if (ecs.has<UILayoutChildComponent>(entity)) {
      auto& layout = ecs.get<UILayoutChildComponent>(entity);
      if (layout.preferred_height > 0.f) {
        box_size = layout.preferred_height - 4.f;
      }
    }

    return box_size + 8.f + text_w;
  }

  if (ecs.has<UITextComponent>(entity)) {
    auto& txt = ecs.get<UITextComponent>(entity);
    return MeasureUiText(txt.text, kBaseFontSize);
  }

  if (ecs.has<UISliderComponent>(entity)) {
    return 100.f;
  }

  if (ecs.has<UIDropdownComponent>(entity)) {
    auto& dd = ecs.get<UIDropdownComponent>(entity);

    float label_w = MeasureUiText(dd.label + ": ", kBaseFontSize);

    float max_option_w = 0.f;
    for (const auto& option : dd.options) {
      float w = MeasureUiText(option, kBaseFontSize);
      if (w > max_option_w) max_option_w = w;
    }

    float arrow_w = 16.f;

    return label_w + max_option_w + arrow_w + 12.f;
  }

  return 0.f;
}

inline bool UIConsumesMouse(ECS& ecs, Vector2 mouse_screen) {
  using namespace referentia::components;

  bool dropdown_open = false;
  bool mouse_inside_dropdown = false;

  ecs.group_view<UIDropdownComponent, UIResolvedRectComponent,
                 UILayoutChildComponent>(
    [&](Entity, UIDropdownComponent& dropdown,
        UIResolvedRectComponent& resolved, UILayoutChildComponent& layout) {
      if (!dropdown.expanded) return;

      dropdown_open = true;

      if (!ecs.has<UIWindowComponent>(layout.parent)) return;

      auto& win = ecs.get<UIWindowComponent>(layout.parent);

      Rectangle rect = ScrolledResolvedRect(resolved, win.scroll_y);

      float option_h = kDropdownOptionHeight * ui_scale;

      Rectangle total_rect{
        rect.x, rect.y, rect.width,
        rect.height + option_h * static_cast<float>(dropdown.options.size())};

      if (CheckCollisionPointRec(mouse_screen, total_rect))
        mouse_inside_dropdown = true;
    });

  if (dropdown_open) {
    return !mouse_inside_dropdown;
  }

  return false;
}

inline bool IsMouseOverAnyWindow(ECS& ecs, Vector2 mouse_screen) {
  using namespace referentia::components;

  bool over_window = false;

  ecs.group_view<UIWindowComponent>([&](Entity, UIWindowComponent& window) {
    if (over_window) return;

    Rectangle rect{window.position.x, window.position.y, window.width,
                   window.height};

    rect = ScaleRect(rect);

    if (CheckCollisionPointRec(mouse_screen, rect)) {
      over_window = true;
    }
  });

  return over_window;
}

inline bool IsMouseOverUIRect(ECS& ecs, Rectangle target_rect) {
  using namespace referentia::components;

  Vector2 mouse = GetMousePosition();

  Entity top_window{};
  float highest_y = -1e9f;

  ecs.group_view<UIWindowComponent>(
    [&](Entity entity, UIWindowComponent& window) {
      Rectangle win{window.position.x * ui_scale, window.position.y * ui_scale,
                    window.width * ui_scale, window.height * ui_scale};

      if (CheckCollisionPointRec(mouse, win)) {
        if (window.position.y > highest_y) {
          highest_y = window.position.y;
          top_window = entity;
        }
      }
    });

  if (highest_y < -1e8f) return false;

  UIWindowComponent& window = ecs.get<UIWindowComponent>(top_window);

  Rectangle top_rect{window.position.x * ui_scale, window.position.y * ui_scale,
                     window.width * ui_scale, window.height * ui_scale};

  if (!CheckCollisionPointRec(mouse, top_rect)) return false;

  return CheckCollisionPointRec(mouse, target_rect);
}

}  // namespace referentia::systems
