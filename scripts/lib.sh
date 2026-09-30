#!/usr/bin/env bash

# ============================================================
# Shared helpers for build.sh and tests.sh
#
# Sourced, never executed. Owns: colours, logging, the banner,
# CPU-count detection, premake discovery and makefile generation.
#
# Sourcing scripts own the shell options (set -euo pipefail).
# ============================================================

# ============================================================
# Paths
#
# Resolved from this file's own location, so every script works no
# matter which directory it is invoked from.
# ============================================================

LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${LIB_DIR}/.." && pwd)"

PREMAKE_BIN="${ROOT}/build/premake5"
BUILD_DIR="${ROOT}/build"
BIN_DIR="${ROOT}/bin"

# ============================================================
# Colors
#
# Emitted only when stdout is a terminal. Redirecting a build into a
# file or a CI log should not leave escape sequences in it, and
# NO_COLOR is honoured per no-color.org.
# ============================================================

if [[ -t 1 ]] && [[ -z "${NO_COLOR:-}" ]]; then
  RESET="\033[0m"
  BOLD="\033[1m"
  DIM="\033[2m"
  BG_RED="\033[41m"
  WHITE="\033[37m"
  BLACK="\033[30m"
  GREEN="\033[32m"
  CYAN="\033[36m"
  YELLOW="\033[33m"
  RED="\033[31m"
else
  RESET=""; BOLD=""; DIM=""; BG_RED=""
  WHITE=""; BLACK=""; GREEN=""; CYAN=""; YELLOW=""; RED="";
fi

# ============================================================
# Logging helpers
# ============================================================

log_ok()   { echo -e "  ${GREEN}[OK]${RESET} $1"; }
log_warn() { echo -e "  ${YELLOW}[WARN]${RESET} $1"; }
log_err()  { echo -e "  ${RED}[ERROR]${RESET} $1"; }
log_info() { echo -e "  ${CYAN}[INFO]${RESET} $1"; }

# Indent every line of piped output by two spaces so compiler and test
# output lines up with the rest of the build log.
indent() { sed 's/^/  /'; }

section() { echo -e "${BOLD}${CYAN}=== $1 =========================${RESET}"; }

# ============================================================
# Banner
# ============================================================

print_banner() {
  local mode="$1"
  # The logo: a board on a stand, echoing the app icon. White lineart on a
  # magenta plate, so the whole block reads as one badge. Art is 23 columns
  # wide, indented 8 to sit centred under the 39-column title rule below.
  local plate="${BOLD}${BLACK}${BG_RED}"
  echo -e "          ${plate}    ┌─────────┐    ${RESET}"
  echo -e "          ${plate}  ╔═╧═════════╧═╗  ${RESET}"
  echo -e "          ${plate}  ║ ┌─────────┐ ║  ${RESET}"
  echo -e "          ${plate}  ║ │ ╱╲   ╱╲ │ ║  ${RESET}"
  echo -e "          ${plate}  ║ │   ───   │ ║  ${RESET}"
  echo -e "          ${plate}  ║ └─────────┘ ║  ${RESET}"
  echo -e "          ${plate}  ╚══╤═══╤═══╤══╝  ${RESET}"
  echo -e "          ${plate}     │   │   │     ${RESET}"
  echo -e "          ${plate}     ├───┼───┤     ${RESET}"
  echo -e "          ${plate}     │   │   │     ${RESET}"
  echo -e "          ${plate}     ╧   ╧   ╧     ${RESET}"
  echo -e "${BOLD}${RED}"
  echo "======================================="
  echo "             Referentia"
  echo "        raylib / premake5 / C++"
  echo "======================================="
  echo -e "${RESET}"
  case "$mode" in
    tests)
      echo -e "${CYAN}${BOLD}Mode:${RESET} build and run the unit tests"
      ;;
    build-only)
      echo -e "${CYAN}${BOLD}Mode:${RESET} build only (not launched)"
      ;;
    *)
      echo -e "${CYAN}${BOLD}Mode:${RESET} build and launch"
      ;;
  esac
  echo
}

# ============================================================
# Environment
# ============================================================

# Parallel job count: an explicit --jobs wins, then $JOBS, then the
# detected core count. Prefers cross-compilation when a toolchain for
# another target is present, which is a much better use of an idle
# machine than a single-target build.
detect_jobs() {
  if [[ -n "${JOBS:-}" ]]; then
    echo "$JOBS"
    return
  fi
  local n=""
  if command -v nproc >/dev/null 2>&1; then
    n="$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then
    n="$(sysctl -n hw.ncpu)"
  fi
  echo "${n:-4}"
}

# True when there is a display to open a window on. True on macOS and
# Windows, which have no DISPLAY/WAYLAND_DISPLAY convention.
have_display() {
  if [[ "${OSTYPE:-}" == darwin* ]] || [[ "${OS:-}" == Windows_NT ]]; then
    return 0
  fi
  [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]
}

# ============================================================
# Premake
# ============================================================

ensure_premake() {
  if [[ -x "${PREMAKE_BIN}" ]]; then
    log_ok "Premake5 found: ${PREMAKE_BIN#"${ROOT}/"}"
    return
  fi
  if command -v premake5 >/dev/null 2>&1; then
    PREMAKE_BIN="$(command -v premake5)"
    log_ok "Premake5 found on PATH: ${PREMAKE_BIN}"
    return
  fi
  log_err "Premake5 not found."
  log_err "  Expected the vendored binary at: ${PREMAKE_BIN}"
  log_err "  Or install it from https://premake.github.io/download"
  exit 1
}

# The generated makefiles embed absolute paths to the resources, so they
# only work from the directory they were generated in. An old copy left
# behind by a different checkout therefore fails with a confusing
# "cp: cannot stat '/somebody/else/.local/src/referentia/resources/icon.png'".
#
# They are also generated and gitignored, so regenerating is always safe.
# It costs ~80ms, which is cheaper than debugging a phantom failure, so
# this runs before every build unless --no-regen is passed.
generate_makefiles() {
  ensure_premake
  log_info "Generating makefiles…"

  # Premake's stdout is captured rather than discarded, and replayed only for
  # the lines that describe real work. It is what prints the raylib download
  # and its progress bar: a first build fetches ~50MB, and swallowing that
  # leaves the user staring at a build that appears to hang for a minute with
  # no explanation. The project list premake prints on every run is not worth
  # forwarding, which is why this filters instead of just removing >/dev/null.
  local premake_log
  premake_log="$(cd "${BUILD_DIR}" && "${PREMAKE_BIN}" gmake 2>&1)" || {
    printf '%s\n' "${premake_log}" | indent
    log_err "Premake5 failed to generate makefiles."
    exit 1
  }
  if [[ "${premake_log}" == *"not found, downloading"* ||
    "${premake_log}" == *"Unzipping"* ||
    "${premake_log}" == *"Removing stale"* ]]; then
    printf '%s\n' "${premake_log}" |
      grep -E "not found, downloading|Download progress|Unzipping|Removing stale" |
      indent
  fi

  # Sanity check: the generated make must point back at this checkout.
  # A stale one from another directory will reference a foreign path and
  # fail much later with an unrelated error, so catch it here.
  local makefile="${BUILD_DIR}/build_files/referentia.make"
  if [[ -f "${makefile}" ]] && ! grep -qF "${ROOT}" "${makefile}"; then
    log_warn "Generated makefile does not reference this checkout - regenerating again."
    rm -rf "${BUILD_DIR}/build_files"
    (cd "${BUILD_DIR}" && "${PREMAKE_BIN}" gmake) >/dev/null 2>&1
  fi
  if [[ -f "${makefile}" ]] && ! grep -qF "${ROOT}" "${makefile}"; then
    log_err "Makefiles still reference a foreign path after regenerating."
    log_err "  Remove ${BUILD_DIR#"${ROOT}/"}/build_files and rerun."
    exit 1
  fi

  log_ok "Makefiles generated (resources and includes resolve to this checkout)"
}

# Full clean: every project, including raylib. Slow to rebuild.
make_clean() {
  local config="$1"
  log_info "Cleaning ${config} (all projects)…"
  (cd "${ROOT}" && make config="${config}" clean) >/dev/null 2>&1 || true
  # `make clean` walks the current dependency graph, so it structurally cannot
  # remove an object whose source path no longer exists. That is exactly what
  # changing a pinned dependency leaves behind: moving raylib from
  # raylib-master to raylib-6.0 orphans objects that still name the old path as
  # a prerequisite, and the next build dies with "No rule to make target". The
  # makefile looks correct, which is what makes it so confusing. Deleting the
  # generated object tree outright is the only way to guarantee the next build
  # starts from nothing.
  rm -rf "${BUILD_DIR}/build_files/obj"
  log_ok "Cleaned"
}

# Targeted clean for a single project. The root `clean` also wipes raylib,
# which costs ~30s to rebuild and which the tests do not even link against,
# so tests.sh cleans only its own project.
make_clean_target() {
  local project="$1" config="$2"
  log_info "Cleaning ${project} (${config})…"
  (
    cd "${BUILD_DIR}/build_files" &&
      make --no-print-directory -f "${project}.make" config="${config}" clean
  ) >/dev/null 2>&1 || true
  log_ok "Cleaned"
}
