// components/node.h
#pragma once

#include <string>
#include <string_view>

#include "board/transform.h"
#include "components/tags.h"

namespace referentia::components {

/**
 * ============================================================================
 * Node Components
 * ============================================================================
 *
 * What every placed thing on the board is: a transform plus whatever payload
 * the thing needs to draw itself. TagNode marks the category, so a system can
 * select nodes without knowing which payload they carry.
 *
 * Raylib-free, like components/board.h: these are plain data describing board
 * geometry, which keeps them includable from the headless test target. The
 * renderer converts at the draw boundary. A Texture2D in here would be more
 * convenient at the call site and would make every test that touches a node need
 * an OpenGL context, for a field nothing but the renderer reads.
 * ============================================================================
 */

/**
 * Position and size of a node on the board.
 *
 * A world-space rect whose origin is the top-left corner, as Rect is everywhere
 * else in the codebase. Rotation is a separate field rather than baked into the
 * rect, which is the one decision that makes rotation work at all: the bounding
 * box of a rotated rectangle is not that rectangle, so folding the angle into the
 * rect would make the stored size meaningless and would squash the image into
 * its bounding box every time it turned. Keeping them apart means a 16:9
 * reference stays 16:9 at any angle, which is the point of a reference board.
 *
 * The rect is the *unrotated* box and `rotation` is about its centre. Both are
 * written by the interaction system (systems/image_nodes.h) while a node is
 * dragged or turned; the renderer reads the pair as-is.
 */
struct NodeTransform {
  static constexpr std::string_view Name = "NodeTransform";

  board::Rect rect{0.f, 0.f, 0.f, 0.f};

  /** Radians, about the rect's centre. 0 for every node placed so far. */
  float rotation = 0.f;

  NodeTransform() = default;
  NodeTransform(board::Rect r, float rot = 0.f) : rect(r), rotation(rot) {}

  /** Convenience for the common "centred on this point" case. */
  static NodeTransform Centred(float centre_x, float centre_y, float width,
                               float height) {
    return NodeTransform{
        board::CentreOn({centre_x, centre_y, width, height}, centre_x, centre_y)};
  }
};

/**
 * A placed image: the GPU texture and the source it came from.
 *
 * The texture is an id rather than a Texture2D so this stays headless (see the
 * file header), which means someone has to own the GL object's lifetime. The
 * answer is app/image_import.h, which registers every texture it creates and
 * unloads them in one pass at shutdown, after the thread pool is stopped and
 * while the GL context is still alive. An entity holding a texture id that
 * nothing unloads would leak VRAM on every drop, and VRAM leaks do not announce
 * themselves -- the app just gets slower over a long session.
 */
struct ImageNodeComponent {
  static constexpr std::string_view Name = "ImageNode";

  /** raylib Texture2D::id. 0 is raylib's "no texture", hence never valid here. */
  unsigned int texture_id = 0;

  /** The image's own pixel size, before any placement scaling. */
  int pixel_width = 0;
  int pixel_height = 0;

  /**
   * Where it came from: a filesystem path, or a filename on the web where there
   * is no path. Kept rather than discarded because the node needs a label, and a
   * node on a reference board with no name is a picture the user cannot find
   * again. Never a directory, never a bare id.
   */
  std::string source;

  ImageNodeComponent() = default;
  ImageNodeComponent(unsigned int id, int pixel_w, int pixel_h,
                     std::string src = {})
      : texture_id(id),
        pixel_width(pixel_w),
        pixel_height(pixel_h),
        source(std::move(src)) {}
};

}  // namespace referentia::components
