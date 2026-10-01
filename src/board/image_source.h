// board/image_source.h
//
// Everything about deciding *whether* an image can be imported and *where* it
// lands, with no raylib and no filesystem access, so it is unit testable
// headlessly and usable from the web build's byte path as well as from a
// desktop path.
//
// Split from app/image_import.h on purpose. That file talks to raylib and to
// the platform, so nothing under test can include it; this is the half worth
// testing, and the half where a mistake is silent. A placement that is off by
// a factor of zoom still draws an image, it just draws it in the wrong place,
// and a dropped .jpg that is filtered out never appears and never complains.

#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "board/transform.h"

namespace referentia::board {

/**
 * Extensions the app will attempt to import, without the dot and lowercase.
 *
 * These have to agree with the decoders actually compiled into raylib, because
 * LoadImageFromMemory() gates its extension list on the SUPPORT_FILEFORMAT_*
 * macros and returns an empty image for anything not compiled in. The list is
 * therefore a mirror of the defines in build/premake5.lua:
 *
 *     SUPPORT_FILEFORMAT_JPG=1, SUPPORT_FILEFORMAT_TGA=1   (added by this project)
 *     SUPPORT_FILEFORMAT_PNG, BMP, GIF, QOI, DDS           (raylib defaults to on)
 *
 * raylib 6.0 ships JPEG *off*, so without the premake define a dropped .jpg
 * would be rejected deep inside the decoder and the user would see nothing at
 * all. If a format is added to one of those two places it belongs here too, and
 * the test below is the place that catches it being forgotten.
 *
 * WebP is absent deliberately: raylib 6.0 has no WebP decoder in any
 * configuration, so offering it in a file dialog filter would advertise a file
 * the app cannot open.
 */
inline constexpr std::string_view kSupportedImageExtensions[] = {
    "png", "jpg", "jpeg", "bmp", "tga", "gif", "qoi", "dds",
};

/**
 * The file-dialog filter string for the extensions above.
 *
 * Built from kSupportedImageExtensions rather than written out, so the dialog
 * cannot offer a file the app will then refuse. GLFW wants the list in raylib's
 * own form: a leading extension, or "ext;Description;Patterns".
 */
inline std::string ImageFileFilter() {
  std::string out;
  for (const std::string_view ext : kSupportedImageExtensions) {
    if (!out.empty()) out += ";";
    out += ".";
    out += ext;
  }
  return out;
}

/** Everything the import layer needs to describe one chosen file, and nothing else. */
struct ImageRequest {
  /**
   * A filesystem path, or empty on the web.
   *
   * The two are different things and are not interchangeable: a path can be
   * re-read, stat'ed and shown to the user, while browser bytes exist only in
   * memory until they are turned into a texture. The web path carries the bytes
   * in `bytes` and leaves this empty rather than inventing a path that would
   * fail if anything ever tried to open it.
   */
  std::string path;

  /** File name with no directory and no extension, for the node's label. */
  std::string name;

  /** The image itself, when it came from somewhere with no path (the web). */
  std::vector<unsigned char> bytes;

  /**
   * World point the image is centred on.
   *
   * A centre rather than a top-left corner because CentreOn() is what the rest
   * of the board uses to place content, and because a drop lands under the
   * pointer: the thing the user aimed at should end up under the pointer.
   */
  Vec2 anchor{0.f, 0.f};
};

/**
 * The lowercase extension of `path`, without the dot, or empty if it has none.
 *
 * Lowercased because the extension is compared against a lowercase table, and
 * because filesystems disagree with each other: the same image dropped on Linux
 * is "photo.JPEG" from a camera and on Windows is routinely "photo.JPG". A
 * case-sensitive comparison rejects both, which reads as the app being broken
 * on one platform and not another.
 *
 * Only the last dot counts. A file called "v1.2.final" has the extension
 * "final", and a directory called "my.images" inside the path must not be
 * mistaken for one.
 */
inline std::string LowerExtension(std::string_view path) {
  const size_t slash = path.find_last_of("/\\");
  const size_t dot = path.find_last_of('.');
  if (dot == std::string_view::npos) return {};
  // A dot at or before the last separator belongs to a directory name, not to
  // the file, so "folder.png/file" has no extension.
  if (slash != std::string_view::npos && dot < slash) return {};

  std::string out;
  for (size_t i = dot + 1; i < path.size(); ++i)
    out += static_cast<char>(
        std::tolower(static_cast<unsigned char>(path[i])));
  return out;
}

/** The file name with no directory part, for a node label. */
inline std::string FileStem(std::string_view path) {
  const size_t slash = path.find_last_of("/\\");
  const size_t start = (slash == std::string_view::npos) ? 0 : slash + 1;

  const size_t dot = path.find_last_of('.');
  const size_t end = (dot == std::string_view::npos || dot < start)
                         ? path.size()
                         : dot;
  return std::string(path.substr(start, end - start));
}

/** Whether the app will try to import this path at all. */
inline bool IsSupportedImagePath(std::string_view path) {
  const std::string ext = LowerExtension(path);
  if (ext.empty()) return false;

  for (const std::string_view candidate : kSupportedImageExtensions) {
    if (ext == candidate) return true;
  }
  return false;
}

/**
 * Longest side, in world units, that an imported image may span.
 *
 * Placement is one image pixel per world unit, which is what makes a 1:1 check
 * possible at zoom 1: that is the whole reason the board has a world coordinate
 * system rather than drawing images in screen space. But a photo straight off a
 * phone is 4000px wide and a stitched panorama is far wider, and at 1:1 those
 * land as a single rect thousands of units across -- so far off screen that the
 * node looks like it failed to import, which is exactly the moment a user needs
 * to know it did not.
 *
 * 2000 is about twenty grid cells at the board's default spacing, so a clamped
 * image is still comfortably pannable and zoomable rather than filling
 * everything. Only ever scaled *down*: a small icon stays at its true size
 * instead of being blown up into a blurry mess.
 */
inline constexpr float kMaxImageWorldExtent = 2000.f;

/**
 * World rect for an image of `pixel_w` x `pixel_h` centred on `anchor`.
 *
 * Scale is uniform, so a non-square image keeps its aspect ratio; a non-uniform
 * fit would silently distort every photo dropped on the board.
 *
 * Deliberately takes no zoom. The world size of a node is a stored property, and
 * if it were derived from the camera's current zoom then zooming the board would
 * resize every image on it, permanently, in both directions: zoom in, then out,
 * and the image is a different size than it started. Zoom changes how large a
 * node *looks*, never how big it *is*. The one place zoom legitimately appears
 * is the drop point, which is converted to world space by the caller.
 */
inline Rect ImagePlacement(int pixel_w, int pixel_h, Vec2 anchor,
                           float max_extent = kMaxImageWorldExtent) {
  Rect r{anchor.x, anchor.y, static_cast<float>(pixel_w),
         static_cast<float>(pixel_h)};

  const float longest = (r.width > r.height) ? r.width : r.height;
  if (longest > max_extent && longest > 0.f) {
    const float scale = max_extent / longest;
    r.width *= scale;
    r.height *= scale;
  }

  return CentreOn(r, anchor.x, anchor.y);
}

/**
 * Queued image imports, drained once per frame by the import layer.
 *
 * A queue rather than a call because a drop is not a thing that can be acted on
 * where it is noticed: a native file dialog blocks, and loading a texture does
 * file I/O. Both belong at one defined point in the frame, which is what
 * UpdateImageImport() is for.
 *
 * Coalesced rather than cleared. A user dragging a folder's worth of files, or
 * two drops in quick succession, must not lose the first batch to the second:
 * each request appends, and only paths not already queued are added, so the
 * same file dropped twice imports once instead of stacking two identical
 * textures on top of each other.
 */
class ImageRequestQueue {
 public:
  /** Appends `requests`, skipping paths this queue already holds. */
  void Push(std::vector<ImageRequest> requests) {
    for (ImageRequest& request : requests) {
      if (request.name.empty() && !request.path.empty())
        request.name = FileStem(request.path);

      // Duplicate detection is on the identity of the source, not the anchor:
      // the same file twice is one image, while two different files may
      // legitimately want the same spot (and the cascade below separates them).
      if (!request.path.empty() && HoldsPath(request.path)) continue;

      m_requests.push_back(std::move(request));
    }
  }

  bool Empty() const { return m_requests.empty(); }
  size_t Size() const { return m_requests.size(); }

  /**
   * Moves everything out and leaves the queue empty.
   *
   * Moved rather than copied, because this hands over a vector of paths plus,
   * on the web, the pixel data of every file in the batch.
   */
  std::vector<ImageRequest> Take() {
    std::vector<ImageRequest> out;
    out.swap(m_requests);
    return out;
  }

  /**
   * A batch is spread by this many world units per file.
   *
   * A multi-file drop that puts every image on the exact same point looks like
   * one image: the top one covers the rest and the count of what was imported
   * cannot be seen. A small diagonal cascade, equal to one row height at the
   * default scale, separates them without making the group sprawl.
   */
  static constexpr float kCascadeStep = 24.f;

  /**
   * Nudges every request but the first down-right by one cascade step.
   *
   * Applied when the batch is taken rather than when it is pushed, so a second
   * drop that arrives before the first is drained cascades from its own start
   * instead of continuing the previous batch's offset.
   */
  static void Cascade(std::vector<ImageRequest>& requests) {
    for (size_t i = 1; i < requests.size(); ++i) {
      requests[i].anchor.x += kCascadeStep * static_cast<float>(i);
      requests[i].anchor.y += kCascadeStep * static_cast<float>(i);
    }
  }

 private:
  bool HoldsPath(const std::string& path) const {
    for (const ImageRequest& held : m_requests) {
      if (held.path == path) return true;
    }
    return false;
  }

  std::vector<ImageRequest> m_requests;
};

}  // namespace referentia::board
