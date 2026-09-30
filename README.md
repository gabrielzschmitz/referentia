# Referentia

<img align="right" width="192px" src="./resources/icon.svg" alt="Referentia Logo">

<a href="./LICENSE"><img src="https://img.shields.io/badge/license-MIT-green" alt="License"></a>
<a href="https://github.com/gabrielzschmitz/Motrix"><img src="https://img.shields.io/badge/ECS-Motrix_Based_Project-blue?style=flat-square" alt="Motrix Based ECS"></a>

**Referentia** is a cross-platform visual reference canvas. Drop in the images
you collect while working, and the app keeps them organised on an infinite
pannable board so you can find them again and drag them straight into whatever
else you are drawing in.

It is built with **raylib** and powered by
**[Motrix](https://github.com/gabrielzschmitz/Motrix)**, a minimal Entity
Component System designed for high-performance interactive applications.

---

## Quick Start

### 1. Clone the repository

```sh
git clone https://github.com/gabrielzschmitz/referentia.git Referentia
cd Referentia
```

### 2. Build and run

<details open>
<summary><b>Automatic</b> &mdash; <code>./build.sh</code></summary>

Generates the makefiles, builds, and launches the app:

```sh
./build.sh
```

| Flag | Effect |
| --- | --- |
| `--debug` | Debug build: assertions and `LOG_DEBUG` output on |
| `--no-run` | Build only, do not launch |
| `--clean` | Full clean first (also wipes raylib, so it is slow) |
| `--no-regen` | Skip makefile generation |
| `-j, --jobs N` | Parallel job count (default: all cores) |

</details>

<details>
<summary>Manual &mdash; premake5 + make</summary>

For Windows, macOS or a custom toolchain, see
[INSTALL.md](INSTALL.md) for the full platform matrix.

```sh
cd build && ./premake5 gmake && cd ..
make config=release_x64 -j$(nproc)
./bin/Release/referentia
```

Builds land in `bin/<Config>/`. Other configurations: `debug_x64`,
`release_x86`, `debug_x86`, `debug_arm64`, `release_arm64`.

</details>

### 3. Run the tests

<details open>
<summary><b>Automatic</b> &mdash; <code>./tests.sh</code></summary>

```sh
./tests.sh
```

| Flag | Effect |
| --- | --- |
| `--debug` | Debug build of the tests |
| `--bench` | Also run the ECS/SparseSet benchmarks |
| `--list` | List the registered tests and exit |
| `--clean` | Clean the test project first |

The tests link no raylib, so this never builds raylib: a clean run takes about
two seconds.

</details>

<details>
<summary>Manual &mdash; premake5 + make</summary>

```sh
cd build && ./premake5 gmake && cd ..
make config=release_x64 referentia-tests
./bin/Release/referentia-tests
```

The test binary needs no GPU, no display and no X11 server, so it runs fine in
CI. It exits non-zero if any test fails.

</details>

---

## Features

* Infinite pan/zoom reference board
* Image and text nodes with drag, resize and rotate
* Tag-based groups rendered as a labelled frame around their members
* Auto-arrange: grid, shelf-pack, justify, distribute and fit-to-view, applied
  to any selection with a keypress
* Built on the Motrix ECS and raylib; runs on Windows, macOS, Linux and the web

## Project Structure

```text
build/      # premake5 build scripts and the downloaded raylib checkout
include/    # shared third-party headers
resources/  # icons and fonts
scripts/    # packaging / release tooling, and the build.sh helpers
src/        # C++ sources
├── app/     # application lifecycle, board wiring, platform entrypoints
├── board/   # pure camera/transform maths (headless, unit tested)
├── components/ # component definitions
├── entities/# reusable entity factories
├── engine/  # ECS core, generic systems, and the UI toolkit
├── systems/ # per-frame update and render passes
└── tests/   # unit tests and benchmarks
```

`build.sh` and `tests.sh` sit at the repository root.

### Architecture

Referentia uses [Motrix](https://github.com/gabrielzschmitz/Motrix) as its ECS
backbone:

* **Entities** are references, groups, and the board itself
* **Components** store transform, metadata and interaction state
* **Systems** update the board, nodes, groups and layout independently

There is exactly one board and no scene registry: `app/board.h` holds the four
functions the app loop calls (`BoardInit`, `BoardUpdate`, `BoardRender`,
`BoardCreateUI`). A second board mode would reintroduce a dispatch table at
that point, when there would be something to dispatch to.

Namespaces are split by reuse: `motrix::engine` is the vendored ECS and generic
engine utilities, `referentia::*` is everything specific to this application.

The board is deliberately **flat**. A node carries a list of tags, and a group
is a frame that is rendered around every node tagged with the group's name.
There is no parent/child hierarchy, so moving a member can never invalidate a
frame, the frame simply re-fits to its members each frame unless it is pinned.

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file
for details.

### Third-Party

* **Work Sans**: SIL Open Font License 1.1. All nine statics (Thin
  through Black, upright and italic) are bundled; see
  [`resources/fonts/OFL.txt`](resources/fonts/OFL.txt).

