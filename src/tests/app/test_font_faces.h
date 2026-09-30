// tests/app/test_font_faces.h
//
// Headless tests for the Work Sans face table (src/app/font_faces.h).
//
// The loader hands raylib a filename, gets back a Font, and hands it to
// DrawTextEx. If the filename is wrong, nothing fails: the build is clean, the
// log is clean, and the weight simply does not appear on screen. So the
// mapping is pinned here instead.
//
// This suite links no raylib, which is why the face table lives in its own
// raylib-free header (see font_faces.h). It also touches the filesystem,
// because a table entry pointing at a TTF that is not in resources/fonts is
// exactly the failure that is invisible until someone runs the app.
#pragma once

#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>

#include "../test_lib.h"
#include "app/font_faces.h"

namespace ff = referentia;

// The fonts live next to the repository root, which is where the test binary is
// run from. The candidates mirror the head of app::ResolveFontPath so the test
// passes for the same reason the app resolves.
static std::string FontFileOnDisk(const char* filename) {
  const char* kPrefixes[] = {"resources/fonts/", "../../resources/fonts/",
                             "fonts/"};
  for (const char* prefix : kPrefixes) {
    const std::string candidate = std::string(prefix) + filename;
    std::FILE* f = std::fopen(candidate.c_str(), "rb");
    if (f != nullptr) {
      std::fclose(f);
      return candidate;
    }
  }
  return {};
}

TEST(test_font_faces_CountIsNineWeightsByTwoSlants) {
  CHECK_EQ(ff::kFontWeightCount, 9);
  CHECK_EQ(ff::kFontSlantCount, 2);
  CHECK_EQ(ff::kFontFaceCount, 18);
  CHECK_EQ(static_cast<int>(sizeof(ff::kFontFaceFiles) / sizeof(char*)),
           ff::kFontFaceCount);
}

TEST(test_font_faces_WeightsAreThinnestToBoldest) {
  // The enum order is the table order, and GetFont indexes by it, so the
  // sequence has to be monotonic. Reversed would still "work" and just mean
  // Bold rendered at Thin, which no test of the rendering would catch.
  const ff::FontWeight kExpected[] = {
      ff::FontWeight::Thin,      ff::FontWeight::ExtraLight,
      ff::FontWeight::Light,     ff::FontWeight::Regular,
      ff::FontWeight::Medium,    ff::FontWeight::SemiBold,
      ff::FontWeight::Bold,      ff::FontWeight::ExtraBold,
      ff::FontWeight::Black,
  };
  for (int i = 0; i < ff::kFontWeightCount; ++i) {
    CHECK_EQ(static_cast<int>(kExpected[i]), static_cast<int>(i));
  }
}

TEST(test_font_faces_IndexIsUniquePerWeightAndSlant) {
  std::set<int> seen;
  for (int w = 0; w < ff::kFontWeightCount; ++w) {
    for (int s = 0; s < ff::kFontSlantCount; ++s) {
      const int index = ff::FontFaceIndex(static_cast<ff::FontWeight>(w),
                                          static_cast<ff::FontSlant>(s));
      CHECK_MSG(index >= 0 && index < ff::kFontFaceCount,
                "FontFaceIndex is out of range");
      CHECK_NE(index < 0, true);
      CHECK_MSG(seen.insert(index).second, "two faces share one index");
    }
  }
  CHECK_EQ(static_cast<int>(seen.size()), ff::kFontFaceCount);
}

TEST(test_font_faces_ArrayMatchesTheLookupFunction) {
  // The array is what the loader actually reads; the function is what a caller
  // reads. If they disagree, the weight asked for and the weight loaded are
  // different faces, and both look plausible.
  for (int w = 0; w < ff::kFontWeightCount; ++w) {
    for (int s = 0; s < ff::kFontSlantCount; ++s) {
      const auto weight = static_cast<ff::FontWeight>(w);
      const auto slant = static_cast<ff::FontSlant>(s);
      CHECK_EQ(std::string(ff::kFontFaceFiles[ff::FontFaceIndex(weight, slant)]),
               std::string(ff::FontFaceFile(weight, slant)));
    }
  }
}

TEST(test_font_faces_NoNameIsUsedTwice) {
  std::set<std::string> seen;
  for (int i = 0; i < ff::kFontFaceCount; ++i) {
    const std::string name = ff::kFontFaceFiles[i];
    CHECK_MSG(!name.empty(), "empty font filename");
    CHECK_MSG(seen.insert(name).second,
              ("duplicate font filename: " + name).c_str());
  }
}

TEST(test_font_faces_NamesFollowTheWorkSansConvention) {
  // Every face is "WorkSans-<Weight>.ttf" with "Italic" before the
  // extension, except the upright Regular, which the family ships without the
  // weight spelled out. Pinned because that exception is the only reason
  // FontFaceFile is a function and not a literal array.
  for (int w = 0; w < ff::kFontWeightCount; ++w) {
    for (int s = 0; s < ff::kFontSlantCount; ++s) {
      const auto weight = static_cast<ff::FontWeight>(w);
      const auto slant = static_cast<ff::FontSlant>(s);
      const std::string name = ff::FontFaceFile(weight, slant);

      // Regular is the one weight the family leaves unnamed, so it is the one
      // weight whose filename does not spell the weight out -- in either
      // slant. This is the only reason FontFaceFile is a function and not a
      // literal array.
      const bool is_regular = weight == ff::FontWeight::Regular;

      CHECK_EQ(name.rfind("WorkSans-", 0), 0u);
      CHECK_EQ(name.substr(name.size() - 4), ".ttf");

      if (slant == ff::FontSlant::Italic) {
        CHECK_MSG(name.find("Italic.ttf") != std::string::npos,
                  ("italic face without the Italic marker: " + name).c_str());
        // Only the regular italic drops the weight, so "WorkSans-Italic.ttf"
        // must not be some other weight's name.
        if (!is_regular) {
          CHECK_MSG(name != "WorkSans-Italic.ttf",
                    ("only Regular ships a bare italic name: " + name).c_str());
        }
      } else if (!is_regular) {
        CHECK_MSG(name.find("Italic") == std::string::npos,
                  ("upright face carrying the Italic marker: " + name).c_str());
        CHECK_MSG(name != "WorkSans-Regular.ttf",
                  ("only Regular ships a bare upright name: " + name).c_str());
      }
    }
  }
  CHECK_EQ(std::string(ff::FontFaceFile(ff::FontWeight::Regular,
                                        ff::FontSlant::Upright)),
           std::string("WorkSans-Regular.ttf"));
  CHECK_EQ(std::string(ff::FontFaceFile(ff::FontWeight::Regular,
                                        ff::FontSlant::Italic)),
           std::string("WorkSans-Italic.ttf"));
}

TEST(test_font_faces_EveryNamedFileExists) {
  // The check that matters. A typo here is invisible in every other sense.
  for (int i = 0; i < ff::kFontFaceCount; ++i) {
    const char* name = ff::kFontFaceFiles[i];
    CHECK_MSG(!FontFileOnDisk(name).empty(),
              ("Work Sans face named by the table is not on disk: " +
               std::string(name))
                  .c_str());
  }
}

TEST(test_font_faces_BundledFontsAreAllAccountedFor) {
  // The reverse direction: a TTF in resources/fonts that the table does not
  // name is dead weight shipped to every user, and is usually a sign the
  // table was half-updated when a weight was added or renamed.
  std::error_code ec;
  const std::filesystem::path dir{"resources/fonts"};
  if (!std::filesystem::is_directory(dir, ec)) {
    // Running from somewhere other than the repository root; the
    // exists-on-disk test above already covers the forward direction.
    return;
  }

  std::set<std::string> named(ff::kFontFaceFiles,
                             ff::kFontFaceFiles + ff::kFontFaceCount);
  int checked = 0;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".ttf") continue;
    const std::string filename = entry.path().filename().string();
    CHECK_MSG(named.count(filename) == 1,
              ("TTF in resources/fonts is not in the face table: " + filename)
                  .c_str());
    checked++;
  }

  CHECK_EQ(checked, ff::kFontFaceCount);
}
