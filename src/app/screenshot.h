// app/screenshot.h
#pragma once

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

#include "engine/logger.h"
#include "raylib.h"
#include "rlgl.h"

namespace referentia::app {

/**
 * ============================================================================
 * Screenshot Capture
 * ============================================================================
 *
 * Writes the current framebuffer to screenshots/<timestamp>.png (F12, and the
 * Screenshot button on the board panel).
 *
 * Nothing captures directly. The button lives in the widget pass, which runs
 * part way through the frame, so a capture taken from there reads back a
 * half-drawn frame -- in practice the window panel with its groups and controls
 * missing, since they are drawn after the button that asked for the picture.
 * Callers queue a request with RequestScreenshot() and the draw loop serves it
 * with FlushScreenshotRequest() once the frame is complete, so every path
 * captures the same finished frame.
 *
 * LoadImageFromScreen() + ExportImage() rather than raylib's TakeScreenshot():
 * that one writes a fixed filename into the process working directory, so the
 * second press silently overwrites the first and there is no way to keep both.
 * A development screenshot is usually wanted precisely *because* something went
 * wrong, which makes overwrite the worst possible default.
 *
 * Directory and failure handling are explicit because a screenshot that fails
 * must say so. A silent no-op here looks identical to a key that does not work.
 *
 * ============================================================================
 */

namespace detail {

/** Local-time stamp, so filenames sort the way a person expects. */
inline std::string TimestampTag() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
#if defined(_WIN32)
  localtime_s(&tm_buf, &t);
#else
  localtime_r(&t, &tm_buf);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm_buf);
  return buf;
}

}  // namespace detail

/** A capture queued for the end of the frame, by F12 or the panel button. */
inline bool g_screenshot_requested = false;

inline void RequestScreenshot() {
  g_screenshot_requested = true;
}

/**
 * Captures the current frame. Returns the path written, or an empty string on
 * failure. Must be called with a live GL context, after the frame is drawn.
 */
inline std::string CaptureScreenshot(const std::string& directory = "screenshots") {
  namespace fs = std::filesystem;

  std::error_code ec;
  if (!fs::exists(directory, ec) && !fs::create_directories(directory, ec) && ec) {
    logger::error("[SHOT] cannot create {}: {}", directory, ec.message());
    return {};
  }

  // Read back inside the current frame, so the capture reflects exactly what is
  // on screen, not a later frame that may have moved on.
  //
  // The flush first, and it is not optional. raylib batches 2D draw calls and
  // only submits them in EndDrawing(), which runs *after* this: without it the
  // readback sees a framebuffer missing everything still queued, which is
  // exactly the tail of the frame -- the frame-rate panel and the pointer --
  // so the screenshot disagreed with the screen by showing the panel empty.
  rlDrawRenderBatchActive();
  Image shot = LoadImageFromScreen();
  if (shot.data == nullptr) {
    logger::error("[SHOT] TakeImage returned no pixels");
    return {};
  }

  // Second-resolution suffix: pressing F12 twice within the same second is easy
  // during an animation or a glitch, and a collision would overwrite again.
  const std::string base = directory + "/referentia-" + detail::TimestampTag();
  std::string path = base + ".png";
  int n = 2;
  while (fs::exists(path, ec)) {
    path = base + "-" + std::to_string(n++) + ".png";
  }

  // Extension in the filename selects the format; no separate format arg.
  const bool ok = ExportImage(shot, path.c_str());
  UnloadImage(shot);

  if (!ok) {
    logger::error("[SHOT] ExportImage failed for {}", path);
    return {};
  }

  logger::info("[SHOT] saved {}", path);
  return path;
}

/**
 * Serves a queued request. Called by the draw loop once the frame is complete,
 * so a capture taken from a button mid-frame still contains the whole frame.
 */
inline std::string FlushScreenshotRequest(const std::string& directory = "screenshots") {
  if (!g_screenshot_requested) return {};
  g_screenshot_requested = false;
  return CaptureScreenshot(directory);
}

}  // namespace referentia::app
