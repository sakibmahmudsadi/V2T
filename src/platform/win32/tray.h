// The system tray icon.
//
// One icon that always shows what the app is doing, so you can tell at a
// glance whether it is alive and what state it is in:
//
//   waiting   (grey)    the app is up, waiting for the hotkey
//   recording (green)  it is listening to you
//   working   (amber)  the model is decoding
//   error     (red)    something went wrong (the tooltip says what)
//
// The icons are drawn at runtime with GDI, so the project ships no image files.
#pragma once

#include <windows.h>

#include <string>

namespace pt {
namespace win32 {

// The message the shell sends to the owner window, and the command id of the
// "Quit" entry in the context menu.
constexpr UINT kTrayCallbackMessage = WM_APP + 40;
constexpr UINT kTrayCommandQuit = 1001;

enum class TrayState {
  Waiting,
  Recording,
  Working,
  Error,
};

// Writes one of the tray icons to a 24-bit BMP (used to eyeball it without
// taking a screenshot).
bool render_tray_icon(const std::string& path, TrayState state);

class TrayIcon {
 public:
  ~TrayIcon();

  // Adds the icon to the notification area. `owner` is the window that
  // receives the context menu command.
  bool create(HWND owner, const std::string& tooltip, std::string& err);

  // Swaps the icon and the tooltip.
  void set_state(TrayState state, const std::string& tooltip);

  void destroy();

 private:
  bool add_icon(std::string& err);
  void update_icon(TrayState state);

  HWND owner_ = nullptr;
  HICON icons_[4] = {nullptr, nullptr, nullptr, nullptr};
  NOTIFYICONDATAW data_{};
  bool created_ = false;
};

}  // namespace win32
}  // namespace pt
