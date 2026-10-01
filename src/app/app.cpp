// app/app.cpp
#include "app/app.h"

#include <cstdio>
#include <cstring>
#include <thread>

#include "app/app_state.h"
#include "app/banner.h"
#include "app/board.h"
#include "app/file_drop_web.h"
#include "app/fonts.h"
#include "app/image_import.h"
#include "app/screenshot.h"
#include "board/transform.h"
#include "engine/globals.h"
#include "engine/logger.h"
#include "engine/systems/thread_pool.h"
#include "entities/camera.h"
#include "raylib.h"
#include "resource_dir.h"
#include "systems/board_cursor.h"
#include "systems/board_debug.h"
#include "systems/camera.h"
#include "systems/image_nodes.h"
#include "systems/perf_panel.h"
#include "systems/ui.h"

/**
 * Whether the OS cursor is currently hidden, i.e. whether the board's own
 * crosshair is the pointer.
 *
 * Tracked rather than queried on demand so HideCursor/ShowCursor are only
 * called on transitions. Both reach into GLFW and change platform cursor
 * state, so calling them every frame would be needless work 240 times a
 * second. It also guarantees the two can never disagree: the crosshair is drawn
 * whenever this is true, and never when it is false.
 */
static bool g_cursor_hidden = false;

/**
 * Hides the OS cursor while it is over the window, and restores it otherwise.
 *
 * Deliberately HideCursor/ShowCursor and NOT DisableCursor/EnableCursor. The
 * latter pair is for first-person mouse-look and is wrong here twice over:
 * DisableCursor sets GLFW_CURSOR_DISABLED, which *confines* the pointer to the
 * window so the user cannot move it to another application, and both it and
 * EnableCursor call SetMousePosition(width/2, height/2), which teleports the
 * pointer to the middle of the screen every time either is called. HideCursor
 * uses GLFW_CURSOR_HIDDEN, which hides the cursor and leaves the mouse exactly
 * where the user put it.
 *
 * Only called on transitions: both reach into the platform window, so doing this
 * every frame would be pointless work 240 times a second. Tracking the state
 * here rather than asking raylib is what guarantees the crosshair is drawn if
 * and only if the OS cursor is hidden.
 */
static void UpdateCursorVisibility() {
  const Vector2 mouse = GetMousePosition();
  const bool over_window = referentia::board::IsInsideViewport(
    mouse.x, mouse.y, static_cast<float>(GetScreenWidth()),
    static_cast<float>(GetScreenHeight()));

  if (over_window == g_cursor_hidden) return;

  g_cursor_hidden = over_window;
  if (g_cursor_hidden) {
    HideCursor();
  } else {
    ShowCursor();
  }
  LOG_DEBUG("[CURSOR] OS cursor {} (pointer over window: {})",
            g_cursor_hidden ? "hidden" : "shown", over_window);
}

namespace m_app = referentia::app;
namespace m_eng = motrix::engine;
namespace m_font = referentia;

/** Viewport size in pixels, as the float pair the transform helpers take. */
static float ViewportW() { return static_cast<float>(GetScreenWidth()); }
static float ViewportH() { return static_cast<float>(GetScreenHeight()); }

/**
 * The one camera's transform, read from ECS.
 *
 * Four one-line accessors rather than a struct-returning one because the callers
 * each need one field at a conversion site, and returning the whole Camera2D
 * would invite a caller to read cam.camera from it and then bypass the ECS the
 * rest of the frame uses.
 */
static const Camera2D& TheCamera(const m_app::AppState& state) {
  // ecs.get() is non-const because a mutable component reference is what it
  // returns, so the const state is copied out of the lookup rather than
  // const_cast. These are read-only helpers and the camera is not written here.
  return const_cast<m_app::AppState&>(state)
      .ecs.get<referentia::components::CameraComponent>(state.cameraEntity)
      .camera;
}

static float CameraTargetX(const m_app::AppState& state) {
  return TheCamera(state).target.x;
}
static float CameraTargetY(const m_app::AppState& state) {
  return TheCamera(state).target.y;
}
static float CameraZoom(const m_app::AppState& state) {
  return TheCamera(state).zoom;
}

/**
 * Where a file chosen from the dialog lands: the centre of the viewport.
 *
 * A dialog has no pointer position to convert, and the middle of what the user
 * is looking at is the only answer that puts the image somewhere they will see
 * it. The camera target itself would be the same point, but going through
 * ScreenToWorldCentre keeps this honest if the mapping ever changes.
 */
static referentia::board::Vec2 DialogAnchor(const m_app::AppState& state) {
  return referentia::board::ScreenToWorldCentre(
      ViewportW() * 0.5f, ViewportH() * 0.5f, CameraTargetX(state),
      CameraTargetY(state), CameraZoom(state), ViewportW(), ViewportH());
}

static void PrintUsage() {
  printf("Usage: referentia [OPTIONS]\n");
  printf("  -h,  --help           Show this help message\n");
  printf("\nControls:\n");
  printf("  Wheel                 Zoom about the cursor\n");
  printf("  Middle-drag           Pan (or Space + left-drag)\n");
  printf("  =                     Reset target to the origin at zoom 1\n");
  printf("  ESC, Q                Quit\n");
  printf("  F10                   Toggle the board panel\n");
  printf("  F11                   Toggle the frame-rate readout\n");
  printf("  F8                    Toggle the frame-time graph\n");
  printf("  F12                   Save a screenshot to screenshots/\n");
  printf("\nImport:\n");
#if defined(PLATFORM_WEB)
  printf("  Drop on canvas        Place image nodes on the board\n");
  printf("  Open image... button  Same, via the browser's file chooser\n");
#else
  if (referentia::app::kFileDropSupported) {
    printf("  Drop on window        Place image nodes on the board\n");
    printf("  Open image... button  Same, via a native file chooser\n");
  } else {
    // Named explicitly rather than left out. A build that silently has no drag
    // and drop looks identical to one where the feature is broken.
    printf("  Open image... button  Place image nodes on the board\n");
    printf("                       (this build's raylib backend cannot receive\n");
    printf("                        file drops, so only the button works)\n");
  }
#endif
  printf("                       Formats: ");
  for (const std::string_view ext : referentia::board::kSupportedImageExtensions)
    printf(".%s ", ext.data());
  printf("\n");
}

// Returns true when the app should exit immediately without starting, i.e.
// --help was asked for.
static bool ParseCLIFlags(int argc, char** argv) {
  bool print_and_exit = false;

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];

    if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
      PrintUsage();
      print_and_exit = true;
    } else if (arg[0] == '-') {
      logger::warn("[CLI] Unknown option: '{}'", arg);
    }
  }

  return print_and_exit;
}

// Returns false if startup failed, in which case nothing was allocated and
// the caller must not enter the main loop.
static bool InitApp(m_app::AppState& state) {
  // Resizable: the board is an infinite viewport, so the window size is a
  // viewport size rather than a fixed canvas extent.
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
  InitWindow(WINDOW_W, WINDOW_H, "Referentia");

  // raylib's InitWindow is non-fatal on failure: it logs and returns with no
  // GL context, and every later call (font rasterising, BeginDrawing) then
  // dereferences one. On a headless machine that surfaced as a core dump part
  // way through startup, several confusing log lines after the real cause.
  // Bailing here makes it a single clear error and a non-zero exit.
  if (!IsWindowReady()) {
    logger::error(
      "[APP] Window creation failed (no display?). Referentia needs a "
      "desktop session; it cannot run headless. Exiting.");
    return false;
  }

  // ESC quits via raylib's own exit-key handling, which sets the same flag as
  // the window close button, so the shutdown path below is identical either
  // way. Stated explicitly rather than left to the default so the intent is
  // visible and so KEY_NULL cannot creep back in and silently disable it.
  SetExitKey(KEY_ESCAPE);

  // Deliberately no SetTargetFPS() call.
  //
  // SetTargetFPS installs a software frame limiter, so pairing it with
  // FLAG_VSYNC_HINT would serialise the two: the GPU would still block on the
  // display's refresh, and the timer would add a second, lower ceiling on top.
  // On a 60Hz panel that hides the problem; on 144Hz+ it silently pins the app
  // to 60 for no reason. Leaving the limiter unset lets FLAG_VSYNC_HINT pace
  // frames off the display itself, so the app runs at the monitor's native
  // refresh and stays in lockstep with it.
  //
  // Note: raylib applies FLAG_VSYNC_HINT to the GL context swap. On the Web
  // build the browser compositor owns presentation instead, so there is
  // nothing to configure here; raylib's Emscripten loop is already paced by
  // requestAnimationFrame at the display rate.
  const int refresh = GetMonitorRefreshRate(GetCurrentMonitor());
  display_refresh_rate = refresh;
  if (refresh <= 0)
    logger::warn(
      "[APP] Monitor refresh rate unavailable; relying on the default V-Sync "
      "path");
  else
    logger::info(
      "[APP] Presenting V-Synced to a maximum of {} Hz (no software frame "
      "limiter)",
      refresh);
  LOG_DEBUG("[APP] Window {}x{}, monitor {}, refresh {} Hz, ui_scale {}",
            WINDOW_W, WINDOW_H, GetCurrentMonitor(), refresh, ui_scale);

  SearchAndSetResourceDir("resources");

  referentia::LoadFonts();

  state.cameraEntity = referentia::entities::CreateCamera(state.ecs);

  int num_cores = std::thread::hardware_concurrency();
  m_eng::systems::InitThreads(num_cores);

  m_app::BoardInit(state);
  m_app::BoardCreateUI(state.ecs);

  // Back-pointer, set once the ECS exists. The importer is a member of
  // AppState and is default-constructed before the window is even up, so this
  // has to happen here rather than in a constructor.
  state.importer.ecs = &state.ecs;

#if defined(PLATFORM_WEB)
  // The browser's drop handler and hidden file input, wired to this importer.
  // The anchor provider is the same world point a desktop drop would use, so a
  // dropped file lands under the pointer in both builds. It captures state by
  // reference because the camera is ECS data, and the camera entity handle
  // exists from CreateCamera above.
  m_app::InstallWebFileBridge(state.importer, [&state] {
    return referentia::board::ScreenToWorldCentre(
        GetMousePosition().x, GetMousePosition().y, CameraTargetX(state),
        CameraTargetY(state), CameraZoom(state), ViewportW(), ViewportH());
  });
#else
  if (!m_app::kFileDropSupported) {
    logger::warn("[IMPORT] drag and drop unavailable: {}",
                 m_app::kFileDropUnsupportedReason);
  } else {
    logger::info("[IMPORT] drag and drop enabled; formats: {}",
                 referentia::board::ImageFileFilter());
  }
#endif

  return true;
}

static void UpdateApp(m_app::AppState& state, float dt) {
  referentia::systems::UpdateCamera2D(state.ecs);
  m_app::BoardUpdate(state, dt);

  // The import pipeline, in the order the three entry points require.
  //
  // Drops are polled first so a file dropped while a dialog is open is not
  // noticed until the dialog closes -- the chooser is modal and takes over the
  // event loop, so there is nothing to poll in the meantime anyway.
  m_app::PollDroppedFiles(state.importer, TheCamera(state));

  // The dialog is served here rather than in the widget pass that requested it.
  // All three desktop choosers block until the user finishes, and a block
  // mid-frame leaves a half-drawn board frozen behind the dialog until the next
  // present.
  m_app::FlushImageDialogRequest(state.importer, DialogAnchor(state));

  // Decodes whatever the two above queued. This is the only place textures are
  // uploaded, so it runs after the camera has been updated and the drop anchor
  // is computed from this frame's transform rather than last frame's.
  const int imported = m_app::UpdateImageImport(state.importer);
  if (imported > 0) {
    logger::info("[IMPORT] {} image(s) on the board ({} total, {} failed)",
                 imported, state.importer.imported_count,
                 state.importer.failed_count);
  }

  // Node dragging and rotation, after the import so a node created this frame can
  // be picked up immediately, and after the camera so the world<->screen
  // conversion uses this frame's transform rather than last frame's.
  referentia::systems::UpdateImageNodes(state.ecs, state.nodeGrab,
                                        TheCamera(state));
}

static void RenderApp(m_app::AppState& state) {
  BeginDrawing();

  ClearBackground(theme::board_background);

  auto& cam =
    state.ecs.get<referentia::components::CameraComponent>(state.cameraEntity);

  BeginMode2D(cam.camera);

  m_app::BoardRender(state);

  // Nodes, not inside BoardRender: the board's own render is the landmarks and
  // the world grid, which are the board's furniture, while these are its
  // contents. Drawn after them so an image covers the grid rather than being
  // grid-overlaid, which is what a reference image on top of a drawing surface
  // should look like.
  //
  // The camera is passed because the corner handles are sized in screen pixels
  // and need its zoom to become world units.
  referentia::systems::DrawImageNodes(state.ecs, cam.camera);

  EndMode2D();

  // TEMPORARY: screen-space verification readout, see systems/board_debug.h.
  // Read from the overlay entity, same as the world-space half, so the two
  // halves cannot disagree about whether the overlay is enabled.
  state.ecs.view<referentia::components::TagOverlay,
                 referentia::components::BoardDebugComponent>(
    [&](motrix::engine::Entity, referentia::components::TagOverlay&,
        referentia::components::BoardDebugComponent& cfg) {
      referentia::systems::DrawBoardDebugHud(cam.camera, cfg);
    });

  // BoardCreateUI is what F10 calls to build the panel, so the UI system does
  // not need to know what goes on it.
  referentia::systems::RenderUI(state.ecs, m_app::BoardCreateUI);

  if (IsKeyPressed(KEY_F11)) show_fps = !show_fps;

  // F8 toggles the frame-time graph on its own, deliberately not as a sub-toggle
  // of F11. Gating it behind the readout meant F8 did nothing unless the readout
  // happened to be on, which is indistinguishable from a broken key.
  if (IsKeyPressed(KEY_F8)) {
    show_frame_graph = !show_frame_graph;
    if (show_frame_graph) g_frame_history.Clear();
    LOG_DEBUG("[APP] frame graph {}", show_frame_graph ? "on" : "off");
  }

  // Readout and graph are separate toggles but one panel, drawn top-right: the
  // graph sits directly under the frame rate so the two readings describe the
  // same moment. g_graph_ceiling is the graph's smoothed vertical scale, owned
  // here so it survives between frames.
  referentia::systems::DrawPerfPanel(g_frame_history, display_refresh_rate,
                                     show_fps, show_frame_graph,
                                     g_graph_ceiling);

  // The pointer is the last thing drawn, on top of the board, the HUD, the
  // board panel and the perf panel. Drawn earlier it disappeared whenever it
  // crossed a widget, which is exactly when the user is aiming at one. Screen
  // space, after EndMode2D, so it neither scales with the camera nor slides away
  // from the physical mouse.
  if (g_cursor_hidden) referentia::systems::DrawBoardCursor();

  // F12 captures the framebuffer. Last, so the screenshot contains the whole
  // frame -- overlays, panels and the pointer -- rather than whatever had been
  // drawn at the point the key handler happened to sit. The panel's Screenshot
  // button queues the same request from inside the widget pass and is served
  // here, so both paths write the same complete frame.
  if (IsKeyPressed(KEY_F12)) referentia::app::RequestScreenshot();
  referentia::app::FlushScreenshotRequest();

  EndDrawing();
}

int RunApp(int argc, char** argv) {
  if (ParseCLIFlags(argc, argv)) return 0;

  m_app::AppState state;

  if (!InitApp(state)) {
    // InitApp already reported why. Returning non-zero matters: a wrapper or
    // CI job must be able to tell "ran and exited" from "never started".
    return 1;
  }

  logger::info("[APP] Board ready, ECS holds {} entities",
               state.ecs.live_entity_count());

  // A stall is reported to the log whatever the graph is doing: one line is
  // cheap, and a real 50ms hitch is a fact about the run rather than a trend
  // you opted into seeing.
  constexpr int kHitchReportMs = 50;

  bool request_quit = false;

  while (!WindowShouldClose() && !request_quit) {
    const float dt = GetFrameTime();

    // Recorded every frame, graph or no graph, so F8 shows the seconds leading
    // up to the press instead of starting from an empty panel. dt is read at
    // the top of the frame, which is the interval since the previous one and so
    // already accounts for everything the last iteration did.
    g_frame_history.Push(dt * 1000.f);

    // Before update and render, so the frame's pointer state is already
    // consistent: input handling must not run against a stale cursor state, and
    // the crosshair is drawn from the same flag the renderer reads.
    UpdateCursorVisibility();

    UpdateApp(state, dt);
    RenderApp(state);

    // Q quits, but not while a widget owns the keyboard: typing "q" into a text
    // field must insert a q, not close the app. ESC is raylib's exit key and
    // deliberately still works, so there is always a way out.
    if (IsKeyPressed(KEY_Q) && !ui_keyboard_capture) {
      logger::debug("[APP] Quit requested by the Q key");
      request_quit = true;
    }

    if (dt * 1000.0 > kHitchReportMs) {
      LOG_DEBUG("[APP] frame took {} ms (vsync is {} Hz)",
                logger::detail::fixed(dt * 1000.0, 1), display_refresh_rate);
    }
  }

  logger::info("[APP] Shutting down: {} entities, {} threads",
               state.ecs.live_entity_count(), m_eng::systems::num_threads);

  // Thread pool is stopped before the window: worker tasks may still touch ECS
  // or texture handles, and tearing down the GL context first would leave them
  // freeing from a dead context.
  m_eng::systems::ShutdownThreads();

  // Imported textures go next, for the same reason and for the other half of it:
  // a dropped texture is not reclaimed by anything, so a session that imported
  // a few hundred screenshots would otherwise leak every one of them. The ECS
  // still holds the nodes at this point, which is harmless because the window is
  // about to close and no further query will run.
  m_app::UnloadImportedTextures(state.importer);

  // Fonts are unloaded while the GL context is still alive, so their textures
  // are freed rather than leaked into a context that is about to vanish.
  referentia::UnloadFonts();

  // The OS cursor is a global platform resource, not a window one. Restoring it
  // before teardown means the user is not left without a pointer in whatever
  // they switch to next if the window dies while the cursor was hidden.
  if (g_cursor_hidden) {
    ShowCursor();
    g_cursor_hidden = false;
    logger::debug("[CURSOR] OS cursor restored on shutdown");
  }

  CloseWindow();

  logger::info("[APP] Exited cleanly");

  // Bookend the run with the same mark build.sh prints at the top, so a launch
  // in a scrollback is visibly closed rather than trailing off. After CloseWindow
  // so it lands in the terminal once the window is gone.
  m_app::PrintBanner();

  return 0;
}
