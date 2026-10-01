// app/image_import.h
//
// Turns dropped or chosen files into image nodes on the board.
//
// The whole import path funnels through one deferred queue. That is not a
// stylistic choice: two of the three entry points cannot run where they are
// triggered.
//
//   • Drag/drop. GLFW delivers a dropped-file notification inside the platform
//     event loop, and LoadDroppedFiles() is only valid until UnloadDroppedFiles().
//     Decoding in the callback means doing GL work inside someone else's event
//     handler.
//   • The file dialog. NSOpenPanel, IFileOpenDialog, and gtk_file_chooser_run are
//     all modal and block. Calling any of them from the UI pass would freeze
//     mid-frame, with a partially drawn board behind the dialog.
//   • The browser. A file input is async by definition: the click returns before
//     the user has chosen anything.
//
// So every entry point only enqueues, and UpdateImageImport() drains the queue
// once per frame from the update phase where blocking is merely bad rather than
// broken. There is exactly one decode site, which is why a new entry point is a
// few lines rather than a second copy of the placement and texture rules.
#pragma once

#include <string>
#include <vector>

#include "board/gif_timing_parse.h"
#include "board/image_source.h"
#include "board/transform.h"
#include "components/node.h"
#include "entities/node.h"
#include "engine/ecs/ecs.h"
#include "engine/globals.h"
#include "engine/logger.h"
#include "raylib.h"

// The desktop dialogs. Excluded on the web, where there is no blocking file
// chooser to call at all: RequestWebImageDialog() in app/file_drop_web.h opens
// the browser's own instead, and it returns immediately rather than handing back
// paths, so the two cannot share an interface.
#if !defined(PLATFORM_WEB)
#include "app/file_dialog.h"
#endif

namespace referentia::app {

namespace board_ns = referentia::board;
namespace ec = referentia::components;
namespace ents = referentia::entities;
namespace engine = motrix::engine;

/**
 * ============================================================================
 * Image Import
 * ============================================================================
 *
 * Session-only: nodes live until the app closes. There is no save, no undo, and
 * no reload from disk, so nothing here needs to be able to recreate a node from
 * its source later -- which is what keeps ownership to a plain vector of
 * texture ids rather than a handle table.
 * ============================================================================
 */

/**
 * Imported-image state. One per app, owned by app_state.
 *
 * The queue is a member rather than a global because there is one board per
 * window and a static queue would make a second window import into the first.
 */
struct ImageImporter {
  /** Where the import happens. Set once at startup, before any update runs. */
  engine::ECS* ecs = nullptr;

  /**
   * Every texture this importer has created, in creation order.
   *
   * This is the ownership record for the whole feature, and it exists because
   * the ECS is not it: an image node stores a texture *id* (see
   * components/node.h for why it is an id and not a Texture2D), so deleting the
   * entity would leave the GPU object behind with nothing pointing at it. VRAM
   * is not reclaimed by a dropped texture, and the app just gets slower and
   * eventually fails to allocate over a long session. UnloadImportedTextures()
   * is the only correct way to stop, and it has to run before the context dies.
   *
   * Whole Texture2D values rather than bare ids, because UnloadTexture() takes
   * the struct: passing a reconstructed one whose width/height/mipmap fields are
   * zero frees the same GL object, but doing it that way is one refactor away
   * from being wrong in a way that frees nothing.
   */
  std::vector<Texture2D> textures;

  /** Pending imports, drained by UpdateImageImport(). */
  board_ns::ImageRequestQueue pending;

  int imported_count = 0;
  int failed_count = 0;

  /**
   * How many of those were animations.
   *
   * Counted separately because a GIF is the one format where dropping one file
   * places one node holding many textures, and "12 images imported" would then
   * hide a board holding far more pixels than that suggests. Read by nothing
   * yet; kept because the importer is the only place that knows the difference.
   */
  int animated_count = 0;
};

/**
 * Queues an import. Safe to call from any phase, including a UI pass.
 *
 * Rejects unsupported extensions here rather than at decode time, so a user
 * dropping twenty mixed files gets the fifteen images that exist instead of
 * twenty log lines about decoders. The web entry point is the exception: there
 * are no paths to inspect, so the bytes carry their own name and are checked
 * the same way, one level up.
 */
inline void QueueImport(ImageImporter& imp,
                        const std::vector<std::string>& paths,
                        const board_ns::Vec2& anchor) {
  std::vector<board_ns::ImageRequest> accepted;
  accepted.reserve(paths.size());

  for (const std::string& path : paths) {
    if (!board_ns::IsSupportedImagePath(path)) {
      logger::warn("Ignoring unsupported file: {}", path);
      imp.failed_count++;
      continue;
    }
    accepted.push_back({path, board_ns::FileStem(path), {}, anchor});
  }

  if (accepted.empty()) return;

  // Offset a multi-file drop so the images do not land on top of each other.
  board_ns::ImageRequestQueue::Cascade(accepted);
  imp.pending.Push(accepted);
}

/** Queues a browser drop: raw bytes and a filename, with no filesystem path. */
inline void QueueImportFromBytes(ImageImporter& imp, const std::string& name,
                                 std::vector<unsigned char> bytes,
                                 const board_ns::Vec2& anchor) {
  if (bytes.empty()) {
    logger::warn("Ignoring empty dropped file: {}", name);
    imp.failed_count++;
    return;
  }

  // The extension is checked here for the same reason it is checked for a path,
  // and it is the only way to know the format: LoadImageFromMemory is handed the
  // type as a string and has no sniffing, so a file with no usable extension
  // cannot be decoded at all. A browser always supplies the real name for a
  // drop, so this rejects only a genuinely nameless file.
  if (!board_ns::IsSupportedImagePath(name)) {
    logger::warn("Ignoring unsupported dropped file: {}", name);
    imp.failed_count++;
    return;
  }

  // path is empty, not the name: nothing on this path can be opened, and a
  // reader that found "pasted.png" in `path` would go looking for a file that
  // does not exist.
  board_ns::ImageRequest request;
  request.name = name;
  request.bytes = std::move(bytes);
  request.anchor = anchor;
  imp.pending.Push({std::move(request)});
}

/**
 * ============================================================================
 * Animated GIF Import
 * ============================================================================
 *
 * The animated path differs from the still one in three ways that are worth
 * naming, because each is a place where the obvious implementation is wrong:
 *
 *   1. Frames come back as one buffer of w*h*4*n, not as n images. Splitting it
 *      means slicing bytes, and getting the stride or the order wrong produces an
 *      image that looks plausible and is the wrong frame.
 *   2. raylib discards the delays. They are read back out of the container
 *      separately (board/gif_timing_parse.h), because LoadImageAnim* throws the
 *      array away on the way out.
 *   3. Ownership is no longer one texture per node. Every frame is a GL object
 *      that has to outlive the node, so they all go into the importer's record.
 * ============================================================================
 */

/** The maximum frames this app will decode from one file. */
inline constexpr int kMaxGifFrames = 512;

/**
 * Whether this name is a GIF.
 *
 * Case-insensitive, matching the extension check the drop path already does: a
 * .GIF from a camera is a GIF, and treating it as a still image would work
 * perfectly and quietly show only the first frame.
 */
inline bool IsGifPath(const std::string& name_or_path) {
  return board_ns::LowerExtension(name_or_path) == "gif";
}

/**
 * Imports an animated GIF, or falls through to the still path if it has one
 * frame.
 *
 * A single-frame GIF takes the still path on purpose. It is a still image that
 * happens to be spelled GIF, and putting it through here would give it a clock
 * and a schedule for an animation that does not exist, and would allocate a
 * vector for one entry.
 */
inline bool ImportOneAnimatedGif(ImageImporter& imp,
                                 const board_ns::ImageRequest& req,
                                 const std::string& label) {
  // The container is read separately from the decode, and needs the bytes
  // whichever way the image came from. Reading a file into memory twice is
  // cheap next to decoding it, and it keeps one parse serving both entry points.
  std::vector<unsigned char> bytes;
  if (req.path.empty()) {
    bytes = req.bytes;
  } else {
    // LoadFileData is raylib's, and returns a length. A file it cannot read is
    // one the still path would also fail on, so this returns false and lets the
    // caller's error message stand rather than inventing a second one.
    int data_size = 0;
    unsigned char* data = LoadFileData(req.path.c_str(), &data_size);
    if (data == nullptr || data_size <= 0) return false;
    bytes.assign(data, data + data_size);
    MemFree(data);
  }

  if (bytes.size() > static_cast<size_t>(INT32_MAX)) {
    logger::error("GIF is too large to import: {}", label);
    return false;
  }

  const board_ns::GifTiming timing =
      board_ns::ParseGifTiming(bytes.data(), bytes.size());

  // raylib's animated decode. It is handed the type as an extension, exactly as
  // LoadImageFromMemory is, because it has no sniffing either.
  int frame_count = 0;
  Image image{};
  if (req.path.empty()) {
    image = LoadImageAnimFromMemory(".gif", bytes.data(),
                                    static_cast<int>(bytes.size()), &frame_count);
  } else {
    image = LoadImageAnim(req.path.c_str(), &frame_count);
  }

  if (image.data == nullptr || image.width <= 0 || image.height <= 0 ||
      frame_count <= 0) {
    if (image.data != nullptr) UnloadImage(image);
    logger::error("Could not decode GIF: {}", label);
    return false;
  }

  // A still GIF, or a file whose frames could not be counted. The still path
  // handles it, and hands the caller one honest image instead of an animation
  // with no frames.
  if (frame_count == 1) {
    const int pixel_w = image.width;
    const int pixel_h = image.height;
    const Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id == 0) {
      logger::error("Could not upload texture to the GPU for: {}", label);
      return false;
    }
    ents::CreateImageNode(
        *imp.ecs,
        ec::NodeTransform(
            board_ns::ImagePlacement(pixel_w, pixel_h, req.anchor)),
        ec::ImageNodeComponent(texture.id, pixel_w, pixel_h, label));
    imp.textures.push_back(texture);
    return true;
  }

  // Refuse a file that would upload an unbounded number of textures. A GIF with
  // thousands of frames is a real thing to find, and at 32 bytes a pixel it is
  // gigabytes of VRAM: the app would stop allocating rather than the import
  // failing cleanly, and the user would lose the board they were working on.
  // Reported rather than clamped silently, because a truncated animation is not
  // what the file said it was.
  if (frame_count > kMaxGifFrames) {
    UnloadImage(image);
    logger::error(
        "GIF has {} frames, more than the {} this app will decode: {}",
        frame_count, kMaxGifFrames, label);
    return false;
  }

  const int pixel_w = image.width;
  const int pixel_h = image.height;
  // One frame's worth of bytes, and the stride every slice below depends on.
  // Taken from the decoded dimensions rather than from any assumption about the
  // format: a decoder that padded its rows would otherwise have every frame
  // after the first offset by the difference, which reads as a GIF that is
  // subtly corrupt rather than as a bug.
  const size_t frame_bytes = static_cast<size_t>(pixel_w) *
                             static_cast<size_t>(pixel_h) * 4u;

  std::vector<unsigned int> frame_ids;
  frame_ids.reserve(static_cast<size_t>(frame_count));

  for (int i = 0; i < frame_count; ++i) {
    Image frame{};
    frame.data = image.data + static_cast<size_t>(i) * frame_bytes;
    frame.width = pixel_w;
    frame.height = pixel_h;
    frame.mipmaps = 1;
    frame.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    const Texture2D texture = LoadTextureFromImage(frame);
    if (texture.id == 0) {
      // One failed upload means the rest will fail too, and half an animation is
      // worse than none: the node would exist and flicker through frames that
      // never arrived.
      for (const unsigned int id : frame_ids) UnloadTexture(Texture2D{id, 1, 1, 1, 0});
      UnloadImage(image);
      logger::error("Could not upload GIF frame {} to the GPU for: {}", i, label);
      return false;
    }
    imp.textures.push_back(texture);
    frame_ids.push_back(texture.id);
  }

  UnloadImage(image);

  const engine::Entity node = ents::CreateImageNode(
      *imp.ecs,
      ec::NodeTransform(board_ns::ImagePlacement(pixel_w, pixel_h, req.anchor)),
      // The first frame's id is the node's own, so everything that already reads
      // ImageNodeComponent -- the draw path's null check, anything that reports a
      // node's texture -- keeps working without knowing about animation.
      ec::ImageNodeComponent(frame_ids.front(), pixel_w, pixel_h, label));
  imp.ecs->add<ec::ImageAnimationComponent>(node,
                                            ec::ImageAnimationComponent(
                                                std::move(frame_ids), timing));

  imp.animated_count++;
  return true;
}

/**
 * Decodes one image and places a node for it.
 *
 * The whole CPU-side image is freed immediately after the texture is uploaded.
 * LoadImage allocates with malloc, and holding both a 4000x3000 Image and its
 * Texture2D for the life of the node would cost 48MB per photo the user happens
 * to have open. A single UnloadImage here covers both the file and the web
 * entry points.
 */
inline bool ImportOne(ImageImporter& imp, const board_ns::ImageRequest& req) {
  const std::string& label = req.path.empty() ? req.name : req.path;

  // A GIF is decoded by its animated entry point, which returns every frame, and
  // the rest of this function then has two cases to handle instead of one. The
  // decision is made on the extension rather than after decoding because
  // LoadImageAnim on a non-GIF is a plain LoadImage with frameCount 1, and
  // taking that path for a PNG would mean uploading one frame through the
  // animated code for every still image the user opens.
  if (IsGifPath(req.path.empty() ? req.name : req.path))
    return ImportOneAnimatedGif(imp, req, label);

  Image image{};
  if (req.path.empty()) {
    if (req.bytes.size() > static_cast<size_t>(INT32_MAX)) {
      logger::error("Dropped file is too large to import: {}", label);
      return false;
    }
    // The type argument is the extension, dot included, and dispatches the same
    // stb decoders the file path would have reached.
    const std::string type = "." + board_ns::LowerExtension(req.name);
    image = LoadImageFromMemory(type.c_str(), req.bytes.data(),
                                static_cast<int>(req.bytes.size()));
  } else {
    image = LoadImage(req.path.c_str());
  }

  // A failed decode is not an exception and is not logged by raylib: it returns
  // an empty image. Testing the pixels is therefore the *only* way to tell a
  // corrupt or unsupported file from a successful one, and skipping it is how a
  // broken import ends up as an invisible node the user cannot find.
  if (image.data == nullptr || image.width <= 0 || image.height <= 0) {
    logger::error("Could not decode image: {}", label);
    return false;
  }

  // Captured before UnloadImage: the decoded size is what the node records as
  // the source's true pixel dimensions, and a clamped 4000px image is drawn at
  // 2000 world units but is still a 4000px image.
  const int pixel_w = image.width;
  const int pixel_h = image.height;

  const Texture2D texture = LoadTextureFromImage(image);
  UnloadImage(image);

  if (texture.id == 0) {
    // Distinct from a decode failure: the pixels were fine and the upload was
    // not, so the message names the step that failed rather than the file.
    logger::error("Could not upload texture to the GPU for: {}", label);
    return false;
  }

  // Placed whether or not it is enormous: ImagePlacement clamps the drawn size
  // and the node still records the real dimensions.
  ents::CreateImageNode(
      *imp.ecs,
      ec::NodeTransform(
          board_ns::ImagePlacement(pixel_w, pixel_h, req.anchor)),
      ec::ImageNodeComponent(texture.id, pixel_w, pixel_h, label));

  imp.textures.push_back(texture);
  return true;
}

/**
 * Drains the import queue. Call once per frame, from the update phase.
 *
 * Returns the number imported this frame, so the caller can show it.
 */
inline int UpdateImageImport(ImageImporter& imp) {
  if (imp.pending.Empty() || imp.ecs == nullptr) return 0;

  const std::vector<board_ns::ImageRequest> batch = imp.pending.Take();
  int imported = 0;

  for (const board_ns::ImageRequest& req : batch) {
    if (ImportOne(imp, req)) {
      imported++;
      imp.imported_count++;
    } else {
      imp.failed_count++;
    }
  }

  return imported;
}

/**
 * Releases every texture this importer created.
 *
 * Must run while the GL context is still alive, which in the app means from the
 * update phase on the way out, after the thread pool is stopped. Calling it
 * after CloseWindow() would be a use-after-free on the driver, and not calling
 * it at all leaks every image the user imported. The clear() is what makes a
 * second call harmless.
 */
inline void UnloadImportedTextures(ImageImporter& imp) {
  for (const Texture2D& texture : imp.textures) {
    if (texture.id != 0) UnloadTexture(texture);
  }
  imp.textures.clear();
}

// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------

/**
 * Whether the current build's window system can deliver dropped files at all.
 *
 * raylib's file drop is a GLFW feature, and only the default GLFW platform
 * implements it. raylib's native Windows backend (PLATFORM_DESKTOP_WIN32) and
 * its RGFW backend (PLATFORM_DESKTOP_RGFW) have no drop callback, so
 * IsFileDropped() is a function that compiles, links and always returns false.
 * The dialog button is unaffected -- it is a direct API call, not a platform
 * event -- so a user on those backends loses drag and drop and nothing else.
 *
 * Asking rather than assuming is the point: the alternative is a silent no-op
 * that is indistinguishable from a bug.
 */
#if defined(PLATFORM_DESKTOP_WIN32) || defined(PLATFORM_DESKTOP_RGFW)
inline constexpr bool kFileDropSupported = false;
inline constexpr const char* kFileDropUnsupportedReason =
    "This build uses raylib's native platform backend, which has no file-drop "
    "support. Use the Open image... button instead.";
#else
inline constexpr bool kFileDropSupported = true;
inline constexpr const char* kFileDropUnsupportedReason = "";
#endif

/**
 * Polls for dropped files and queues them under the pointer.
 *
 * Called every frame from the update phase, not from a drop event. raylib's
 * IsFileDropped()/LoadDroppedFiles() pair is a poll of a queue GLFW filled, and
 * the loaded list is only valid until UnloadDroppedFiles(). Reading it here and
 * queueing means the decode happens later, at one defined point, with the drop
 * point -- the pointer position now -- recorded as data rather than read later
 * from a cursor that has moved on.
 *
 * The unload is unconditional and runs even when nothing is queued, because the
 * paths GLFW allocated live in its own heap and the app is not the owner.
 */
inline void PollDroppedFiles(ImageImporter& imp, const Camera2D& cam) {
#if defined(PLATFORM_WEB)
  // The web path arrives through app/file_drop_web.h as raw bytes; raylib's
  // drop list there holds names, not contents, and LoadImage cannot open a name.
  (void)imp;
  (void)cam;
#else
  if (!IsFileDropped()) return;

  const FilePathList dropped = LoadDroppedFiles();
  if (dropped.count > 0) {
    // GetMousePosition() returns a raylib Vector2, so the fields are read
    // individually rather than assigning one Vec2 to the other. The drop point is
    // the pointer now, not where it was when GLFW queued the event, and the
    // whole reason this is a poll is that reading the position later would get
    // wherever the user has moved to since.
    const Vector2 pointer = GetMousePosition();
    const board_ns::Vec2 anchor = board_ns::ScreenToWorldCentre(
        pointer.x, pointer.y, cam.target.x, cam.target.y, cam.zoom,
        static_cast<float>(GetScreenWidth()),
        static_cast<float>(GetScreenHeight()));

    std::vector<std::string> paths;
    paths.reserve(dropped.count);
    for (int i = 0; i < dropped.count; ++i) paths.emplace_back(dropped.paths[i]);

    QueueImport(imp, paths, anchor);
  }
  UnloadDroppedFiles(dropped);
#endif
}

// ---------------------------------------------------------------------------
// Deferred dialog requests
// ---------------------------------------------------------------------------

/**
 * A "pick some files" request raised from a widget handler.
 *
 * Deferred for the same reason a screenshot is: the button's handler runs inside
 * the widget pass, part way through the frame, and all three desktop dialogs
 * (NSOpenPanel, IFileOpenDialog, gtk_file_chooser_run) block until the user has
 * finished with them. Opening one there would freeze the app mid-frame with a
 * half-drawn board behind it, and on a compositor that does not redraw until the
 * frame is presented the user would be staring at a frozen image for the whole
 * time they had the dialog open.
 *
 * A flag rather than a callback so the widget pass cannot capture anything: the
 * request has no arguments, and the file list arrives a long time later.
 */
inline bool g_image_dialog_requested = false;

inline void RequestImageDialog() { g_image_dialog_requested = true; }

inline bool ImageDialogRequested() { return g_image_dialog_requested; }

/**
 * Serves a pending dialog request: opens the chooser and queues what was picked.
 *
 * `anchor` is where the images land, normally the centre of the viewport, since
 * there is no drop position to read from a dialog. Returns the number of files
 * the user chose, or 0 for a cancel.
 *
 * The flag is cleared before the dialog opens rather than after, so a second
 * click on the button while the first dialog is open cannot leave a request
 * queued behind it. That would pop a second, unexpected dialog the moment the
 * first closes.
 */
inline int FlushImageDialogRequest(ImageImporter& imp, board_ns::Vec2 anchor) {
  if (!g_image_dialog_requested) return 0;
  g_image_dialog_requested = false;

#if defined(PLATFORM_WEB)
  // No blocking chooser exists here; the browser opens its own and the bytes
  // arrive later through the drop bridge. Nothing to flush, and saying so is
  // better than pretending the desktop path ran.
  engine::LOG_DEBUG("[IMPORT] dialog requests are handled by the browser");
  return 0;
#else
  std::vector<std::string> paths;
  if (!OpenImageFileDialog(paths)) return 0;

  QueueImport(imp, paths, anchor);
  return static_cast<int>(paths.size());
#endif
}

}  // namespace referentia::app
