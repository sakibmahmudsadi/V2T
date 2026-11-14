#include "platform/win32/hotkey.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace pt {
namespace win32 {
namespace {

struct NameToCode {
  const char* name;
  uint16_t code;
};

// The names the config accepts. Right-hand variants matter for this app.
const NameToCode kNames[] = {
    {"rctrl", VK_RCONTROL}, {"lctrl", VK_LCONTROL}, {"ctrl", VK_CONTROL},
    {"ralt", VK_RMENU},    {"lalt", VK_LMENU},     {"alt", VK_MENU},
    {"rshift", VK_RSHIFT}, {"lshift", VK_LSHIFT},  {"shift", VK_SHIFT},
    {"esc", VK_ESCAPE},    {"space", VK_SPACE},    {"enter", VK_RETURN},
    {"tab", VK_TAB},       {"backspace", VK_BACK},
};

HotkeyListener* g_active = nullptr;

}  // namespace

uint16_t key_code_from_name(const std::string& name) {
  std::string lowered;
  lowered.reserve(name.size());
  for (char c : name) {
    lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  for (const NameToCode& entry : kNames) {
    if (lowered == entry.name) return entry.code;
  }
  return 0;
}

bool HotkeyListener::start(const std::vector<std::string>& combo,
                            const std::string& cancel_key, std::string& err) {
  combo_.clear();
  for (const std::string& name : combo) {
    const uint16_t code = key_code_from_name(name);
    if (code == 0) {
      err = "unknown key name in the hotkey: '" + name + "'";
      return false;
    }
    combo_.push_back(code);
  }
  if (combo_.empty()) {
    err = "the hotkey needs at least one key";
    return false;
  }

  cancel_key_ = key_code_from_name(cancel_key);
  combo_down_.assign(combo_.size(), false);
  combo_was_down_ = false;

  HMODULE module = GetModuleHandleA(nullptr);
  hook_ = SetWindowsHookExA(WH_KEYBOARD_LL, &HotkeyListener::hook_proc, module, 0);
  if (hook_ == nullptr) {
    err = "cannot install the keyboard hook (Windows error " +
          std::to_string(GetLastError()) + ")";
    return false;
  }
  g_active = this;
  return true;
}

void HotkeyListener::stop() {
  if (hook_ != nullptr) {
    UnhookWindowsHookEx(static_cast<HHOOK>(hook_));
    hook_ = nullptr;
  }
  if (g_active == this) g_active = nullptr;
}

LRESULT CALLBACK HotkeyListener::hook_proc(int code, WPARAM w_param, LPARAM l_param) {
  if (code == HC_ACTION && g_active != nullptr) {
    const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l_param);
    if (info != nullptr) {
      const bool pressed = (w_param == WM_KEYDOWN || w_param == WM_SYSKEYDOWN);
      const bool released = (w_param == WM_KEYUP || w_param == WM_SYSKEYUP);
      if (pressed || released) {
        g_active->handle_key(static_cast<int>(info->vkCode), pressed);
      }
    }
  }
  return CallNextHookEx(nullptr, code, w_param, l_param);
}

void HotkeyListener::handle_key(int virtual_key, bool pressed) {
  // Cancel key.
  if (cancel_key_ != 0 && virtual_key == cancel_key_ && pressed) {
    if (on_cancel_) on_cancel_();
    return;
  }

  const auto it = std::find(combo_.begin(), combo_.end(), static_cast<uint16_t>(virtual_key));
  if (it == combo_.end()) return;

  const size_t index = static_cast<size_t>(it - combo_.begin());
  combo_down_[index] = pressed;

  const bool all_down = std::all_of(combo_down_.begin(), combo_down_.end(),
                                    [](bool down) { return down; });
  if (all_down && !combo_was_down_) {
    combo_was_down_ = true;
    if (on_toggle_) on_toggle_();
  } else if (!all_down) {
    combo_was_down_ = false;
  }
}

}  // namespace win32
}  // namespace pt
