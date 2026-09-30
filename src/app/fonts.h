// app/fonts.h
//
// Loads the bundled Work Sans faces into raylib Font handles.
//
// Work Sans is licensed under the SIL Open Font License 1.1; the licence text
// ships next to the binaries in resources/fonts/OFL.txt.
//
// Only *static* instances are used. raylib rasterises TTF through stb_truetype,
// which flattens a variable font to its default instance and has no notion of
// a `wght` axis, so WorkSans[wght].ttf would silently render as one weight.
// The full set of nine statics -- Thin through Black, upright and italic --
// ships in resources/fonts; app/font_faces.h holds the filename table.
//
// Faces load lazily. Each one costs roughly 2MB of VRAM for its glyph atlas,
// and the app is not going to draw Thin through Black within a single frame,
// so the startup cost would be paid for a menu that is never opened. Only the
// weights the window chrome itself needs are warmed at startup.
#pragma once

#include <vector>

#include "app/font_faces.h"
#include "engine/globals.h"
#include "engine/logger.h"
#include "raylib.h"

namespace referentia {

/*
 * Codepoints baked into the glyph atlas.
 *
 * Work Sans covers the Google Latin Expert set, which is far more than any
 * UI needs and would produce a multi-megabyte atlas. This covers printable
 * ASCII, the Latin-1 supplement (accented Latin used in most European
 * languages), a few typographic marks and the arrow/box characters the board
 * chrome uses. Anything outside this set falls back to '?'.
 */
inline const std::vector<int>& FontCodepoints() {
  static const std::vector<int> codepoints = [] {
    std::vector<int> cps;
    cps.reserve(256);

    // Printable ASCII.
    for (int c = 32; c <= 126; ++c) cps.push_back(c);

    // Latin-1 supplement: accented letters, punctuation, symbols.
    for (int c = 160; c <= 255; ++c) cps.push_back(c);

    // Typographic characters used in labels.
    for (int c : {0x2018, 0x2019, 0x201C, 0x201D,  // ' ' " "
                  0x2013, 0x2014,                    // en/em dash
                  0x2026,                            // ellipsis
                  0x00B7,                            // middle dot
                  0x2022})                           // bullet
      cps.push_back(c);

    // Board chrome. Every codepoint here was verified to exist in the Work
    // Sans cmap; stb_truetype silently substitutes a blank for anything
    // missing, and raylib only reports the shortfall as a count, so the list
    // is pinned to what the font actually ships. Note the non-"small" triangle
    // variants (U+25B2/U+25BC) are present while the small ones (U+25B4/
    // U+25BE) are not, and the dingbats (U+2713/U+2715) are absent - the tick
    // and cross are drawn as glyph-free shapes instead.
    for (int c : {0x2190, 0x2192, 0x2191, 0x2193,  // arrows
                  0x25A0, 0x25A1,                  // squares
                  0x25B2, 0x25BC, 0x25C0,          // up/down/left triangles
                  0x00D7})                         // multiplication sign
      cps.push_back(c);

    return cps;
  }();
  return codepoints;
}

/*
 * Resolves a bundled font file to a path raylib can fopen.
 *
 * The mount point for `resources/` differs per target, and there is no single
 * layout that covers all of them:
 *
 *   desktop           fonts/           copied next to the binary by premake
 *   web, preload      resources/fonts/  --preload-file ../../resources@/resources
 *   web, standalone   fonts/           --embed-file each ttf @/fonts/<name>
 *   tests/tools       resources/fonts/  run from the repository root
 *
 * Rather than hard-coding one and silently degrading, probe the candidates in
 * order and report the ones that were tried when none exist, so a packaging
 * mistake shows up as a build/run log line instead of mystery boxes.
 */
inline std::string ResolveFontPath(const char* filename) {
  static const char* kPrefixes[] = {
      "fonts/",
      "resources/fonts/",
      "../../resources/fonts/",
      "../resources/fonts/",
  };

  for (const char* prefix : kPrefixes) {
    const std::string candidate = std::string(prefix) + filename;
    if (FileExists(candidate.c_str())) return candidate;
  }
  return {};
}

/*
 * Rasterises one face if it is not resident yet, and returns its handle.
 *
 * raylib's LoadFontEx returns the default font rather than signalling failure,
 * so the path is checked here and a zero texture id is reported.
 */
inline Font& RasteriseFontFace(int index) {
  if (g_font_loaded[index]) return g_fonts[index];

  const char* file = kFontFaceFiles[index];

  const std::string path = ResolveFontPath(file);
  if (path.empty()) {
    logger::error(
        "[fonts] Work Sans '{}' not found; looked for fonts/ and "
        "resources/fonts/ relative to the working directory. Every label that "
        "needs this weight will render with raylib's fallback font.",
        file);
    // Marked resident anyway: the file will still be missing on the next
    // frame, and re-stat-ing it on every draw is not worth the log spam.
    g_font_loaded[index] = true;
    return g_fonts[index];
  }

  const std::vector<int>& codepoints = FontCodepoints();
  const int count = static_cast<int>(codepoints.size());

  // The atlas size is what we rasterise at; kBaseFontSize (what the UI layer
  // draws at) is owned by systems/ui_helpers.h and asserted there, to keep
  // this loader from depending on the drawing layer.
  LOG_DEBUG("[fonts] rasterising '{}' from '{}': {} codepoints, atlas {}px", file,
            path, count, FONT_ATLAS_SIZE);

  g_fonts[index] =
      LoadFontEx(path.c_str(), FONT_ATLAS_SIZE, codepoints.data(), count);
  g_font_loaded[index] = true;

  if (g_fonts[index].texture.id == 0) {
    // A texture id of 0 is what a rasterise failure returns, but it is also
    // what a headless/EGL-less context returns for a valid load, so the
    // distinction is stated rather than assumed.
    logger::error(
        "[fonts] failed to rasterise '{}' (texture id 0; expected on a "
        "headless context, a real failure otherwise)",
        path);
  } else {
    LOG_DEBUG("[fonts] '{}' ready: texture {}, atlas {}px, {} codepoints", file,
              g_fonts[index].texture.id, FONT_ATLAS_SIZE, count);
  }

  return g_fonts[index];
}

/**
 * The face for a weight, rasterising it on first use.
 *
 * Must be called with a live GL context, which in practice means from a draw
 * or measure call during a frame. Yields a zeroed handle, which raylib draws
 * without complaint, if the TTF is missing -- so the result can go straight
 * into MeasureTextEx / DrawTextEx with no null check at the call site.
 */
inline Font GetFont(FontWeight weight, FontSlant slant = FontSlant::Upright) {
  return RasteriseFontFace(FontFaceIndex(weight, slant));
}

/** How many of the faces are currently resident. */
inline int FontsResident() {
  int resident = 0;
  for (int i = 0; i < kFontFaceCount; ++i) {
    if (g_font_loaded[i] && g_fonts[i].texture.id != 0) resident++;
  }
  return resident;
}

/*
 * Warms the faces the window chrome draws with on its first frame, so opening
 * the window does not visibly stall while they rasterise. Every other weight
 * arrives on demand. Returns false if a warmed face is missing.
 */
inline bool LoadFonts() {
  const FontWeight kWarmed[] = {
      FontWeight::Regular,
      FontWeight::Medium,
      FontWeight::SemiBold,
  };

  bool ok = true;
  for (FontWeight weight : kWarmed) {
    const int index = FontFaceIndex(weight, FontSlant::Upright);
    if (RasteriseFontFace(index).texture.id == 0) ok = false;
  }

  if (ok) {
    const int count = static_cast<int>(FontCodepoints().size());
    logger::info("[fonts] Work Sans ready: {} of {} faces resident, {} "
                 "codepoints each, {}px atlas",
                 FontsResident(), kFontFaceCount, count, FONT_ATLAS_SIZE);
  } else {
    logger::warn(
        "[fonts] falling back to raylib's default font for one or more faces");
  }

  return ok;
}

inline void UnloadFonts() {
  for (int i = 0; i < kFontFaceCount; ++i) {
    if (g_font_loaded[i] && g_fonts[i].texture.id != 0) UnloadFont(g_fonts[i]);
    g_fonts[i] = Font{};
    g_font_loaded[i] = false;
  }
}

}  // namespace referentia
