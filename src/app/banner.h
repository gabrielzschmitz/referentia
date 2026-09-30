// app/banner.h
#pragma once

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace referentia::app {

/**
 * ============================================================================
 * Banner
 * ============================================================================
 *
 * The board-on-a-stand logo and title block, printed when the app exits.
 *
 * This is the same art scripts/lib.sh prints at the top of a build. Mirroring it
 * in the binary means a run is bracketed by the same mark whether it was
 * launched by build.sh or by hand, and the exit is not just a bare "Exited
 * cleanly" line in a scrollback.
 *
 * The two copies are duplicated on purpose: the shell needs a PNG-free, pure-ASCII
 * banner before anything is built, and the C++ one cannot shell out to a script
 * it may not be sitting next to. Keep the art and the title rule in sync; the
 * block is 39 columns wide in both.
 *
 * Colours are suppressed when stdout is not a terminal or NO_COLOR is set, so a
 * redirected log does not fill up with escape sequences. The rest of the logger
 * does not yet do this, but a new caller can at least not add to the problem.
 * ============================================================================
 */
inline void PrintBanner() {
#if defined(_WIN32)
  const bool color = _isatty(_fileno(stdout)) != 0;
#else
  const bool color = isatty(fileno(stdout)) != 0;
#endif
  const char* no_color = std::getenv("NO_COLOR");
  const bool use_color = color && !(no_color && no_color[0] != '\0');

  // Magenta plate with black lineart, then the title in red -- matching the
  // shell banner's badge.
  const char* plate = use_color ? "\033[1m\033[30m\033[41m" : "";
  const char* red = use_color ? "\033[1m\033[31m" : "";
  const char* reset = use_color ? "\033[0m" : "";

  std::printf("\n");
  std::printf("          %s    ┌─────────┐    %s\n", plate, reset);
  std::printf("          %s  ╔═╧═════════╧═╗  %s\n", plate, reset);
  std::printf("          %s  ║ ┌─────────┐ ║  %s\n", plate, reset);
  std::printf("          %s  ║ │ ╱╲   ╱╲ │ ║  %s\n", plate, reset);
  std::printf("          %s  ║ │   ───   │ ║  %s\n", plate, reset);
  std::printf("          %s  ║ └─────────┘ ║  %s\n", plate, reset);
  std::printf("          %s  ╚══╤═══╤═══╤══╝  %s\n", plate, reset);
  std::printf("          %s     │   │   │     %s\n", plate, reset);
  std::printf("          %s     ├───┼───┤     %s\n", plate, reset);
  std::printf("          %s     │   │   │     %s\n", plate, reset);
  std::printf("          %s     ╧   ╧   ╧     %s\n", plate, reset);
  std::printf("%s", red);
  std::printf("=======================================\n");
  std::printf("             Referentia\n");
  std::printf("        raylib / premake5 / C++\n");
  std::printf("=======================================\n");
  std::printf("%s\n", reset);
  std::fflush(stdout);
}

}  // namespace referentia::app
