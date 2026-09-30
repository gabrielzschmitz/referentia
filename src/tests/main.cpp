// tests/main.cpp
//
// Entry point for the headless `referentia-tests` binary.
//
//   referentia-tests            run the unit tests
//   referentia-tests --bench    run the tests, then the benchmarks
//   referentia-tests --list     print the registered test names and exit
//
// Exits 0 only when every test passed, so CI can gate on the exit status.
#include <cstring>
#include <iostream>

#include "test_lib.h"

int main(int argc, char** argv) {
  bool run_bench = false;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--bench") == 0) {
      run_bench = true;
    } else if (std::strcmp(argv[i], "--list") == 0) {
      for (const auto& t : TestRegistry::instance().tests) {
        std::cout << t.name << "\n";
      }
      return 0;
    } else if (std::strcmp(argv[i], "--help") == 0 ||
               std::strcmp(argv[i], "-h") == 0) {
      std::cout << "Usage: referentia-tests [--bench] [--list]\n"
                   "  --bench   also run the ECS/SparseSet benchmarks\n"
                   "  --list    print the registered tests and exit\n";
      return 0;
    } else {
      std::cerr << "referentia-tests: unknown option '" << argv[i] << "'\n";
      return 2;
    }
  }

  const int failed = TestRegistry::instance().run_all();

  if (run_bench) {
    BenchRegistry::instance().run_all(/*runs=*/ 20, /*warmup=*/ 5);
  }

  return failed == 0 ? 0 : 1;
}
