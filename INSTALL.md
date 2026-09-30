# Installation Guide

This guide covers how to build and run Referentia on all supported platforms,
including web builds.

---

## Prerequisites

* **C++ compiler toolchain** suitable for your platform (GCC 9+, Clang 10+, or
  MSVC 2019+)
* **Emscripten SDK** (optional, for web builds only)
* [VSCode](https://code.visualstudio.com/) (optional, for editing and building)

`raylib` is not a prerequisite — the build downloads it automatically on the
first `premake5` run.

---

## Quick Start

### Desktop builds (Linux / Windows / macOS)

The scripts at the repository root wrap the whole sequence. On Windows use
Git Bash or WSL; `build-MinGW-W64.bat` and `build-VisualStudio2022.bat` are
there for cmd.

```bash
./build.sh            # generate, build, launch
./build.sh --debug    # debug build (assertions + LOG_DEBUG output)
./tests.sh            # generate, build, run the tests
```

<details>
<summary>Manual &mdash; Premake5 + make</summary>

1. Generate the build files with Premake5:
   ```bash
   cd build
   ./premake5 gmake                 # Linux
   ./premake5.osx gmake             # macOS
   premake5.exe vs2022              # Windows / Visual Studio
   premake5.exe gmake               # Windows / MinGW Makefiles
   cd ..
   ```

2. Build:
   ```bash
   make config=release_x64 -j$(nproc)
   ```

3. Run:
   ```bash
   ./bin/Release/referentia
   ```

</details>

<details open>
<summary>Manual &mdash; the tests</summary>

```bash
make config=release_x64 referentia-tests
./bin/Release/referentia-tests
```

Or `./tests.sh [--debug] [--bench] [--list]`. The test binary is headless: it
links no raylib and needs no GPU, display or X11 server. It exits non-zero if
any test fails, so it can be used directly in CI.

</details>

---

## Platform-Specific Instructions

### Windows

#### MinGW-W64 (GCC)

1. Ensure MinGW-W64 is in your PATH, or use the W64devkit terminal.
2. Generate and build:
   ```cmd
   cd build
   premake5.exe gmake
   cd ..
   mingw32-make config=release_x64
   ```
3. Run:
   ```cmd
   bin\Release\referentia.exe
   ```

> **Note**: Use a modern MinGW-W64 (e.g. from
> [W64devkit](https://github.com/skeeto/w64devkit/releases)) or the raylib
> installer. Avoid mixing MinGW distributions.

#### Microsoft Visual Studio (2019 / 2022)

1. ```cmd
   cd build
   premake5.exe vs2022
   ```
2. Open the generated `Referentia.sln` and build with `Ctrl+Shift+B`.
3. The executable lands in `bin\Release\`.

---

### Linux

```bash
cd build
./premake5 gmake
cd ..
make config=release_x64 -j$(nproc)
./bin/Release/referentia
```

#### Wayland support

```bash
cd build
./premake5 gmake --wayland=on
cd ..
make config=release_x64
```

> The Wayland path is supported by the vendored GLFW, but it is marked
> experimental upstream and file drag-and-drop is not expected to work there.
> Use the X11 build if you need import/export.

#### Compilation database (for IDE support)

```bash
bear -- make config=release_x64 VERBOSE=1
```

---

### macOS

```bash
cd build
./premake5.osx gmake
cd ..
make config=release_x64
./bin/Release/referentia
```

Alternatively, generate an Xcode project:

```bash
cd build && ./premake5.osx xcode4 && cd ..
```

---

## Web Build (Emscripten)

### Prerequisites

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

### Building

```bash
cd build
./premake5 gmake --with-emscripten --graphics=openges2
cd ..
emmake make config=release_web
```

Web builds always target OpenGL ES 2 (WebGL); if `--graphics` is omitted,
premake defaults to it automatically.

### Running

```bash
emrun --serve_after_close bin/Release/referentia.html
```

The web build produces `referentia.html`, `.js`, `.wasm` and `.data`, plus a
`referentia-web.zip` ready to host anywhere.

> The browser sandbox has no access to the host filesystem, so the web build
> can **read** dropped files but cannot drag references out to a native
> application. Export on the web goes through the system clipboard (copy, then
> paste into the target app).

### Single-file build

```bash
cd build
./premake5 gmake --with-emscripten --graphics=openges2 --itchio
cd ..
emmake make config=release_web
```

This embeds the wasm and every runtime asset (including the Work Sans TTFs)
into a single `index.html` that runs by double-clicking, with no web server.

---

## Build Configurations

### Configurations

* **Debug** — debug symbols, no optimisation
  ```bash
  make config=debug_x64
  ```
* **Release** — optimised
  ```bash
  make config=release_x64
  ```

### Platforms

* `x64` — 64-bit Intel/AMD (default)
* `x86` — 32-bit Intel/AMD
* `ARM64` — ARM 64-bit (Apple Silicon, etc.)
* `Web` — WebAssembly (Emscripten only)

### Graphics API (`--graphics`)

`opengl33` (default), `opengl43`, `opengl21`, `opengl11`, `openges2`, `openges3`.

### Backend (`--backend`)

`glfw` (default, cross-platform), `rgfw` (lightweight alternative), `win32`
(native Win32, Windows only).

### Maximum performance (`--perf`)

`none` (stock `-O2`, default), `fast` (`-O3 -flto`), `avx2` (`-O3 -flto
-mavx2 -mfma -mbmi2`), `native` (`-O3 -flto -march=native`).

```bash
cd build && ./premake5 gmake --perf=native && cd ..
make clean
make -j$(nproc) config=release_x64
```

> There is deliberately no `-ffast-math` preset.

---

## VSCode Integration

Open the project folder in VSCode with the C/C++ extension installed. Build
with `Ctrl+Shift+B`, run with `F5`.

---

## Troubleshooting

### "raylib not found"

`premake5` downloads the pinned raylib release to
`build/external/raylib-<version>` on first run. The version is `RAYLIB_VERSION`
near the top of `build/premake5.lua`; a release tag is pinned rather than
`master` so a build cannot break because upstream moved. If the download fails,
fetch it manually with the same version:

```bash
VERSION=6.0                      # keep in step with RAYLIB_VERSION
cd build/external
curl -LO "https://github.com/raysan5/raylib/archive/refs/tags/${VERSION}.zip"
unzip -q "${VERSION}.zip" && rm "${VERSION}.zip"   # extracts to raylib-${VERSION}/
```

Premake removes any other `raylib-*` directory in `build/external` before it
downloads, so a version bump cannot leave two raylib trees side by side. If you
do end up with a mix, delete `build/external/raylib-*` and build again.

### Missing X11 headers (Linux)

```bash
# Ubuntu/Debian
sudo apt-get install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
# Fedora
sudo dnf install libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel
# Arch
sudo pacman -S libx11 libxrandr libxinerama libxcursor libxi
```

### Missing Wayland headers (Linux, `--wayland=on`)

```bash
sudo apt-get install libwayland-dev wayland-protocols libxkbcommon-dev
```

### Web build shows a black screen

Serve it over HTTP rather than opening it as a `file://` URL, or use
`emrun --serve_after_close`.

### Web build renders no text

The itch.io single-file build embeds assets one at a time by path, and it
embeds all eighteen Work Sans faces even though only three are rasterised at
startup. If you add a face, add it in *both* mount blocks of the
`web_standalone` branch in `build/premake5.lua` (`/fonts/...` and
`/resources/fonts/...`) and add it to `kFontFaceFiles` in
`src/app/font_faces.h`, or it will be missing from the bundle. The
`referentia-tests` suite fails if the two lists disagree, or if a face named by
the table is not in `resources/fonts`.

---

## Clean Build

```bash
make clean
rm -rf build/build_files bin
cd build && ./premake5 gmake && cd ..
make config=release_x64
```

---

## Project Structure

```text
Referentia/
├── build/           # premake5 scripts + downloaded raylib (build_files/ is generated inside)
├── bin/             # compiled binaries (Debug/, Release/)
├── include/         # shared third-party headers
├── resources/       # icons, fonts
├── scripts/         # packaging / release tooling, and the build.sh helpers
├── src/             # source code
├── build.sh         # generate + build + launch
└── tests.sh         # generate + build + run the tests
```

## Additional Information

* **Raylib**: 6.0, pinned to the release tag (downloaded automatically by
  premake; see `RAYLIB_VERSION` in `build/premake5.lua`)
* **C Standard**: C17 / GNU17 (GNU17 for web builds)
* **C++ Standard**: C++17 / GNU++17 (GNU++17 for web builds)
* **UI font**: Work Sans, all nine statics (Thin..Black, upright and
  italic), loaded on demand from `src/app/font_faces.h`. SIL Open Font
  License 1.1 (`resources/fonts/OFL.txt`)
* **License**: MIT (see `LICENSE`)
