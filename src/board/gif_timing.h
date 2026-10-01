// board/gif_timing.h
//
// Which frame of an animated image is on screen at a given moment, and how that
// is decided. Raylib-free, so it is unit tested headlessly like the rest of
// board/.
//
// This is deliberately *only* timing. The pixels come from
// stbi_load_gif_from_memory, reached through raylib's LoadImageAnim*, which
// hands back every frame as a full-size RGBA image; all that is left to work
// out is when to show which one, and that is arithmetic over a list of
// per-frame delays. Keeping the arithmetic here rather than next to the texture
// upload means the part with the interesting edge cases -- a zero delay, a
// looping file, a stalled frame -- is testable without a GL context, which is
// the only kind of test this project can run in CI.
#pragma once

#include <cstddef>
#include <vector>

namespace referentia::board {

/**
 * ============================================================================
 * GIF Timing
 * ============================================================================
 *
 * One rule, applied in a few places: a delay is a *duration*, and durations do
 * not accumulate into drift. A node does not advance a frame counter by "one
 * per N frames at 60fps"; it holds a time and asks which frame that time falls
 * in. Frame-rate-dependent playback is the classic way an animation plays at
 * the wrong speed on a machine that is not running at 60fps, and a reference
 * board is exactly the app where a GIF playing fast is a GIF the user is
 * measuring something against.
 * ============================================================================
 */

/** One frame's on-screen duration, in milliseconds. Never zero in practice. */
using FrameDelayMs = int;

/**
 * A frame's delay substituted for a nonsensical one.
 *
 * A GIF frame can legally declare a delay of 0 or 1 centisecond, which browsers
 * and image viewers universally render as the shortest delay they can rather
 * than as literal zero. Substituting here rather than special-casing it at each
 * use means a file full of zero-delay frames plays at a legible speed instead of
 * flashing past, and it is also what keeps the loop maths below from dividing by
 * a zero total duration.
 */
inline constexpr FrameDelayMs kMinFrameDelayMs = 10;

/**
 * Per-frame delays plus the file's loop count.
 *
 * `loop_count` follows the GIF application extension: 0 means loop forever, and
 * any positive number is that many *additional* plays after the first, so a
 * count of 1 plays twice. It is stored raw rather than converted to a total
 * because that is the only place the raw value is used.
 */
struct GifTiming {
  std::vector<FrameDelayMs> frame_delays_ms;
  int loop_count = 0;

  bool animated() const { return frame_delays_ms.size() > 1; }
};

/**
 * Clamps a decoded delay to something that can actually be displayed.
 *
 * Public because the decoder is not the only thing that can produce a delay --
 * this is the single definition of what a usable delay is, and a second caller
 * inventing its own minimum is how two nodes end up playing the same file at
 * different speeds.
 */
inline FrameDelayMs SanitisedDelay(int raw_ms) {
  if (raw_ms < kMinFrameDelayMs) return kMinFrameDelayMs;
  return raw_ms;
}

/**
 * Total run time of one pass through the animation, in milliseconds.
 *
 * Zero for a single-frame image, which has no timeline at all. The loop maths
 * below is written so a zero total can never be used as a divisor: an empty or
 * one-frame timing returns frame 0 without consulting the total.
 */
inline int TotalDurationMs(const GifTiming& timing) {
  int total = 0;
  for (const FrameDelayMs delay : timing.frame_delays_ms) total += delay;
  return total;
}

/**
 * Index of the frame that should be showing at `elapsed_ms` into the animation.
 *
 * `elapsed_ms` is time since the node started playing, and is allowed to exceed
 * the total: that is what happens on the frame after the last one, and clamping
 * is the whole point -- a GIF left running must not run off the end of its own
 * array.
 *
 * For a file that loops, time wraps into the first pass. For one that does not,
 * the last frame is held for ever once the animation has finished, which is the
 * correct rendering of "played N times and stopped": showing frame 0 again would
 * restart a GIF that was over, and freezing on the last frame is what every
 * image viewer does.
 */
inline int FrameAt(const GifTiming& timing, double elapsed_ms) {
  const size_t frames = timing.frame_delays_ms.size();
  if (frames == 0) return 0;
  if (frames == 1) return 0;

  const int total = TotalDurationMs(timing);
  if (total <= 0) return 0;

  // A negative elapsed cannot happen from GetTime(), but clamping here rather
  // than trusting the caller means the modulo below can never be negative,
  // which is the one case where a wrap produces a negative index.
  if (elapsed_ms < 0.0) return 0;

  if (timing.loop_count <= 0) {
    // Loops forever: wrap into the first pass.
    const int wrapped = static_cast<int>(elapsed_ms) % total;
    int acc = 0;
    for (size_t i = 0; i < frames; ++i) {
      acc += timing.frame_delays_ms[i];
      if (wrapped < acc) return static_cast<int>(i);
    }
    return static_cast<int>(frames - 1);
  }

  // Loops a finite number of times, so the schedule repeats and then stops. The
  // wrap has to happen per pass, not once across the whole timeline: flattening
  // it would leave every frame after the first one unreachable, because the
  // running total would keep growing past the end of a single pass.
  const int total_plays = timing.loop_count + 1;
  const int end_ms = total * total_plays;
  if (elapsed_ms >= static_cast<double>(end_ms)) {
    // Finished: hold the last frame. Restarting would make a GIF that played
    // N times begin again on its own, and every image viewer holds instead.
    return static_cast<int>(frames - 1);
  }

  const int wrapped = static_cast<int>(elapsed_ms) % total;
  int acc = 0;
  for (size_t i = 0; i < frames; ++i) {
    acc += timing.frame_delays_ms[i];
    if (wrapped < acc) return static_cast<int>(i);
  }
  return static_cast<int>(frames - 1);
}

/**
 * Number of times the animation plays in total, or 0 for a file that never ends.
 *
 * Separated from the wrap arithmetic above so the "has it finished" question has
 * one answer, and so a caller can tell "still playing" from "stopped on the
 * last frame" without re-deriving the end time.
 */
inline int TotalPlays(const GifTiming& timing) {
  if (!timing.animated()) return 0;
  if (timing.loop_count <= 0) return 0;  // 0 == forever
  return timing.loop_count + 1;
}

/** Whether an animation at `elapsed_ms` has run out of plays. */
inline bool FinishedPlaying(const GifTiming& timing, double elapsed_ms) {
  const int plays = TotalPlays(timing);
  if (plays == 0) return false;
  const int end_ms = TotalDurationMs(timing) * plays;
  return elapsed_ms >= static_cast<double>(end_ms);
}

}  // namespace referentia::board
