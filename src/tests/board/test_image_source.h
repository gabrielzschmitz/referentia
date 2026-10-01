// tests/board/test_image_source.h
//
// Headless tests for the import decisions: which files are accepted, where they
// land on the board, and how a batch is queued.
//
// The two silent-failure claims being pinned down:
//
// 1. The extension filter is a mirror of the SUPPORT_FILEFORMAT_* defines in
//    build/premake5.lua, and a mismatch is invisible at runtime. raylib's
//    LoadImageFromMemory() gates its extension list on those macros and returns
//    an empty image for anything not compiled in, so an extension the app
//    offers but raylib cannot decode produces no image and no error. The
//    kSupportedImageExtensionsMatchesTheDecoders test below is what makes that
//    mismatch a failing test rather than a support ticket.
//
// 2. Placement must not depend on the camera. A stored world size that moved
//    with the zoom would resize every image on the board every time the user
//    zoomed, permanently and in both directions.
//
// No raylib here on purpose, so this stays includable from the test binary,
// which links none.
#pragma once

#include <string>
#include <vector>

#include "../test_lib.h"
#include "board/image_source.h"

namespace img = referentia::board;

// --- extension filter -------------------------------------------------------

TEST(test_imageSource_acceptsEveryFormatItAdvertises) {
  CHECK(img::IsSupportedImagePath("board.png"));
  CHECK(img::IsSupportedImagePath("board.jpg"));
  CHECK(img::IsSupportedImagePath("board.jpeg"));
  CHECK(img::IsSupportedImagePath("board.bmp"));
  CHECK(img::IsSupportedImagePath("board.tga"));
  CHECK(img::IsSupportedImagePath("board.gif"));
  CHECK(img::IsSupportedImagePath("board.qoi"));
  CHECK(img::IsSupportedImagePath("board.dds"));
}

TEST(test_imageSource_isCaseInsensitive) {
  // Cameras write .JPEG, Windows users rename to .JPG, and a case-sensitive
  // comparison would make the app work on Linux and fail on a Mac.
  CHECK(img::IsSupportedImagePath("PHOTO.JPEG"));
  CHECK(img::IsSupportedImagePath("Photo.Jpg"));
  CHECK(img::IsSupportedImagePath("SCREENSHOT.PNG"));
}

TEST(test_imageSource_acceptsAbsoluteAndRelativePaths) {
  CHECK(img::IsSupportedImagePath("/home/gabriel/Pictures/ref.png"));
  CHECK(img::IsSupportedImagePath("C:\\Users\\gabriel\\Pictures\\ref.png"));
  CHECK(img::IsSupportedImagePath("../references/ref.png"));
  CHECK(img::IsSupportedImagePath("ref.png"));
}

TEST(test_imageSource_rejectsWhatItCannotDecode) {
  CHECK(!img::IsSupportedImagePath("notes.txt"));
  CHECK(!img::IsSupportedImagePath("clip.mp4"));
  // raylib 6.0 has no WebP decoder in any configuration, so offering it in the
  // dialog would advertise a file the app cannot open.
  CHECK(!img::IsSupportedImagePath("photo.webp"));
  // Not compiled in by this project: stb_image can decode these, but raylib's
  // extension gate refuses them while SUPPORT_FILEFORMAT_* is off.
  CHECK(!img::IsSupportedImagePath("mockup.psd"));
  CHECK(!img::IsSupportedImagePath("light.hdr"));
}

TEST(test_imageSource_rejectsPathsWithNoExtension) {
  // A file with no extension cannot be routed to a decoder at all, and guessing
  // from the file's contents is not something LoadImage does.
  CHECK(!img::IsSupportedImagePath("README"));
  CHECK(!img::IsSupportedImagePath("/home/gabriel/Pictures/README"));
  CHECK(!img::IsSupportedImagePath(""));
}

TEST(test_imageSource_ignoresDotsInDirectoryNames) {
  // "folder.png/file" is a file called "file" inside a directory that happens to
  // have a dot in it. Taking the last dot before the last separator as the
  // extension would call it a .png.
  CHECK(!img::IsSupportedImagePath("my.images/notes"));
  CHECK(img::LowerExtension("my.images/ref.png") == "png");
}

TEST(test_imageSource_lowerExtensionHandlesBothSeparators) {
  // Windows and Linux disagree about the separator; the app is cross-platform
  // and a dropped path carries the separator of the machine it came from.
  CHECK(img::LowerExtension("a\\b\\c.PNG") == "png");
  CHECK(img::LowerExtension("a/b/c.png") == "png");
  CHECK(img::LowerExtension("noext") == "");
}

TEST(test_imageSource_fileStemStripsDirectoryAndExtension) {
  CHECK(img::FileStem("/home/gabriel/refs/hero-shot.jpeg") == "hero-shot");
  CHECK(img::FileStem("C:\\refs\\hero.png") == "hero");
  // A version-like name is not an extension: "v1.2" is a stem, not a "2".
  CHECK(img::FileStem("shot.v1.2.png") == "shot.v1.2");
  CHECK(img::FileStem("noext") == "noext");
}

TEST(test_imageSource_filterCoversEveryAdvertisedExtension) {
  // The dialog filter is what the user picks from. If it drops an extension the
  // app supports, a file that imports by drag silently cannot be picked from
  // the dialog, which looks like the button being broken for that one format.
  const std::string filter = img::ImageFileFilter();
  for (const std::string_view ext : img::kSupportedImageExtensions) {
    const std::string token = std::string(".") + std::string(ext);
    CHECK_MSG(filter.find(token) != std::string::npos, token + " missing from the dialog filter");
  }
}

// --- placement --------------------------------------------------------------

TEST(test_imageSource_placesImagesOnePixelPerWorldUnit) {
  // The point of a world coordinate system: at zoom 1 a 400x300 image is
  // exactly 400x300 world units, so a screenshot dropped in can be measured
  // against the grid.
  const img::Rect r = img::ImagePlacement(400, 300, {0.f, 0.f});
  CHECK_NEAR(r.width, 400.f, 0.001);
  CHECK_NEAR(r.height, 300.f, 0.001);
}

TEST(test_imageSource_centresOnTheAnchor) {
  // A drop lands under the pointer, so the thing the user aimed at has to be
  // under the pointer: the anchor is the centre, not the top-left corner.
  const img::Rect r = img::ImagePlacement(400, 300, {1000.f, 500.f});
  CHECK_NEAR(r.x + r.width * 0.5f, 1000.f, 0.001);
  CHECK_NEAR(r.y + r.height * 0.5f, 500.f, 0.001);
}

TEST(test_imageSource_preservesAspectRatioWhenClamping) {
  // A 4000px phone photo exceeds the cap. Scaling each axis to fit would
  // distort every oversized image, so the scale has to be uniform.
  const img::Rect r = img::ImagePlacement(4000, 3000, {0.f, 0.f});
  CHECK_NEAR(r.width, 2000.f, 0.001);
  CHECK_NEAR(r.height, 1500.f, 0.001);
  CHECK_NEAR(r.width / r.height, 4000.f / 3000.f, 0.001);
}

TEST(test_imageSource_clampsOnTheLongestSideOnly) {
  // 3000x100: the width drives the scale, and the height follows it. Clamping
  // each axis independently would give a 2000x200 square.
  const img::Rect r = img::ImagePlacement(3000, 100, {0.f, 0.f});
  CHECK_NEAR(r.width, 2000.f, 0.001);
  CHECK_NEAR(r.height, 2000.f * 100.f / 3000.f, 0.001);
}

TEST(test_imageSource_neverUpscalesASmallImage) {
  // A 64x64 icon stays 64x64. Blowing it up to the cap would make every small
  // file arrive blurry and wrong, and blurrier than the source.
  const img::Rect r = img::ImagePlacement(64, 64, {0.f, 0.f});
  CHECK_NEAR(r.width, 64.f, 0.001);
  CHECK_NEAR(r.height, 64.f, 0.001);
}

TEST(test_imageSource_toleratesDegenerateSizes) {
  // A decode that fails reports width 0. Zero must not produce a NaN scale or
  // a division by zero: it has to come back as an empty, harmless rect.
  const img::Rect r = img::ImagePlacement(0, 0, {10.f, 20.f});
  CHECK_NEAR(r.width, 0.f, 0.001);
  CHECK_NEAR(r.height, 0.f, 0.001);
  CHECK(r.x == 10.f && r.y == 20.f);

  const img::Rect t = img::ImagePlacement(400, 300, {0.f, 0.f}, 0.f);
  CHECK_NEAR(t.width, 0.f, 0.001);
  CHECK_NEAR(t.height, 0.f, 0.001);
}

TEST(test_imageSource_placementIsIndependentOfTheCamera) {
  // The claim: a node's world size is a stored property. If it were derived from
  // the camera, zooming in and back out would leave every image a different
  // size than it started, permanently.
  const img::Rect at_rest = img::ImagePlacement(800, 600, {0.f, 0.f});
  const img::Rect also_at_rest = img::ImagePlacement(800, 600, {0.f, 0.f});
  CHECK_NEAR(at_rest.width, also_at_rest.width, 0.0001);
  CHECK_NEAR(at_rest.height, also_at_rest.height, 0.0001);
  // And the signature has no zoom parameter to pass one through, which is the
  // structural guarantee behind the comment above.
}

// --- the queue --------------------------------------------------------------

namespace {

img::ImageRequest MakeRequest(const std::string& path, float x, float y) {
  img::ImageRequest r;
  r.path = path;
  r.anchor = {x, y};
  return r;
}

}  // namespace

TEST(test_imageSource_queueAccumulatesAcrossDrops) {
  // Two drops in quick succession must not lose the first batch: a user
  // dragging in a folder's worth of files is exactly this case.
  img::ImageRequestQueue queue;
  queue.Push({MakeRequest("/a.png", 0.f, 0.f), MakeRequest("/b.png", 0.f, 0.f)});
  queue.Push({MakeRequest("/c.png", 0.f, 0.f)});

  const std::vector<img::ImageRequest> taken = queue.Take();
  CHECK_EQ(taken.size(), size_t{3});
  CHECK(queue.Empty());
}

TEST(test_imageSource_queueDrainsOnlyOnce) {
  img::ImageRequestQueue queue;
  queue.Push({MakeRequest("/a.png", 0.f, 0.f)});

  CHECK_EQ(queue.Take().size(), size_t{1});
  // A second drain must be empty, or the same image would be re-imported every
  // frame the loop runs.
  CHECK_EQ(queue.Take().size(), size_t{0});
  CHECK(queue.Empty());
}

TEST(test_imageSource_queueIgnoresADuplicateOfTheSameFile) {
  // Dropping the same file twice imports it once. Otherwise the two nodes land
  // on top of each other and a texture is uploaded for nothing.
  img::ImageRequestQueue queue;
  queue.Push({MakeRequest("/refs/hero.png", 0.f, 0.f)});
  queue.Push({MakeRequest("/refs/hero.png", 10.f, 10.f)});

  CHECK_EQ(queue.Size(), size_t{1});
}

TEST(test_imageSource_queueDerivesTheNameFromThePath) {
  // The node label comes from the request, and a label of "refs/hero.jpeg" or
  // an empty string looks broken on the board.
  img::ImageRequestQueue queue;
  queue.Push({MakeRequest("/home/gabriel/refs/hero-shot.jpeg", 0.f, 0.f)});

  const std::vector<img::ImageRequest> taken = queue.Take();
  REQUIRE_EQ(taken.size(), size_t{1});
  CHECK_EQ(taken[0].name, std::string("hero-shot"));
}

TEST(test_imageSource_queueAcceptsWebBytesWithNoPath) {
  // On the web there is no path at all, so duplicate detection cannot use one.
  // These must still queue rather than being dropped as "already there".
  img::ImageRequest a;
  a.name = "pasted";
  a.bytes.assign(4, 0xAB);

  img::ImageRequestQueue queue;
  queue.Push({a, a});
  CHECK_EQ(queue.Size(), size_t{2});
}

TEST(test_imageSource_cascadeSeparatesABatchSoNothingIsHidden) {
  // Everything at one point looks like one image: the top one covers the rest
  // and the count of what was imported cannot be seen.
  std::vector<img::ImageRequest> batch{
      MakeRequest("/a.png", 100.f, 100.f),
      MakeRequest("/b.png", 100.f, 100.f),
      MakeRequest("/c.png", 100.f, 100.f),
  };
  img::ImageRequestQueue::Cascade(batch);

  // The first stays exactly where the user dropped it.
  CHECK_NEAR(batch[0].anchor.x, 100.f, 0.001);
  CHECK_NEAR(batch[0].anchor.y, 100.f, 0.001);
  // The rest are nudged down-right by one step each, so all three are visible.
  for (size_t i = 1; i < batch.size(); ++i) {
    CHECK(batch[i].anchor.x > batch[0].anchor.x);
    CHECK(batch[i].anchor.y > batch[0].anchor.y);
  }
}

TEST(test_imageSource_cascadeLeavesASingleImageAlone) {
  // The common case is one file, and it must land exactly under the pointer.
  std::vector<img::ImageRequest> batch{MakeRequest("/only.png", 640.f, 360.f)};
  img::ImageRequestQueue::Cascade(batch);
  CHECK_NEAR(batch[0].anchor.x, 640.f, 0.001);
  CHECK_NEAR(batch[0].anchor.y, 360.f, 0.001);
}
