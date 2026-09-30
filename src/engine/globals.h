// engine/globals.h
#pragma once

#include "app/font_faces.h"
#include "raylib.h"
#include "systems/frame_history.h"

/*
 * Window / scaling defaults.
 *
 * The board is an infinite viewport, so there is no world-space canvas extent
 * here any more. What remains is the initial window size and the UI scale,
 * which the whole widget layer multiplies through.
 */
inline constexpr int WINDOW_W = 1280;
inline constexpr int WINDOW_H = 720;

/*
 * UI scale. The widget layout is computed in logical units and multiplied by
 * this at draw time (see systems::ScaleRectCached). Raise it for HiDPI
 * displays; the fonts are rasterised at a matching base size so downscaling
 * stays sharp.
 *
 * Scaling here rather than inflating the per-widget font sizes is what keeps
 * text and its container in proportion: the row heights and paddings are
 * logical too, so raising the text alone clips it inside 20px-tall rows, while
 * raising this makes the whole panel bigger on screen at the same ratio.
 */
inline float ui_scale = 1.5f;

/*
 * Refresh rate of the monitor the window opened on, queried once at startup
 * (0 if the platform cannot report it).
 *
 * There is no software frame limiter: FLAG_VSYNC_HINT paces presentation off
 * the display, so this is the ceiling the app actually runs at rather than a
 * target we enforce. The FPS readout shows it so a V-Sync failure is visible
 * instead of being mistaken for a slow board.
 */
inline int display_refresh_rate = 0;

/*
 * Set while a widget is consuming keyboard input, e.g. a focused text field.
 *
 * The app's single-letter shortcuts check this first. Without it, typing "q"
 * into a text field would close the app, which is the kind of bug that only
 * shows up once someone tries to name a group "quotes" -- so the contract is
 * declared here now, ahead of the text widget that has to honour it.
 */
inline bool ui_keyboard_capture = false;

/*
 * Visibility of the frame-rate readout and the frame-time graph underneath it.
 *
 * Both live in globals.h rather than being file-static in app.cpp because the
 * F10 board panel binds checkboxes straight to them: the key and the checkbox
 * have to drive the same flag, or the panel lies about what the keyboard does.
 *
 * The graph is independent of the readout. Tying them together would mean F8
 * silently does nothing until F11 happens to be on, which is exactly the sort of
 * key that looks broken.
 */
inline bool show_fps = true;
inline bool show_frame_graph = false;

/*
 * Rolling window of frame times behind the F8 graph.
 *
 * A global for the same reason the toggles above are: the app loop pushes into
 * it every frame, the panel reads it when drawing, and the F10 checkbox clears
 * it on re-enable. Three call sites, one buffer, so it cannot be duplicated
 * into a local that one of them fails to see.
 *
 * Pushed unconditionally, even while the graph is off, so switching it on shows
 * recent history rather than starting from an empty panel.
 */
inline referentia::systems::FrameTimeHistory g_frame_history;

/*
 * Smoothed vertical scale of the F8 graph, in milliseconds.
 *
 * Kept between frames so the plot does not rescale on every draw, which would
 * make the whole visible history jitter. Owned here next to the history for
 * the same reason: the value is meaningless apart from the buffer it scales.
 */
inline float g_graph_ceiling = 0.f;

/*
 * Zoom bounds for the board camera. The low end has to go well below 1 so a
 * large board can be seen all at once; the high end is capped so a single
 * pixel of source image is never magnified past legibility.
 */
inline constexpr float ZOOM_MIN = 0.02f;
inline constexpr float ZOOM_MAX = 8.f;

/*
 * Fonts. One slot per Work Sans face, indexed by FontFaceIndex(weight, slant);
 * see app/font_faces.h for the face table and app/fonts.h for the loader.
 *
 * This is a C array rather than a std::array because every slot is zero
 * initialised with no dynamic initialisation to run at startup, and because
 * LoadFonts hands out `Font&` references to individual slots for lazy filling.
 *
 * Rasterising all eighteen faces costs roughly 2MB of VRAM each, so they are
 * not all loaded up front: the slots stay zero until something first draws
 * with that weight. Callers never see this, because they go through
 * app::GetFont.
 */
inline Font g_fonts[referentia::kFontFaceCount] = {};

/** Which of the g_fonts slots have actually been rasterised. */
inline bool g_font_loaded[referentia::kFontFaceCount] = {};

/*
 * Base sizes.
 *
 * Work Sans is rasterised into the glyph atlas at FONT_ATLAS_SIZE and then
 * drawn at smaller sizes. Downscaling a bitmap atlas is what keeps 11px UI
 * labels crisp; the reverse (rasterising small, drawing large) is blurry.
 */
inline constexpr int FONT_ATLAS_SIZE = 48;

/*
 * Board and node theme colours.
 */
namespace theme {

inline constexpr Color board_background{24, 25, 30, 255};

/**
 * Windows and floating surfaces (menus, tooltips) sit on the canvas, so they
 * need to be a step darker than it. At the canvas colour they were invisible:
 * the panel resolved to the same RGB as the background behind it, leaving only
 * a hairline outline to say where the window ended.
 */
inline constexpr Color window_background{18, 19, 23, 255};
inline constexpr Color board_grid_dot{255, 255, 255, 26};
inline constexpr Color board_grid_axis{255, 255, 255, 52};

inline constexpr Color node_label{228, 228, 235, 255};
inline constexpr Color node_label_dim{150, 151, 160, 255};

inline constexpr Color selection_fill{124, 108, 240, 46};
inline constexpr Color selection_outline{160, 148, 255, 255};
inline constexpr Color selection_handle{255, 255, 255, 255};
inline constexpr Color selection_hover{255, 255, 255, 140};

inline constexpr Color rotate_handle{255, 209, 102, 255};

}  // namespace theme
