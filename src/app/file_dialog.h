// app/file_dialog.h
//
// Native file chooser for importing images.
//
// Each platform has a different modal dialog, and all three must not block the
// event loop from inside the UI pass: the caller invokes them from the update
// phase or from a deferred action (see app/screenshot.h for the same deferred
// pattern). The function below returns true if the user picked at least one file
// and appends their paths to `out_paths`.
//
// The filter is built from board/image_source.h so the dialog offers exactly the
// extensions the app can decode and rejects everything else up front, rather
// than opening a .webp and logging an error later.
//
// No raylib dependency: this is pure platform glue, so it can be compiled on
// the headless build targets without linking OpenGL. The web build never calls
// these, and the implementations are guarded by platform defines.
#pragma once

#include <string>
#include <vector>

namespace referentia::app {

/**
 * Opens a platform-native "Open Image Files" dialog and returns the selected
 * filesystem paths. Returns true if the user chose one or more files, false if
 * they cancelled or an error occurred.
 *
 * `multiple` defaults to true because dragging in a folder is the main case and
 * a single click is a single file.
 */
bool OpenImageFileDialog(std::vector<std::string>& out_paths,
                         bool multiple = true);

}  // namespace referentia::app
