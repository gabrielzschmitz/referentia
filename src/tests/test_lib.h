// tests/test_lib.h
//
// Minimal header-only test framework for the headless `referentia-tests`
// binary. It deliberately links no raylib: a translation unit reachable from
// the test runner must not call any RLAPI function, or the link fails. That
// constraint is what keeps every unit under test (ECS, layout, transforms,
// text buffers, group membership) runnable in CI with no GPU or display.
//
// Usage:
//   #include "test_lib.h"
//   TEST(my_feature_behaves) { CHECK_EQ(Add(2, 3), 5); }
#pragma once

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define REF_TEST_ISATTY _isatty
#define REF_TEST_FILENO _fileno
#else
#include <unistd.h>
#define REF_TEST_ISATTY isatty
#define REF_TEST_FILENO fileno
#endif

// ------------------------------------------------------------
// TEST FAILURE CONTEXT
// ------------------------------------------------------------

/**
 * Formats a failure context string for CHECK_MSG.
 *
 * Lives here rather than in a suite header because every suite needs it and
 * duplicating it per-header collides at link time in the single-TU aggregator.
 *
 * A parameter pack cannot carry printf's format attribute (GCC rejects the
 * attribute on this signature), so -Wformat-security fires on the forwarding
 * snprintf: the format string is a parameter, not a literal. Every call site in
 * the test suite passes a literal, so the warning is suppressed for this
 * function only rather than project-wide.
 */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#endif
template <typename... Args>
inline std::string Msg(const char* fmt, Args... args) {
  char buf[256];
  std::snprintf(buf, sizeof(buf), fmt, args...);
  return buf;
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// ------------------------------------------------------------
// ANSI COLORS (disabled when stdout is not a tty, or NO_COLOR is set)
// ------------------------------------------------------------
#define C_RESET detail::C("\033[0m")
#define C_RED detail::C("\033[31m")
#define C_GREEN detail::C("\033[32m")
#define C_YELLOW detail::C("\033[33m")
#define C_BLUE detail::C("\033[34m")
#define C_MAGENTA detail::C("\033[35m")
#define C_CYAN detail::C("\033[36m")
#define C_BOLD detail::C("\033[1m")

namespace detail {

inline bool& ColorEnabled() {
  static bool enabled = [] {
    if (const char* nc = std::getenv("NO_COLOR")) {
      if (nc[0] != '\0') return false;
    }
    return REF_TEST_ISATTY(REF_TEST_FILENO(stdout)) != 0;
  }();
  return enabled;
}

inline std::string C(const char* code) { return ColorEnabled() ? code : ""; }

// Failure bookkeeping. Declared ahead of the registry because the registry's
// run_all() queries it.
inline int& FailureCount() {
  static int count = 0;
  return count;
}

inline std::string& CurrentTest() {
  static std::string name;
  return name;
}

inline void ReportFailure(const char* file, int line, const std::string& msg) {
  ++FailureCount();
  std::cout << "\n    " << C_RED << "x" << C_RESET << " " << file << ":"
            << line << ": " << msg << "\n";
}

// Renders any value for an assertion message. Falls back to a placeholder for
// types with no operator<<.
template <typename T>
inline auto ToString(const T& v, int)
    -> decltype(std::declval<std::ostream&>() << v, std::string()) {
  std::ostringstream ss;
  ss << v;
  return ss.str();
}

template <typename T>
inline std::string ToString(const T&, long) {
  return "<value>";
}

inline std::string ToString(std::nullptr_t, int) { return "nullptr"; }

inline bool NearlyEqual(double a, double b, double eps) {
  if (std::isnan(a) || std::isnan(b)) return false;
  if (a == b) return true;
  return std::fabs(a - b) <= eps;
}

}  // namespace detail

// =======================================================
// TEST REGISTRY
// =======================================================
class TestRegistry {
 public:
  struct Entry {
    std::string name;
    std::function<void()> func;
  };

  std::vector<Entry> tests;

  static TestRegistry& instance() {
    static TestRegistry r;
    return r;
  }

  void add(const char* name, std::function<void()> fn) {
    tests.push_back({name, std::move(fn)});
  }

  // Returns the number of failed test cases.
  int run_all() const {
    int failed = 0;
    std::cout << C_MAGENTA << C_BOLD << "\nRunning " << tests.size()
              << " test(s)\n" << C_RESET;

    for (const auto& t : tests) {
      detail::CurrentTest() = t.name;
      const int before = detail::FailureCount();

      std::cout << C_BLUE << C_BOLD << "  [TEST] " << C_RESET << t.name
                << " ... " << std::flush;

      t.func();

      if (detail::FailureCount() > before) {
        std::cout << C_RED << C_BOLD << "FAIL" << C_RESET << " ("
                  << (detail::FailureCount() - before) << " check"
                  << (detail::FailureCount() - before == 1 ? "" : "s") << ")\n";
        ++failed;
      } else {
        std::cout << C_GREEN << "ok" << C_RESET << "\n";
      }
    }

    std::cout << "\n" << C_BOLD << (failed == 0 ? C_GREEN : C_RED) << "==== "
              << (tests.size() - failed) << " passed, " << failed
              << " failed ====" << C_RESET << "\n";
    return failed;
  }

 private:
  TestRegistry() = default;
};

struct TestRegistrar {
  TestRegistrar(const char* name, std::function<void()> fn) {
    TestRegistry::instance().add(name, std::move(fn));
  }
};

#define TEST(name)                                                             \
  static void name();                                                          \
  static TestRegistrar _test_reg_##name(#name, name);                          \
  static void name()

// =======================================================
// ASSERTIONS
// =======================================================
// Non-fatal: records a failure and keeps running the test case, so one run
// reports every problem instead of only the first.
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      detail::ReportFailure(__FILE__, __LINE__, "CHECK(" #cond ") failed");     \
    }                                                                          \
  } while (0)

#define CHECK_MSG(cond, msg)                                                   \
  do {                                                                         \
    if (!(cond)) {                                                             \
      detail::ReportFailure(__FILE__, __LINE__,                                \
                            std::string("CHECK(" #cond ") failed: ") + (msg)); \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    const auto& _lhs = (a);                                                    \
    const auto& _rhs = (b);                                                    \
    if (!(_lhs == _rhs)) {                                                     \
      detail::ReportFailure(                                                   \
          __FILE__, __LINE__,                                                  \
          std::string(#a " == " #b " failed: got ") +                         \
              detail::ToString(_lhs, 0) + ", expected " +                      \
              detail::ToString(_rhs, 0));                                      \
    }                                                                          \
  } while (0)

#define CHECK_NE(a, b)                                                         \
  do {                                                                         \
    const auto& _lhs = (a);                                                    \
    const auto& _rhs = (b);                                                    \
    if (_lhs == _rhs) {                                                        \
      detail::ReportFailure(__FILE__, __LINE__,                                \
                            std::string(#a " != " #b " failed: both are ") +  \
                                detail::ToString(_lhs, 0));                    \
    }                                                                          \
  } while (0)

// Float comparison with an explicit tolerance. Use this instead of CHECK_EQ for
// anything computed, where exact bit equality is not meaningful.
#define CHECK_NEAR(a, b, eps)                                                  \
  do {                                                                         \
    const double _a = static_cast<double>(a);                                  \
    const double _b = static_cast<double>(b);                                  \
    if (!detail::NearlyEqual(_a, _b, (eps))) {                                 \
      std::ostringstream _ss;                                                  \
      _ss << #a " ~= " #b " failed: got " << _a << ", expected " << _b          \
          << " (eps " << (eps) << ")";                                         \
      detail::ReportFailure(__FILE__, __LINE__, _ss.str());                    \
    }                                                                          \
  } while (0)

// Fatal: abandons the rest of the test case. Use when continuing would
// dereference something the failed check just proved is missing.
#define REQUIRE(cond)                                                          \
  do {                                                                         \
    if (!(cond)) {                                                             \
      detail::ReportFailure(__FILE__, __LINE__,                                \
                            "REQUIRE(" #cond ") failed, aborting test case");  \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define REQUIRE_EQ(a, b)                                                       \
  do {                                                                         \
    const auto& _lhs = (a);                                                    \
    const auto& _rhs = (b);                                                    \
    if (!(_lhs == _rhs)) {                                                     \
      detail::ReportFailure(                                                   \
          __FILE__, __LINE__,                                                  \
          std::string("REQUIRE(" #a " == " #b ") failed: got ") +             \
              detail::ToString(_lhs, 0) + ", expected " +                      \
              detail::ToString(_rhs, 0));                                      \
      return;                                                                  \
    }                                                                          \
  } while (0)

// =======================================================
// BENCHMARK REGISTRY
// =======================================================
class BenchRegistry {
 public:
  struct Entry {
    std::string name;
    std::function<void()> func;
  };

  std::vector<Entry> benches;

  static BenchRegistry& instance() {
    static BenchRegistry r;
    return r;
  }

  void add(const char* name, std::function<void()> fn) {
    benches.push_back({name, std::move(fn)});
  }

  // Runs each benchmark, discarding the first `warmup` runs to let caches and
  // branch predictors settle, then reports mean and sample stddev in ms.
  void run_all(int runs = 5, int warmup = 1) const {
    using clock = std::chrono::high_resolution_clock;

    if (benches.empty()) return;
    std::cout << "\n" << C_MAGENTA << C_BOLD << "Running " << benches.size()
              << " benchmark(s)" << C_RESET;

    for (const auto& b : benches) {
      std::cout << C_CYAN << C_BOLD << "\n[BENCH] " << C_RESET << b.name << "\n";

      for (int i = 0; i < warmup; i++) b.func();

      std::vector<double> times;
      times.reserve(static_cast<size_t>(runs));
      for (int i = 0; i < runs; i++) {
        const auto start = clock::now();
        b.func();
        const auto end = clock::now();
        times.push_back(
            std::chrono::duration<double, std::milli>(end - start).count());
      }

      double sum = 0;
      for (double t : times) sum += t;
      const double avg = sum / runs;

      double variance = 0;
      for (double t : times) variance += (t - avg) * (t - avg);
      const double stddev = std::sqrt(variance / runs);

      std::cout << "  avg:  " << C_GREEN << avg << " ms" << C_RESET << "\n";
      std::cout << "  std:  " << C_YELLOW << stddev << " ms" << C_RESET
                << "\n";
      std::cout << "  runs: " << C_BLUE;
      for (double t : times) std::cout << t << " ";
      std::cout << C_RESET;
    }
    std::cout << "\n";
  }

 private:
  BenchRegistry() = default;
};

struct BenchRegistrar {
  BenchRegistrar(const char* name, std::function<void()> fn) {
    BenchRegistry::instance().add(name, std::move(fn));
  }
};

#define BENCH(name)                                                            \
  static void name();                                                          \
  static BenchRegistrar _bench_reg_##name(#name, name);                        \
  static void name()
