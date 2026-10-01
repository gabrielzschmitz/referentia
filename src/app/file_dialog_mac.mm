// app/file_dialog_mac.mm
//
// Native file chooser for macOS, via NSOpenPanel.
//
// This file is Objective-C++ (.mm) because NSOpenPanel is an AppKit class: it
// has to be sent Objective-C messages, which plain C++ cannot express. It lives
// in its own static library (referentia-mac, see build/premake5.lua) so that
// only the macOS build compiles it, and so the C++ side of the project never has
// to be built as Objective-C++.
//
// ARC is not assumed. The panel is autoreleased and its lifetime is bounded by
// the runModal call below, and under manual retain/release the autorelease pool
// is what frees it. Getting that wrong is not a crash on a modern OS, it is a
// panel that never appears on the second call, which is exactly the kind of bug
// that only shows up after the user has imported ten images.

#include "app/file_dialog.h"

#include <string>
#include <string_view>
#include <vector>

#include "board/image_source.h"
#include "engine/logger.h"

#if defined(__APPLE__)

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

namespace referentia::app {

namespace board = referentia::board;
namespace engine = motrix::engine;

namespace {

/** UTF-8 NSString, or empty for nil. */
std::string ToUtf8(NSString* text) {
  if (text == nil) return {};
  const char* utf8 = [text UTF8String];
  return utf8 != nullptr ? std::string(utf8) : std::string{};
}

/**
 * The extension allowlist as an NSArray of lowercase extension strings.
 *
 * UTType identifiers are not used here. They would be the modern answer, but
 * the deployment target is not pinned to a version where every listed format
 * has one, and an extension list works for all of them -- including the ones
 * raylib decodes through stb rather than through any Apple framework. A filter
 * that only offered formats macOS recognises would refuse files the app can
 * actually open.
 */
NSArray<NSString*>* AppleTypeExtensions() {
  NSMutableArray<NSString*>* out = [NSMutableArray array];
  for (const std::string_view ext : board::kSupportedImageExtensions) {
    [out addObject:[NSString stringWithUTF8String:std::string(ext).c_str()]];
  }
  return out;
}

}  // namespace

bool OpenImageFileDialog(std::vector<std::string>& out_paths, bool multiple) {
  // An autorelease pool, because runModal spins a nested event loop and anything
  // autoreleased during it would otherwise only be released when the whole
  // frame's pool drains -- a slow leak across every dialog the user opens.
  @autoreleasepool {
    // canChooseFiles (not canChooseDirectories): the import opens paths with
    // LoadImage, and a directory handed to it would be a failed decode rather
    // than a folder import.
    NSOpenPanel* panel = [NSOpenPanel openPanel];

    [panel setCanChooseFiles:YES];
    [panel setCanChooseDirectories:NO];
    [panel setAllowsMultipleSelection:multiple ? YES : NO];
    // Resolving symbolic links: what the user picked is what gets opened, and a
    // link to a file outside a synced folder otherwise imports a path that may
    // not exist next session.
    [panel setResolvesAliases:YES];

    [panel setTitle:@"Open Images"];
    [panel setMessage:@"Choose images to place on the board"];
    [panel setPrompt:@"Open"];

    // One allowed type per extension rather than a single UTType union, matching
    // the dialogs on the other platforms: the point of the filter is to let the
    // user see which of their files will import.
    [panel setAllowedFileTypes:AppleTypeExtensions()];
    [panel setAllowsOtherFileTypes:NO];

    // App-modal rather than window-modal: the panel is a standalone window, and
    // window-modal would tie it to a window this header knows nothing about.
    NSModalResponse response = [panel runModal];

    if (response != NSModalResponseOK) {
      // NSModalResponseCancel and the window-close button both land here. A
      // cancel is not an error, so it is not logged: doing so would put an error
      // in the log every time the user dismisses the dialog.
      return false;
    }

    NSArray<NSURL*>* urls = [panel URLs];
    out_paths.clear();
    out_paths.reserve(urls.count);

    for (NSURL* url in urls) {
      // path, not absoluteString: LoadImage wants a filesystem path, and the
      // percent-escaped URL form would be decoded as literal characters.
      NSString* path = [url path];
      if (path == nil) continue;

      // Spaces and non-ASCII characters are legal in a path, so this is not
      // cosmetic: an unescaped URL string would fail to open a file named
      // "Joao's.png".
      out_paths.push_back(ToUtf8(path));
    }

    if (out_paths.empty()) {
      engine::LOG_WARN("The panel returned no usable file paths.");
      return false;
    }

    return true;
  }
}

}  // namespace referentia::app

#endif  // __APPLE__
