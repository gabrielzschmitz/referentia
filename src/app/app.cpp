// app/app.cpp
#include "app/app.h"

#include <cstdio>
#include <cstring>
#include <thread>

#include "app/app_state.h"
#include "app/board.h"
#include "app/fonts.h"
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
#include "systems/ui.h"

static bool SHOW_FPS = true;

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

static void PrintUsage() {
  printf("Usage: referentia [OPTIONS]\n");
  printf("  -h,  --help           Show this help message\n");
  printf("\nControls:\n");
  printf("  Wheel                 Zoom about the cursor\n");
  printf("  Middle-drag           Pan (or Space + left-drag)\n");
  printf("  =                     Reset target to the origin at zoom 1\n");
  printf("  ESC, Q                Quit\n");
  printf("  F10                   Toggle the UI panel\n");
  printf("  F11                   Toggle the FPS readout\n");
  printf("  F3                    Toggle frame-time reporting\n");
  printf("  F8                    Log frame time (needs F3 on)\n");
  printf("  F12                   Dump the live ECS (debug builds only)\n");
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

  return true;
}

static void UpdateApp(m_app::AppState& state, float dt) {
  referentia::systems::UpdateCamera2D(state.ecs);
  m_app::BoardUpdate(state, dt);
}

static void RenderApp(m_app::AppState& state) {
  BeginDrawing();

  ClearBackground(theme::board_background);

  auto& cam =
    state.ecs.get<referentia::components::CameraComponent>(state.cameraEntity);

  BeginMode2D(cam.camera);

  m_app::BoardRender(state);

  EndMode2D();

  // The pointer, in screen space and after EndMode2D so the camera transform
  // does not scale it or let it slide away from the physical mouse. Drawn
  // whenever the OS cursor is hidden, which is why it sits here rather than in
  // the world-space board render.
  if (g_cursor_hidden) {
    referentia::systems::DrawBoardCursor();
  }

  // TEMPORARY: screen-space verification readout, see systems/board_debug.h.
  // Read from the overlay entity, same as the world-space half, so the two
  // halves cannot disagree about whether the overlay is enabled.
  state.ecs.view<referentia::components::TagOverlay,
                 referentia::components::BoardDebugComponent>(
    [&](motrix::engine::Entity, referentia::components::TagOverlay&,
        referentia::components::BoardDebugComponent& cfg) {
      referentia::systems::DrawBoardDebugHud(cam.camera, cfg);
    });

  // BoardCreateUI is what F10 calls to rebuild the panel after it has been
  // closed, so the UI system does not need to know what builds the panel.
  referentia::systems::RenderUI(state.ecs, m_app::BoardCreateUI);

  if (IsKeyPressed(KEY_F11)) SHOW_FPS = !SHOW_FPS;

  // F12 dumps the live ECS: every entity, its version and its components. This
  // is the check that "everything is an entity" is actually true, rather than
  // something the code only appears to do.
  if (IsKeyPressed(KEY_F12)) {
    state.ecs.print_entities(logger::Level::Debug,
                             {"TagCamera", "TagWorldRoot"});
  }

  if (SHOW_FPS) {
    // Top-right, right-aligned by measuring: the debug readout owns the
    // top-left corner, and a fixed origin here would sit underneath it.
    // Actual/cap is shown so a V-Sync failure is obvious: the two numbers
    // should match whatever the panel refreshes at.
    std::string fps_text = TextFormat("FPS: %d", GetFPS());
    if (display_refresh_rate > 0) {
      fps_text += TextFormat(" / %d", display_refresh_rate);
    }

    const float size = 18.f * ui_scale;
    const float spacing = 1.f * ui_scale;
    const Vector2 measured =
      MeasureTextEx(m_font::GetFont(m_font::FontWeight::SemiBold),
                    fps_text.c_str(), size, spacing);
    const float margin = 12.f * ui_scale;

    const Rectangle panel{
      static_cast<float>(GetScreenWidth()) - measured.x - margin * 2.f, margin,
      measured.x + margin * 2.f, measured.y + margin};
    DrawTextEx(m_font::GetFont(m_font::FontWeight::SemiBold), fps_text.c_str(),
               {panel.x + margin, panel.y + margin * 0.5f}, size, spacing,
               theme::selection_outline);
  }

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

  // Frame-time reporting is off by default. Debug builds now run at the
  // display's refresh rate, so a frame-counted interval meant twice a second
  // on a 240Hz panel: a wall of text for information nobody asked for.
  //
  // F3 toggles it. While on, one line per press; a single long frame is always
  // worth a line on its own, since that is a real stall rather than noise.
  constexpr int kHitchReportMs = 50;

  bool report_frame_time = false;
  bool request_quit = false;
  double window_ms = 0.0;
  long window_frames = 0;

  while (!WindowShouldClose() && !request_quit) {
    const float dt = GetFrameTime();

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

    if (IsKeyPressed(KEY_F3)) {
      report_frame_time = !report_frame_time;
      window_ms = 0.0;
      window_frames = 0;
      LOG_DEBUG("[APP] Frame-time reporting {}",
                report_frame_time ? "on" : "off");
    }

    if (report_frame_time) {
      window_ms += dt * 1000.0;
      ++window_frames;

      if (IsKeyPressed(KEY_F8)) {
        if (window_frames > 0) {
          const double avg = window_ms / window_frames;
          LOG_DEBUG("[APP] {} ms/frame over {} frames ({} FPS), window {}x{}",
                    logger::detail::fixed(avg, 2), window_frames,
                    logger::detail::fixed(1000.0 / avg, 0), GetScreenWidth(),
                    GetScreenHeight());
        }
        window_ms = 0.0;
        window_frames = 0;
      }
    }

    // A long frame is reported regardless of the toggle: it is a stall, not a
    // trend, and it is the thing worth noticing while developing.
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

  return 0;
}
