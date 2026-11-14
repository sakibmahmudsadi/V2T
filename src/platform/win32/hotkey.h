// Global hotkey detection.
//
// Uses a low-level keyboard hook (WH_KEYBOARD_LL) because RegisterHotKey
// cannot tell right Ctrl from left Ctrl — and Right Ctrl + Right Alt is the
// combo this app uses.
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pt {
namespace win32 {

// Maps a key name ("rctrl", "ralt", "esc", "space", ...) to a virtual key
// code. Returns 0 when the name is unknown.
uint16_t key_code_from_name(const std::string& name);

class HotkeyListener {
 public:
  // `on_toggle` fires when the combo is pressed (press to start, press again
  // to stop). `on_cancel` fires when the cancel key is pressed.
  void set_callbacks(std::function<void()> on_toggle, std::function<void()> on_cancel) {
    on_toggle_ = std::move(on_toggle);
    on_cancel_ = std::move(on_cancel);
  }

  // Installs the hook. The calling thread must run a message loop.
  bool start(const std::vector<std::string>& combo, const std::string& cancel_key,
             std::string& err);
  void stop();

 private:
  static LRESULT CALLBACK hook_proc(int code, WPARAM w_param, LPARAM l_param);

  void handle_key(int virtual_key, bool pressed);

  void* hook_ = nullptr;
  std::vector<uint16_t> combo_;
  uint16_t cancel_key_ = 0;
  std::vector<bool> combo_down_;
  bool combo_was_down_ = false;
  std::function<void()> on_toggle_;
  std::function<void()> on_cancel_;
};

}  // namespace win32
}  // namespace pt
