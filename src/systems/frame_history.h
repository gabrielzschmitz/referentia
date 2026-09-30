// systems/frame_history.h
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

/**
 * ============================================================================
 * Frame Time History
 * ============================================================================
 *
 * A fixed-capacity ring buffer of recent frame times, in milliseconds.
 *
 * Deliberately raylib-free so the test target can exercise it: the graph that
 * draws it is trivial, but "does the buffer actually keep the newest sample at
 * the right end" and "does it stop growing" are exactly the mistakes a graph
 * makes invisible. Those are worth a test, and a test cannot link raylib.
 *
 * A ring buffer rather than std::vector: Push is called every frame on the
 * render thread, and a fixed array means no allocation, no reallocation spike
 * landing in the middle of a frame-time measurement, and a hard bound on
 * memory that cannot grow if the app is ever left running.
 *
 * Newest sample last, always. At(0) is the oldest retained sample and
 * At(size() - 1) is the current frame, which is the order the graph plots.
 *
 * ============================================================================
 */
namespace referentia::systems {

class FrameTimeHistory {
 public:
  /**
   * Samples retained.
   *
   * 240 is four seconds at 60Hz, which is long enough to see a hitch scroll
   * past and still be on screen when you look, and short enough that a single
   * 30-second stall does not squash the whole graph into one spike column.
   */
  static constexpr int kCapacity = 240;

  /**
   * Records one frame.
   *
   * Non-finite and negative values are clamped to zero. GetFrameTime() is the
   * only source and should never return one, but a NaN reaching the graph would
   * poison Max() and then every y-coordinate derived from it, blanking the
   * panel for the rest of the run with no way to tell why.
   */
  void Push(float ms) {
    // isfinite, not just >= 0: +inf passes a non-negative test and would then
    // win Max() forever, pinning the graph's ceiling at infinity and collapsing
    // every real sample onto the floor. NaN fails >= and so was already caught,
    // but the two degenerate values belong in the same guard.
    if (!std::isfinite(ms) || ms < 0.f) ms = 0.f;
    samples_[head_] = ms;
    head_ = (head_ + 1) % kCapacity;
    if (count_ < kCapacity) ++count_;
  }

  /** Number of samples currently retained, 0 to kCapacity. */
  int size() const { return count_; }

  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == kCapacity; }

  /**
   * Sample by age: At(0) is the oldest retained, At(size() - 1) the newest.
   * Out-of-range reads return 0 so a caller can index a partially filled window
   * without a bounds check of its own.
   */
  float At(int i) const {
    if (i < 0 || i >= count_) return 0.f;
    const int start = (head_ - count_ + kCapacity) % kCapacity;
    return samples_[(start + i) % kCapacity];
  }

  /** The current frame, or 0 when nothing has been recorded yet. */
  float Latest() const { return count_ ? At(count_ - 1) : 0.f; }

  float MaxMs() const {
    float worst = 0.f;
    for (int i = 0; i < count_; ++i) worst = std::max(worst, At(i));
    return worst;
  }

  float AvgMs() const {
    if (count_ == 0) return 0.f;
    float sum = 0.f;
    for (int i = 0; i < count_; ++i) sum += At(i);
    return sum / static_cast<float>(count_);
  }

  /**
   * Samples strictly above the budget.
   *
   * This is the number worth surfacing: a frame that missed the budget is a
   * visible stutter, whereas a high average is often just a low refresh rate.
   */
  int OverBudgetCount(float budget_ms) const {
    int n = 0;
    for (int i = 0; i < count_; ++i)
      if (At(i) > budget_ms) ++n;
    return n;
  }

  /** Drops every sample. Used when the graph is toggled back on. */
  void Clear() {
    head_ = 0;
    count_ = 0;
  }

 private:
  std::array<float, kCapacity> samples_{};
  int head_ = 0;  // where the next sample goes
  int count_ = 0; // how many are valid, once full always kCapacity
};

}  // namespace referentia::systems
