// app/file_drop_web.h
//
// Browser file drop, and the hidden <input type="file"> behind the dialog
// button.
//
// Two browser facts shape this file, and both explain why it looks nothing like
// the desktop path:
//
//   • A dropped file is not a path. The page gets a File whose bytes live in a
//     sandboxed blob store, and a web page has no filesystem to name. Every
//     request on this platform therefore carries bytes and a name, which is why
//     board/image_source.h has an explicit bytes field rather than pretending a
//     path exists.
//
//   • A file input is asynchronous. The click returns immediately and the user
//     picks a file some time later; the only notification is a change event.
//     There is no call that blocks and returns a path, so the desktop dialogs and
//     this cannot share anything but the name of the request they both end up
//     making.
//
// The bridge is one-way: JS calls a C function, which copies the bytes out of
// the emscripten heap and hands them to app/image_import.h. Compiled out
// entirely unless PLATFORM_WEB, so including this header anywhere else is
// harmless rather than a link error.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/logger.h"

#if defined(PLATFORM_WEB)

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "app/image_import.h"

namespace referentia::app {

namespace board_ns = referentia::board;
namespace engine = motrix::engine;

/**
 * The importer the web bridge feeds, and where a drop lands.
 *
 * Both are set once at startup by InstallWebFileBridge() and never reassigned,
 * which is what makes it safe for a browser event to reach them: the callback
 * runs on the browser's stack, where there is no C++ frame to pass a reference
 * through. A null check on every call is the alternative, and it would be
 * checked on the first drop of a session and never again.
 */
inline ImageImporter* g_web_importer = nullptr;

/**
 * Supplies the world point a newly arrived file is centred on.
 *
 * A plain function pointer rather than a std::function: this is called once per
 * dropped file, and a function pointer is exactly the shape of the thing that
 * knows about the camera. Set by the app, which is the only place that knows both
 * the camera entity and the panel.
 */
inline board_ns::Vec2 (*g_web_drop_anchor)() = nullptr;

/**
 * C entry point for one arrived file. Called from JS with a pointer into the
 * emscripten heap.
 *
 * The bytes are copied before returning. The block belongs to the caller -- the
 * JS side frees it as soon as this returns -- so a queue that held the pointer
 * would decode freed memory one frame later, which in practice is a decode that
 * works for one file and corrupts for the next.
 */
extern "C" EMSCRIPTEN_KEEPALIVE void referentia_web_on_file(
    const char* name, unsigned char* data, int size) {
  if (name == nullptr || size <= 0 || g_web_importer == nullptr) return;

  const std::string name_str(name);
  if (!board_ns::IsSupportedImagePath(name_str)) {
    engine::LOG_WARN("Ignoring unsupported dropped file: {}", name_str);
    return;
  }

  const board_ns::Vec2 anchor =
      (g_web_drop_anchor != nullptr) ? g_web_drop_anchor() : board_ns::Vec2{0.f, 0.f};

  // The check is here as well as in QueueImportFromBytes because this is the
  // one entry point that receives bytes directly from outside the app: a
  // hand-crafted message on the JS console reaches this function.
  if (static_cast<unsigned long>(size) >
      static_cast<unsigned long>(INT32_MAX)) {
    engine::LOG_ERROR("Dropped file is too large to import: {}", name_str);
    return;
  }

  QueueImportFromBytes(*g_web_importer, name_str,
                       std::vector<unsigned char>(data, data + size), anchor);
}

/**
 * Installs the JS side: a drop listener on the canvas and a hidden file input.
 *
 * Written as EM_JS rather than a separate .js asset because the payload is two
 * handlers and one small state object, and a separate file would have to be
 * copied into the build output and kept in step with this header by hand.
 */
EM_JS(void, referentia_web_install, (), {
  var canvas = Module.canvas;
  if (!canvas) {
    console.warn('[referentia] no canvas; file drop is unavailable');
    return;
  }

  var send = function (name, bytes) {
    // The boundary only understands pointers and numbers, never a JS typed
    // array, so the bytes are staged in the emscripten heap and freed here.
    // The +1 keeps malloc(0) from being called for a genuinely empty file.
    var size = bytes.length;
    var ptr = _malloc(size > 0 ? size : 1);
    if (size > 0) HEAPU8.set(bytes, ptr);
    _referentia_web_on_file(UTF8ToString(name), ptr, size);
    _free(ptr);
  };

  // dragover must be cancelled or the browser navigates to the dropped file and
  // the whole app is replaced by a picture of the user's screenshot. This is the
  // single most important line in the file and it does nothing visible.
  canvas.addEventListener('dragover', function (e) {
    e.preventDefault();
  });

  canvas.addEventListener('drop', function (e) {
    e.preventDefault();
    var items = e.dataTransfer && e.dataTransfer.files;
    if (!items) return;
    for (var i = 0; i < items.length; i++) {
      var file = items[i];
      var reader = new FileReader();
      reader.onload = function (e) {
        send(file.name, new Uint8Array(e.target.result));
      };
      // Async: the drop event returns long before these finish, which is why
      // nothing here can return a path to the caller.
      reader.readAsArrayBuffer(file);
    }
  });

  // The dialog button. A real <input type=file> is the only file chooser a page
  // is allowed to open, and it must be in the document (and clicked from a user
  // gesture) or the browser blocks it. accept is built from the same extension
  // table, so the browser's own chooser offers exactly the formats the app can
  // decode instead of "all files".
  var input = document.createElement('input');
  input.type = 'file';
  input.multiple = true;
  input.style.display = 'none';
  input.accept = '.png,.jpg,.jpeg,.bmp,.tga,.gif,.qoi,.dds';

  input.addEventListener('change', function () {
    var files = input.files;
    for (var i = 0; i < files.length; i++) {
      var file = files[i];
      var reader = new FileReader();
      reader.onload = function (e) {
        send(file.name, new Uint8Array(e.target.result));
      };
      reader.readAsArrayBuffer(file);
    }
    // Cleared so picking the same file twice fires 'change' again. Without this,
    // a user who imports a file, deletes it and picks it again sees nothing
    // happen and concludes the button is broken.
    input.value = '';
  });

  document.body.appendChild(input);
  Module.referentiaWebInput = input;
});

/**
 * Opens the browser's file chooser, for the "Open image..." button.
 *
 * emscripten_run_script rather than a second EM_JS function: the input element
 * is owned by the JS installed above, and reaching it from C++ is one line. The
 * null check lives in the script so a click before startup finished is a no-op
 * instead of a thrown TypeError that would stop the frame.
 */
inline void RequestWebImageDialog() {
  emscripten_run_script(
      "if (Module.referentiaWebInput) Module.referentiaWebInput.click();");
}

/**
 * Wires the browser handlers to an importer. Call once, after the ECS exists.
 *
 * Takes the anchor provider by pointer rather than reading a global from the app
 * so the dependency is visible in the signature: whoever installs the bridge is
 * also the one promising where a drop lands.
 */
inline void InstallWebFileBridge(ImageImporter& importer,
                                 board_ns::Vec2 (*anchor_provider)()) {
  g_web_importer = &importer;
  g_web_drop_anchor = anchor_provider;
  referentia_web_install();
}

}  // namespace referentia::app

#endif  // PLATFORM_WEB
