// components/node.h
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "board/gif_timing.h"
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

  /**
   * raylib Texture2D::id. 0 is raylib's "no texture", hence never valid here.
   *
   * For an animated node this tracks whichever frame is due now, and
   * ImageAnimationComponent holds the full list. The renderer therefore reads
   * one texture per node and needs to know nothing about animation; the cost is
   * that the id is a cache, and anything that changes the frame has to go
   * through the playback system rather than writing it.
   */
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

/**
 * Playback state for a node whose image has more than one frame.
 *
 * A separate component rather than more fields on ImageNodeComponent, because a
 * still image has nothing to say about playback: adding a schedule and a clock
 * to every node would put two fields on the common case that must be correct
 * and are never read, and would make "is this node animated" a question about
 * emptiness rather than a question about presence. The renderer asks whether the
 * node has one of these at all.
 *
 * `frame_texture_ids` is parallel to `timing.frame_delays_ms`, and both are
 * parallel to the frames raylib decoded, so index i of each describes the same
 * frame. They are kept in step by construction rather than by a lookup: the
 * importer fills all three from one decode, and there is no path that adds a
 * frame to one and not the others.
 *
 * Time since the node started playing, not a frame counter. See
 * board/gif_timing.h for why accumulating frames is the wrong way to do this.
 */
struct ImageAnimationComponent {
  static constexpr std::string_view Name = "ImageAnimation";

  /** One texture per decoded frame, in play order. Never empty. */
  std::vector<unsigned int> frame_texture_ids;

  /** Per-frame schedule, from board/gif_timing_parse.h. */
  board::GifTiming timing;

  /**
   * Milliseconds since playback started, accumulated from frame deltas.
   *
   * Started at 0 rather than -1 so a node is on its first frame from the moment
   * it appears, which is what a GIF does in every viewer. A node whose schedule
   * has a single frame still gets a clock; it simply never leaves frame 0.
   */
  double elapsed_ms = 0.0;

  ImageAnimationComponent() = default;
  ImageAnimationComponent(std::vector<unsigned int> ids, board::GifTiming t)
      : frame_texture_ids(std::move(ids)), timing(std::move(t)) {}

  /** The frame that should be on screen right now. */
  int current_frame() const {
    return board::FrameAt(timing, elapsed_ms);
  }
};

}  // namespace referentia::components
