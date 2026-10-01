// app/file_dialog_linux.cpp
//
// Native file chooser for Linux, with three tiers in preference order.
//
//   1. GTK 3        -- the real thing, compiled in when premake finds it.
//   2. zenity       -- the GNOME CLI dialog, present on nearly every desktop.
//   3. kdialog      -- the KDE CLI dialog, the equivalent for KDE sessions.
//
// The CLI fallbacks exist because premake's pkg-config probe runs at build time
// on the build machine and cannot know what the *running* machine has: a user
// who compiled without libgtk-3-dev would otherwise have no way to open files at
// all, even sitting at a terminal where zenity is already in PATH. They are a
// last resort, not the default. If GTK3 was found at build time it is used, so
// such a build never shells out.
//
// A console prompt is deliberately absent. A stdin prompt in a windowed app is
// unusable, and a fake fallback that reads a line from the terminal would make
// the app look like it has a file dialog when it does not. With nothing
// available, the function returns false and says why.

#include "app/file_dialog.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "board/image_source.h"
#include "engine/logger.h"

#if defined(__linux__) || defined(__linux)

#if defined(REF_HAVE_GTK3)
#include <gtk/gtk.h>
#endif

namespace referentia::app {

namespace board = referentia::board;

namespace {

/**
 * Shell-quotes a single argument for /bin/sh.
 *
 * The CLI dialogs are run through a shell because neither zenity nor kdialog
 * takes a filter as an argv array. A path may legitimately contain spaces and
 * quotes -- a user's folder called "Gabriel's Images" -- so the arguments are
 * quoted rather than interpolated. Without this the dialog opens the wrong
 * directory, silently.
 */
std::string ShellQuote(const std::string& arg) {
  std::string out = "'";
  for (const char c : arg) {
    // A single quote cannot appear inside single quotes, so close, escape, reopen.
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

/** Whether an executable is in PATH. Runs a shell; no fork-free check exists. */
bool HasCommand(const std::string& name) {
  return std::system(("command -v " + name + " > /dev/null 2>&1").c_str()) == 0;
}

/**
 * Splits newline-separated dialog output into paths.
 *
 * Both CLI dialogs emit one path per line for multi-select. The carriage return
 * is stripped because kdialog writes CRLF, and a trailing \r would make the
 * extension check fail on every single file it returned -- the dialog would
 * appear to return nothing at all.
 */
std::vector<std::string> SplitLines(const std::string& blob) {
  std::vector<std::string> out;
  std::string current;

  for (const char c : blob) {
    if (c != '\n') {
      current += c;
      continue;
    }
    if (!current.empty() && current.back() == '\r') current.pop_back();
    if (!current.empty()) out.push_back(current);
    current.clear();
  }
  if (!current.empty()) {
    if (current.back() == '\r') current.pop_back();
    if (!current.empty()) out.push_back(current);
  }
  return out;
}

/** Runs `cmd`, returning its stdout split into lines. Exit status in `status`. */
std::vector<std::string> RunAndCapture(const std::string& cmd, int& status) {
  std::vector<std::string> out;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    status = -1;
    return out;
  }

  std::string blob;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) blob += buffer;
  status = pclose(pipe);

  return SplitLines(blob);
}

/**
 * The extension allowlist as a shell dialog's filter syntax.
 *
 * Shared by zenity and kdialog because both spell it the same way: patterns
 * separated by spaces, and the whole thing passed as one argument. The trailing
 * "All files" entry is what lets a user with an unlisted extension still select
 * it and get a clear "unsupported format" message, rather than watching their
 * file simply not appear in the list.
 */
std::string CliImageFilter() {
  std::string filter = "Images|";
  bool first = true;
  for (const std::string_view ext : board::kSupportedImageExtensions) {
    if (!first) filter += " ";
    first = false;
    filter += "*." + std::string(ext);
  }
  filter += " |All files|*";
  return filter;
}

#if defined(REF_HAVE_GTK3)

/**
 * GTK3 chooser, present when premake detected gtk+-3.0.
 *
 * The accept filter is a convenience and not a guarantee: GTK lets a user type
 * an arbitrary path and accept it regardless of the filter. The extension is
 * re-checked by the import layer, which is the real gate -- this filter only
 * saves the user from picking something that will be refused.
 *
 * GTK has to be initialised before any widget exists. It is initialised on first
 * use rather than at startup so a session that never opens a dialog pays
 * nothing, and the lambda guard makes the second call a no-op.
 */
struct DialogResult {
  /** Whether a dialog was actually shown. False means "could not try". */
  bool shown = false;
  /** Whether the user chose at least one file. */
  bool accepted = false;
  std::vector<std::string> paths;
};

DialogResult DialogGtk3(bool multiple) {
  static const bool gtk_ready = [] {
    if (!gtk_init_check(nullptr, nullptr)) {
      logger::error(
          "GTK3 is compiled in but gtk_init_check() failed: no display "
          "available (headless session, or DISPLAY/WAYLAND_DISPLAY unset).");
      return false;
    }
    return true;
  }();

  DialogResult result;
  if (!gtk_ready) return result;

  GtkWidget* dialog = gtk_file_chooser_dialog_new(
      "Open Images", nullptr, GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
      GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, nullptr);
  if (dialog == nullptr) return result;
  result.shown = true;

  GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
  gtk_file_chooser_set_select_multiple(chooser, multiple ? TRUE : FALSE);

  GtkFileFilter* images = gtk_file_filter_new();
  gtk_file_filter_set_name(images, "Images");
  for (const std::string_view ext : board::kSupportedImageExtensions) {
    const std::string pattern = "*." + std::string(ext);
    gtk_file_filter_add_pattern(images, pattern.c_str());
  }
  gtk_file_chooser_add_filter(chooser, images);

  GtkFileFilter* all = gtk_file_filter_new();
  gtk_file_filter_set_name(all, "All files");
  gtk_file_filter_add_pattern(all, "*");
  gtk_file_chooser_add_filter(chooser, all);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    // gtk_file_chooser_get_filenames() returns a GSList of newly-allocated
    // UTF-8 *strings* (gchar*), to be freed with g_slist_free_full() and
    // g_free(). It is easy to confuse with its sibling
    // gtk_file_chooser_get_files(), which returns GFile objects instead, and
    // treating the elements as GFile means g_file_get_path() dispatches through
    // whatever bytes happen to sit where the vtable pointer should be -- which
    // segfaults the moment the user picks a file, and only then, so a manual
    // test that cancels the dialog appears healthy.
    GSList* files = gtk_file_chooser_get_filenames(chooser);
    for (GSList* item = files; item != nullptr; item = item->next) {
      result.paths.emplace_back(static_cast<const char*>(item->data));
    }
    g_slist_free_full(files, g_free);
    result.accepted = true;
  }
  gtk_widget_destroy(dialog);

  // A modal dialog leaves events queued; draining them means the window does not
  // repaint oddly or process a stale click the moment the dialog closes.
  while (gtk_events_pending()) gtk_main_iteration_do(FALSE);

  return result;
}

#endif  // REF_HAVE_GTK3

/** zenity --file-selection, if zenity is installed. */
bool TryZenity(bool multiple, std::vector<std::string>& out) {
  if (!HasCommand("zenity")) return false;

  std::string cmd = "zenity --file-selection --title='Open Images'";
  if (multiple) cmd += " --multiple";
  cmd += " --file-filter=" + ShellQuote(CliImageFilter()) + " 2>/dev/null";

  int status = 0;
  std::vector<std::string> picked = RunAndCapture(cmd, status);
  // zenity exits 1 when the user closes the dialog. That is a cancel, not a
  // failure, and logging it would put an error in the log every time the dialog
  // is dismissed -- which teaches a user to ignore the log.
  if (status != 0) return false;

  out = std::move(picked);
  return true;
}

/** kdialog --getopenfilename, if kdialog is installed. */
bool TryKdialog(bool multiple, std::vector<std::string>& out) {
  if (!HasCommand("kdialog")) return false;

  // kdialog separates multiple selections with " " (a single space) rather than
  // newlines, and the whole filename field is one argument.
  std::string cmd = "kdialog --title 'Open Images' --getopenfilename . ";
  cmd += ShellQuote(CliImageFilter());
  if (multiple) cmd += " --multiple --separate-output";
  cmd += " 2>/dev/null";

  int status = 0;
  std::vector<std::string> picked = RunAndCapture(cmd, status);
  if (status != 0) return false;

  if (!picked.empty()) {
    // SplitLines saw one line; kdialog's own separator is what actually
    // separates the paths inside it.
    const std::string joined = picked.front();
    out.clear();
    std::string current;
    for (const char c : joined) {
      if (c == ' ') {
        if (!current.empty()) out.push_back(current);
        current.clear();
      } else if (c != '\n' && c != '\r') {
        current += c;
      }
    }
    if (!current.empty()) out.push_back(current);
  }
  return true;
}

}  // namespace

bool OpenImageFileDialog(std::vector<std::string>& out_paths, bool multiple) {
  std::vector<std::string> picked;

#if defined(REF_HAVE_GTK3)
  {
    // `shown` distinguishes "the user cancelled" from "no display, so GTK could
    // not run at all". Conflating them would either pop a second dialog after a
    // deliberate cancel, or give up without trying the fallbacks.
    const DialogResult gtk = DialogGtk3(multiple);
    if (gtk.shown) {
      if (!gtk.accepted) return false;
      out_paths = gtk.paths;
      return true;
    }
    logger::warn("Falling back to a command-line file dialog.");
  }
#endif

  if (TryZenity(multiple, picked) && !picked.empty()) {
    out_paths = std::move(picked);
    return true;
  }
  if (TryKdialog(multiple, picked) && !picked.empty()) {
    out_paths = std::move(picked);
    return true;
  }

  logger::warn(
      "No file dialog available. Install libgtk-3-dev and rebuild, or install "
      "zenity or kdialog.");
  return false;
}

}  // namespace referentia::app

#endif  // __linux__
