// app/font_faces.h
#pragma once

#include <cstddef>

/*
 * ============================================================================
 * The Work Sans face table
 * ============================================================================
 *
 * Which TTF backs which weight and slant, and nothing else.
 *
 * Deliberately free of any raylib include. This is pure data, and keeping it
 * that way is what lets the headless test binary (which links no raylib)
 * assert the table is complete, non-duplicated, and that every file it names
 * is actually on disk -- a renamed or missing TTF is otherwise only visible as
 * boxes on screen.
 *
 * app/fonts.h owns rasterising the table; the drawing layer just asks for a
 * weight and gets whichever slant it passes.
 * ============================================================================
 */

namespace referentia {

/** The nine Work Sans weights, thinnest first. */
enum class FontWeight {
  Thin,
  ExtraLight,
  Light,
  Regular,
  Medium,
  SemiBold,
  Bold,
  ExtraBold,
  Black,
  Count,
};

/** Upright, or the matching italic. */
enum class FontSlant { Upright, Italic, Count };

inline constexpr int kFontWeightCount = static_cast<int>(FontWeight::Count);
inline constexpr int kFontSlantCount = static_cast<int>(FontSlant::Count);
inline constexpr int kFontFaceCount = kFontWeightCount * kFontSlantCount;

/** Index into the flat face table. */
inline constexpr int FontFaceIndex(FontWeight weight, FontSlant slant) {
  return static_cast<int>(weight) * kFontSlantCount + static_cast<int>(slant);
}

/**
 * Filename of one face, relative to the fonts directory.
 *
 * Work Sans ships the upright Regular as "WorkSans-Regular.ttf" and its italic
 * as "WorkSans-Italic.ttf", with the weight spelled out on every other face
 * ("WorkSans-SemiBoldItalic.ttf"). So Regular is the one weight whose upright
 * name has no weight in it, which is the only reason this is a function rather
 * than a plain literal array.
 */
inline const char* FontFaceFile(FontWeight weight, FontSlant slant) {
  switch (weight) {
    case FontWeight::Thin:
      return slant == FontSlant::Italic ? "WorkSans-ThinItalic.ttf"
                                        : "WorkSans-Thin.ttf";
    case FontWeight::ExtraLight:
      return slant == FontSlant::Italic ? "WorkSans-ExtraLightItalic.ttf"
                                        : "WorkSans-ExtraLight.ttf";
    case FontWeight::Light:
      return slant == FontSlant::Italic ? "WorkSans-LightItalic.ttf"
                                        : "WorkSans-Light.ttf";
    case FontWeight::Regular:
      return slant == FontSlant::Italic ? "WorkSans-Italic.ttf"
                                        : "WorkSans-Regular.ttf";
    case FontWeight::Medium:
      return slant == FontSlant::Italic ? "WorkSans-MediumItalic.ttf"
                                        : "WorkSans-Medium.ttf";
    case FontWeight::SemiBold:
      return slant == FontSlant::Italic ? "WorkSans-SemiBoldItalic.ttf"
                                        : "WorkSans-SemiBold.ttf";
    case FontWeight::Bold:
      return slant == FontSlant::Italic ? "WorkSans-BoldItalic.ttf"
                                        : "WorkSans-Bold.ttf";
    case FontWeight::ExtraBold:
      return slant == FontSlant::Italic ? "WorkSans-ExtraBoldItalic.ttf"
                                        : "WorkSans-ExtraBold.ttf";
    case FontWeight::Black:
      return slant == FontSlant::Italic ? "WorkSans-BlackItalic.ttf"
                                        : "WorkSans-Black.ttf";
    case FontWeight::Count:
      break;
  }
  return "";
}

/** Every face, thinnest/lightest first, so index order is the load order. */
inline const char* const kFontFaceFiles[kFontFaceCount] = {
    "WorkSans-Thin.ttf",          "WorkSans-ThinItalic.ttf",
    "WorkSans-ExtraLight.ttf",    "WorkSans-ExtraLightItalic.ttf",
    "WorkSans-Light.ttf",         "WorkSans-LightItalic.ttf",
    "WorkSans-Regular.ttf",       "WorkSans-Italic.ttf",
    "WorkSans-Medium.ttf",        "WorkSans-MediumItalic.ttf",
    "WorkSans-SemiBold.ttf",      "WorkSans-SemiBoldItalic.ttf",
    "WorkSans-Bold.ttf",          "WorkSans-BoldItalic.ttf",
    "WorkSans-ExtraBold.ttf",     "WorkSans-ExtraBoldItalic.ttf",
    "WorkSans-Black.ttf",         "WorkSans-BlackItalic.ttf",
};

}  // namespace referentia
