#!/usr/bin/env bash

set -euo pipefail

# ============================================================
# referentia build script
#
# Regenerates the premake5 makefiles, builds the app and launches it.
#
# Usage:
#   ./build.sh              Release build, then launch.
#   ./build.sh --debug      Debug build (assertions + debug logging), then launch.
#   ./build.sh --no-run     Build only.
#   ./build.sh --clean      Full clean first (slow: rebuilds raylib).
#   ./build.sh --no-regen   Skip premake regeneration.
#   ./build.sh --jobs 8     Override the parallel job count.
# ============================================================

# shellcheck source=scripts/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scripts/lib.sh"

# ============================================================
# Configuration
# ============================================================

CONFIG="release_x64"     # premake config name
PROJECT="referentia"     # premake project name
RUN=1                    # launch after building
REGEN=1                  # regenerate makefiles
CLEAN=0
JOBS=""

# ============================================================
# Argument parsing
# ============================================================

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

  -d, --debug          Debug build: assertions and debug logging on.
  -r, --release        Release build (default).
      --no-run         Build only, do not launch.
  -c, --clean          Clean before building. Also wipes raylib, so the
                        next build takes a while.
      --no-regen       Skip premake5 makefile generation.
  -j, --jobs <n>       Parallel job count (default: all cores).
  -h, --help           Show this help and exit.

The makefiles are generated and gitignored, and they embed absolute paths,
so they are regenerated on every run. That is what keeps a stale copy from
another checkout out of the build.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--debug)   CONFIG="debug_x64" ;;
    -r|--release) CONFIG="release_x64" ;;
    --no-run)     RUN=0 ;;
    -c|--clean)   CLEAN=1 ;;
    --no-regen)   REGEN=0 ;;
    -j|--jobs)
      [[ $# -ge 2 ]] || { log_err "--jobs needs a value"; exit 1; }
      JOBS="$2"; shift
      ;;
    -h|--help)    print_banner "build-only"; usage; exit 0 ;;
    *)            log_err "Unknown argument: $1"; usage; exit 1 ;;
  esac
  shift
done

# A single build profile, so the run uses the same binary as the build.
case "${CONFIG}" in
  debug_x64)   CONFIG_DIR="Debug" ;;
  release_x64) CONFIG_DIR="Release" ;;
  *)           log_err "Unsupported config: ${CONFIG}"; exit 1 ;;
esac

BINARY="${BIN_DIR}/${CONFIG_DIR}/${PROJECT}"
JOBS="$(detect_jobs)"

# ============================================================
# Main
# ============================================================

if [[ "${RUN}" -eq 1 ]]; then
  print_banner "build"
else
  print_banner "build-only"
fi

section "Configuration"
log_info "Config:     ${CONFIG}  →  bin/${CONFIG_DIR}/${PROJECT}"
log_info "Parallel:   ${JOBS} jobs"
log_info "CWD:        ${ROOT}"

if [[ "${CLEAN}" -eq 1 ]]; then
  echo
  make_clean "${CONFIG}"
fi

if [[ "${REGEN}" -eq 1 ]]; then
  echo
  section "Makefiles"
  generate_makefiles
else
  echo
  log_warn "Skipping makefile generation (--no-regen)."
  log_warn "A stale makefile from another checkout will fail here."
fi

echo
section "Build"
log_info "Compiling ${PROJECT}…"
echo
if ! (cd "${ROOT}" && make config="${CONFIG}" "${PROJECT}" -j"${JOBS}") 2>&1 | indent; then
  echo
  log_err "Build failed."
  exit 1
fi
echo

if [[ ! -x "${BINARY}" ]]; then
  log_err "Build reported success but the binary is missing:"
  log_err "  ${BINARY#"${ROOT}/"}"
  exit 1
fi
log_ok "Built ${BINARY#"${ROOT}/"}"

if [[ "${RUN}" -eq 0 ]]; then
  echo
  log_ok "Done (not launched, --no-run)."
  exit 0
fi

# ============================================================
# Launch
# ============================================================

echo
section "Launch"

# A headless box cannot open a window, and the app exits 1 with a clear
# message when it cannot. Skip the launch rather than printing a failure
# that looks like a build problem.
if ! have_display; then
  log_warn "No display detected (DISPLAY/WAYLAND_DISPLAY unset) - not launching."
  log_warn "Run it on a machine with a desktop session, or pass --no-run in CI."
  echo
  log_ok "Build complete (output in bin/${CONFIG_DIR})"
  exit 0
fi

print_controls() {
  local controls line key desc width=0

  # PrintUsage() in src/app/app.cpp is the only place that knows the key
  # bindings. This used to keep its own hand-written copy, which had already
  # drifted from it, so ask the binary instead: adding a binding is then a
  # one-file change. The binary exits before opening a window, which is what
  # makes calling it from a script safe.
  # `|| true` is load-bearing: the script runs under `set -euo pipefail`, and
  # pipefail makes the pipeline inherit a non-zero status from a binary that
  # will not run -- a stale libraylib, for instance. That would abort the
  # script here, hiding the app's own "cannot open a window" message behind a
  # bare exit 127. An unrunnable binary just means we skip the block.
  controls="$("${BINARY}" --help 2>/dev/null | sed -n '/^Controls:/,$p' | tail -n +2 || true)"
  [ -n "${controls}" ] || return 0

  # Width of the widest key, so the description column is derived from the data
  # rather than hand-counted. A longer key added later re-aligns itself.
  while IFS= read -r line; do
    line="${line#  }"
    [ -n "${line}" ] || continue
    key="${line%%  *}"
    [ "${#key}" -gt "${width}" ] && width="${#key}"
  done <<< "${controls}"

  printf '  %bControls%b\n' "${BOLD}" "${RESET}"
  while IFS= read -r line; do
    line="${line#  }"
    [ -n "${line}" ] || continue
    # Split on the first run of 2+ spaces, not on single whitespace: a key can
    # itself contain a space ("ESC, Q", and "Space + drag" one day), and
    # word-splitting turns that into a key of "ESC," followed by junk.
    key="${line%%  *}"
    desc="${line#"${key}"}"
    # Trim the padding between the columns; only now is desc pure description.
    desc="${desc#"${desc%%[![:space:]]*}"}"
    # printf %b, never a heredoc: cat copies backslash escapes verbatim, so a
    # heredoc emits the five literal characters \033[2m instead of a real dim
    # escape. %-Ns pads the key alone, so colouring never shifts the column.
    printf '    %b%-*s%b  %s\n' "${DIM}" "${width}" "${key}" "${RESET}" "${desc}"
  done <<< "${controls}"
}

print_controls

log_info "Starting ${PROJECT}…"
echo

# Let the app own the terminal: raylib takes the cursor, and a piped log
# would otherwise be interleaved with our own output.
"${BINARY}"

echo
log_ok "Exited cleanly"
