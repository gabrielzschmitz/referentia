#!/usr/bin/env bash

set -euo pipefail

# ============================================================
# referentia test script
#
# Builds and runs the headless unit test binary.
#
# The tests link no raylib, so this never builds raylib: a clean run is
# about two seconds.
#
# Usage:
#   ./tests.sh                Release build, run the tests.
#   ./tests.sh --debug        Debug build, run the tests.
#   ./tests.sh --bench        Also run the ECS/SparseSet benchmarks.
#   ./tests.sh --list         List the registered tests and exit.
#   ./tests.sh --clean        Clean the test project first.
# ============================================================

# shellcheck source=scripts/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scripts/lib.sh"

# ============================================================
# Configuration
# ============================================================

CONFIG="release_x64"     # premake config name
PROJECT="referentia-tests"
REGEN=1                  # regenerate makefiles
CLEAN=0
JOBS=""
BENCH=""
LIST=""
EXTRA=()                 # passed straight through to the test binary

# ============================================================
# Argument parsing
# ============================================================

usage() {
  cat <<EOF
Usage: $(basename "$0") [OPTIONS]

  -d, --debug          Debug build: assertions and debug logging on.
  -r, --release        Release build (default).
      --bench          Also run the ECS/SparseSet benchmarks.
      --list           List the registered tests and exit.
  -c, --clean          Clean the test project first. Does not touch
                        raylib, which the tests do not link.
      --no-regen       Skip premake5 makefile generation.
  -j, --jobs <n>       Parallel job count (default: all cores).
  -h, --help           Show this help and exit.

Any unrecognised argument is forwarded to referentia-tests, so new
runner flags work here without touching this script.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--debug)   CONFIG="debug_x64" ;;
    -r|--release) CONFIG="release_x64" ;;
    --bench)      BENCH="--bench" ;;
    --list)       LIST="--list" ;;
    -c|--clean)   CLEAN=1 ;;
    --no-regen)   REGEN=0 ;;
    -j|--jobs)
      [[ $# -ge 2 ]] || { log_err "--jobs needs a value"; exit 1; }
      JOBS="$2"; shift
      ;;
    -h|--help)    print_banner "tests"; usage; exit 0 ;;
    *)            EXTRA+=("$1") ;;
  esac
  shift
done

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

print_banner "tests"

section "Configuration"
log_info "Config:     ${CONFIG}  →  bin/${CONFIG_DIR}/${PROJECT}"
log_info "Parallel:   ${JOBS} jobs"
if [[ -n "${BENCH}" ]]; then
  log_info "Benchmarks: on"
fi
if [[ "${#EXTRA[@]}" -gt 0 ]]; then
  log_info "Forwarded:  ${EXTRA[*]}"
fi

if [[ "${CLEAN}" -eq 1 ]]; then
  echo
  make_clean_target "${PROJECT}" "${CONFIG}"
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

# ============================================================
# Build
#
# Only the test project, never `all`: the app drags in raylib and its
# shader compilation, which the tests have no use for.
# ============================================================

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

# ============================================================
# Run
# ============================================================

# --list is informational: it prints names and exits, so running the suite
# after it would be noise.
if [[ -n "${LIST}" ]]; then
  echo
  section "Tests"
  "${BINARY}" --list | indent
  exit 0
fi

echo
section "Tests"

set +e
"${BINARY}" ${BENCH} ${EXTRA[@]+"${EXTRA[@]}"}
TEST_STATUS=$?
set -e

echo
if [[ "${TEST_STATUS}" -eq 0 ]]; then
  log_ok "All tests passed (${CONFIG})"
  exit 0
fi

log_err "Tests failed with exit status ${TEST_STATUS} (${CONFIG})"
exit "${TEST_STATUS}"
