// engine/globals.h
#pragma once

#include "app/font_faces.h"
#include "raylib.h"

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
 */
inline float ui_scale = 1.25f;

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
