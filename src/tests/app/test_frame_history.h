// tests/app/test_frame_history.h
//
// Headless tests for FrameTimeHistory (src/systems/frame_history.h).
//
// The graph that draws this buffer is a dozen lines of DrawLineV, and none of
// it is worth testing. The buffer is where the mistakes live, and every one of
// them is invisible on screen: a plot that drifts left as it fills, a graph
// whose height is computed from the wrong sample, an over-budget count that
// quietly never fires. All of those look like a working graph.
//
// Hence this suite. It links no raylib, which is why the buffer is in its own
// raylib-free header.
#pragma once

#include <cmath>
#include <limits>

#include "../test_lib.h"
#include "systems/frame_history.h"

namespace fh = referentia::systems;

namespace {

constexpr int kCap = fh::FrameTimeHistory::kCapacity;

}  // namespace

TEST(frame_history_starts_empty) {
  fh::FrameTimeHistory h;
  CHECK_EQ(h.size(), 0);
  CHECK(h.empty());
  CHECK(!h.full());
  // Every aggregate has to answer sanely with nothing in it, because the panel
  // draws before the first push has landed.
  CHECK_NEAR(h.MaxMs(), 0.f, 1e-6);
  CHECK_NEAR(h.AvgMs(), 0.f, 1e-6);
  CHECK_NEAR(h.Latest(), 0.f, 1e-6);
  CHECK_EQ(h.OverBudgetCount(16.7f), 0);
  // Out-of-range reads are defined, not undefined.
  CHECK_NEAR(h.At(0), 0.f, 1e-6);
  CHECK_NEAR(h.At(-1), 0.f, 1e-6);
}

TEST(frame_history_push_keeps_newest_last) {
  fh::FrameTimeHistory h;
  h.Push(10.f);
  h.Push(20.f);
  h.Push(30.f);

  CHECK_EQ(h.size(), 3);
  // Index 0 is the OLDEST retained sample, the last index is the current frame.
  // The graph plots in this order, so getting it backwards scrolls the plot
  // right-to-left and a spike appears where nothing happened.
  CHECK_NEAR(h.At(0), 10.f, 1e-6);
  CHECK_NEAR(h.At(1), 20.f, 1e-6);
  CHECK_NEAR(h.At(2), 30.f, 1e-6);
  CHECK_NEAR(h.Latest(), 30.f, 1e-6);
}

TEST(frame_history_reads_past_the_end_return_zero) {
  fh::FrameTimeHistory h;
  h.Push(5.f);
  h.Push(7.f);
  // The graph indexes up to size(); a partially filled window must not read
  // uninitialised memory for the samples that do not exist yet.
  CHECK_NEAR(h.At(2), 0.f, 1e-6);
  CHECK_NEAR(h.At(999), 0.f, 1e-6);
  CHECK_NEAR(h.At(-1), 0.f, 1e-6);
}

TEST(frame_history_stops_growing_at_capacity) {
  fh::FrameTimeHistory h;
  for (int i = 0; i < kCap + 500; ++i) h.Push(static_cast<float>(i));

  CHECK_EQ(h.size(), kCap);
  CHECK(h.full());

  // The oldest samples are the ones discarded, and the retained window is the
  // most recent kCap pushes in order.
  CHECK_NEAR(h.At(0), 500.f, 1e-6);
  CHECK_NEAR(h.At(kCap - 1), static_cast<float>(kCap + 499), 1e-6);
  CHECK_NEAR(h.Latest(), static_cast<float>(kCap + 499), 1e-6);
}

TEST(frame_history_wraparound_preserves_order) {
  // One full buffer plus three more, so the ring has wrapped past its start.
  // The three oldest samples (0, 1, 2) are overwritten and the window becomes
  // 3, 4, ... 239, 1001, 1002, 1003, oldest first. This is the case where an
  // off-by-one in the ring arithmetic silently reorders the plot.
  fh::FrameTimeHistory h;
  for (int i = 0; i < kCap; ++i) h.Push(static_cast<float>(i));
  h.Push(1001.f);
  h.Push(1002.f);
  h.Push(1003.f);

  CHECK_EQ(h.size(), kCap);
  CHECK_NEAR(h.At(0), 3.f, 1e-6);                 // oldest surviving sample
  CHECK_NEAR(h.At(1), 4.f, 1e-6);
  CHECK_NEAR(h.At(kCap - 4), 239.f, 1e-6);        // last of the original run
  CHECK_NEAR(h.At(kCap - 3), 1001.f, 1e-6);       // first wrapped sample
  CHECK_NEAR(h.At(kCap - 2), 1002.f, 1e-6);
  CHECK_NEAR(h.At(kCap - 1), 1003.f, 1e-6);       // newest, at the end
  CHECK_NEAR(h.Latest(), 1003.f, 1e-6);
}

TEST(frame_history_max_and_avg) {
  fh::FrameTimeHistory h;
  h.Push(10.f);
  h.Push(30.f);
  h.Push(20.f);

  CHECK_NEAR(h.MaxMs(), 30.f, 1e-6);
  CHECK_NEAR(h.AvgMs(), 20.f, 1e-6);

  // Max must be the worst frame, not the newest: a single 90ms hitch followed by
  // fast frames is exactly the case the graph exists to show, and a Max() that
  // returned the latest sample would flatten it.
  h.Push(90.f);
  CHECK_NEAR(h.MaxMs(), 90.f, 1e-6);
  CHECK_NEAR(h.AvgMs(), 37.5f, 1e-6);
  CHECK_NEAR(h.Latest(), 90.f, 1e-6);
}

TEST(frame_history_counts_frames_over_budget) {
  fh::FrameTimeHistory h;
  h.Push(16.7f);  // exactly on budget: not over
  h.Push(16.7f);
  h.Push(16.8f);  // over
  h.Push(40.f);   // over
  h.Push(5.f);

  CHECK_EQ(h.OverBudgetCount(16.7f), 2);
  // Strictly greater, so a frame landing exactly on the budget is not flagged.
  // The budget is itself a limit, not an overshoot.
  CHECK_EQ(h.OverBudgetCount(16.8f), 1);
  CHECK_EQ(h.OverBudgetCount(1e6f), 0);
}

TEST(frame_history_clamps_non_finite_samples) {
  // A NaN would poison Max(), and therefore every y-coordinate derived from it,
  // blanking the graph for the rest of the run with no way to diagnose it.
  fh::FrameTimeHistory h;
  h.Push(std::numeric_limits<float>::quiet_NaN());
  h.Push(12.f);
  h.Push(-5.f);  // negative frame time is nonsense; clamp rather than plot
  h.Push(std::numeric_limits<float>::infinity());

  CHECK_NEAR(h.At(0), 0.f, 1e-6);
  CHECK_NEAR(h.At(1), 12.f, 1e-6);
  CHECK_NEAR(h.At(2), 0.f, 1e-6);
  CHECK_NEAR(h.At(3), 0.f, 1e-6);
  // Max is finite, so the graph still has a usable scale.
  CHECK(std::isfinite(h.MaxMs()));
  CHECK_NEAR(h.MaxMs(), 12.f, 1e-6);
  CHECK_NEAR(h.AvgMs(), 3.f, 1e-6);
}

TEST(frame_history_clear_resets_for_reuse) {
  fh::FrameTimeHistory h;
  for (int i = 0; i < 100; ++i) h.Push(50.f);
  h.Clear();

  CHECK_EQ(h.size(), 0);
  CHECK(h.empty());
  CHECK_NEAR(h.MaxMs(), 0.f, 1e-6);

  // Reused immediately, without a full lap: an implementation that cleared only
  // `count` would leave head_ pointing into the old data.
  h.Push(7.f);
  h.Push(8.f);
  CHECK_EQ(h.size(), 2);
  CHECK_NEAR(h.At(0), 7.f, 1e-6);
  CHECK_NEAR(h.Latest(), 8.f, 1e-6);
}
