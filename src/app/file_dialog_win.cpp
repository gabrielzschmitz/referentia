// app/file_dialog_win.cpp
//
// Native file chooser for Windows, via the COM IFileOpenDialog interface.
//
// The modern Item Dialog (Vista and later) rather than GetOpenFileName for
// three reasons that matter here: it is the dialog users already know, it can
// filter by extension without the user typing a wildcard into a file name box,
// and it returns multiple absolute paths in one call.
//
// No WRL. Microsoft::WRL::ComPtr would be the obvious choice, but wrl/client.h
// is not guaranteed to exist in every toolchain this project builds with (the
// MinGW script in build-MinGW-W64.bat is one of them). The tiny RAII holder below
// is a dozen lines and keeps this file to nothing but the Windows SDK's own
// headers, which are required either way.
//
// COM must be initialised on this thread before any interface is used, and
// CoInitializeEx is reference counted per thread, so an unbalanced CoUninitialize
// tears down COM for the rest of the thread. ComScope below pairs them.

#include "app/file_dialog.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "board/image_source.h"
#include "engine/logger.h"

#if defined(_WIN32)

#include <windows.h>

#include <shobjidl.h>

namespace referentia::app {

namespace board = referentia::board;
namespace engine = motrix::engine;

namespace {

/**
 * An HRESULT as "0x80070057", ready to hand to the logger.
 *
 * Rendered here rather than with a printf-style or std::format-style specifier at
 * the call site because the project's logger substitutes bare {} and nothing
 * else. Given "0x{:08X}" it finds no {} at all, so it drops the argument without
 * complaint and writes the specifier into the log verbatim -- a diagnostic that
 * is silently useless, and useless only on the machine that already has a
 * problem.
 */
std::string HexHr(HRESULT hr) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
  return std::string(buf);
}

/** Calls CoUninitialize exactly once, and only if this scope initialised COM. */
class ComScope {
 public:
  ComScope()
      : m_owned(SUCCEEDED(::CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
    // Not fatal: the thread may already be in an apartment, in which case the
    // dialog interfaces still work. Logged because a clean call is the norm and
    // its absence usually means someone else owns the apartment.
    if (!m_owned) {
      engine::LOG_WARN("CoInitializeEx failed; reusing the existing COM "
                       "apartment on this thread.");
    }
  }

  ~ComScope() {
    if (m_owned) ::CoUninitialize();
  }

  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

 private:
  bool m_owned;
};

/**
 * Owns one COM interface pointer and releases it on destruction.
 *
 * Written out rather than pulled from WRL so this compiles with any Windows
 * toolchain. COM's rules are narrow enough to be worth the twelve lines: Release
 * on destruction, never copied, get() for the raw pointer that the API wants.
 */
template <typename T>
class ComPtr {
 public:
  ComPtr() = default;
  explicit ComPtr(T* raw) : m_ptr(raw) {}

  ~ComPtr() {
    if (m_ptr != nullptr) m_ptr->Release();
  }

  T* get() const { return m_ptr; }

  T* operator->() const { return m_ptr; }

  /**
   * Address of the internal pointer, for COM APIs that allocate through an
   * out-parameter. This is what IID_PPV_ARGS needs, and it is the only correct
   * way to take ownership of a CoCreateInstance/GetResults result: passing the
   * raw member would let COM's own Release-based cleanup write to a copy.
   */
  T** Put() { return &m_ptr; }

  /** Hands over ownership, leaving this empty. */
  T* Release() {
    T* raw = m_ptr;
    m_ptr = nullptr;
    return raw;
  }

  // Move-only: two owners of one reference would Release it twice.
  ComPtr(ComPtr&& other) noexcept : m_ptr(other.m_ptr) { other.m_ptr = nullptr; }
  ComPtr& operator=(ComPtr&& other) noexcept {
    if (this != &other) {
      if (m_ptr != nullptr) m_ptr->Release();
      m_ptr = other.m_ptr;
      other.m_ptr = nullptr;
    }
    return *this;
  }

  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;

 private:
  T* m_ptr = nullptr;
};

/** Converts a COM wide string to UTF-8. Empty on failure or an empty input. */
std::string ToUtf8(const wchar_t* wide) {
  if (wide == nullptr || wide[0] == L'\0') return {};

  const int needed =
      ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
  if (needed <= 1) return {}  // <= 1 is either empty or a conversion failure.

  std::string out(static_cast<size_t>(needed - 1), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr,
                        nullptr);
  return out;
}

/**
 * The extension allowlist as one wide pattern string, e.g. "*.png;*.jpg;...".
 *
 * ASCII widening is safe here and only here: board/image_source.h documents the
 * table as lowercase ASCII, and adding a non-ASCII entry to it would silently
 * produce a filter that never matches. A one-byte-per-wchar loop is kept rather
 * than MultiByteToWideChar to make that assumption visible.
 */
std::wstring WideImagePattern() {
  std::wstring pattern;
  for (const std::string_view ext : board::kSupportedImageExtensions) {
    if (!pattern.empty()) pattern += L";";
    pattern += L"*.";
    pattern += std::wstring(ext.begin(), ext.end());
  }
  return pattern;
}

}  // namespace

bool OpenImageFileDialog(std::vector<std::string>& out_paths, bool multiple) {
  ComScope com;

  ComPtr<IFileOpenDialog> dialog;
  const HRESULT created = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                            CLSCTX_INPROC_SERVER,
                                            IID_PPV_ARGS(dialog.Put()));
  if (FAILED(created) || dialog.get() == nullptr) {
    // No shell, e.g. a locked-down session or a container. There is no fallback
    // worth having: the old Win32 common dialog is uglier and no more available.
    // Drag and drop still works, because it needs no dialog at all.
    engine::LOG_ERROR("Could not create the file open dialog (HRESULT {}). "
                      "Drag and drop still works.",
                      HexHr(created));
    return false;
  }

  // Get once, OR in every flag, then set once. SetOptions replaces the whole set,
  // so the common two-call pattern (read options, set one flag, then set another
  // from the stale copy) silently discards the first flag -- which is why
  // FOS_ALLOWMULTISELECT is routinely ignored in code written that way.
  DWORD options = 0;
  dialog->GetOptions(&options);
  // FOS_FORCEFILESYSTEM: the dialog cannot return a virtual filesystem entry
  //   (.zip, OneDrive placeholders) that LoadImage cannot open.
  // FOS_FILEMUSTEXIST: a typed name that does not exist fails here rather than
  //   later as a decode error.
  options |= FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST;
  if (multiple) options |= FOS_ALLOWMULTISELECT;
  dialog->SetOptions(options);

  // Two filter entries, not one. A single entry with eight patterns is shown with
  // all eight patterns as its *name*, which reads as noise in the type dropdown.
  const std::wstring image_pattern = WideImagePattern();
  const std::wstring all_pattern = L"*";
  const COMDLG_FILTERSPEC specs[] = {
      {1, image_pattern.c_str(), L"Image files"},
      {2, all_pattern.c_str(), L"All files"},
  };
  // The strings above outlive the SetFileTypes call, which stores the pointers
  // rather than copying, so both locals must stay in scope until Show() returns.
  dialog->SetFileTypes(static_cast<UINT>(std::size(specs)), specs);

  // 1-based: selects the image filter as the starting one, so a PNG is visible
  // without touching the dropdown.
  dialog->SetFileTypeIndex(1);

  const HRESULT shown = dialog->Show(nullptr);
  if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
  if (FAILED(shown)) {
    engine::LOG_ERROR("The file open dialog failed (HRESULT {}).", HexHr(shown));
    return false;
  }

  ComPtr<IShellItemArray> items;
  const HRESULT got_results = dialog->GetResults(items.Put());
  if (FAILED(got_results) || items.get() == nullptr) {
    engine::LOG_ERROR("The file open dialog returned no results (HRESULT {}).",
                      HexHr(got_results));
    return false;
  }

  DWORD count = 0;
  if (FAILED(items->GetCount(&count))) {
    engine::LOG_ERROR("Could not count the chosen files.");
    return false;
  }

  out_paths.clear();
  out_paths.reserve(count);

  for (DWORD i = 0; i < count; ++i) {
    ComPtr<IShellItem> item;
    if (FAILED(items->GetItemAt(i, item.Put())) || item.get() == nullptr) {
      continue;
    }

    PWSTR path = nullptr;
    // SIGDN_FILESYSPATH is the real path LoadImage can open. SIGDN_NORMALDISPLAY
    // would return something like "hero.png (2 of 3)" for duplicate names, which
    // no filesystem call can resolve.
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
        path != nullptr) {
      out_paths.push_back(ToUtf8(path));
      // CoTaskMemFree, not delete: GetDisplayName allocates from the COM task
      // allocator, and freeing it any other way corrupts the heap.
      ::CoTaskMemFree(path);
    }
  }

  if (out_paths.empty()) {
    // Unreachable with FOS_FORCEFILESYSTEM unless a shell extension misbehaved,
    // but returning false here is better than a silent no-op import.
    engine::LOG_WARN("The chosen items produced no filesystem paths.");
    return false;
  }

  return true;
}

}  // namespace referentia::app

#endif  // _WIN32
