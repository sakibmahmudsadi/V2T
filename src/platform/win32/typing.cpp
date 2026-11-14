#include "platform/win32/typing.h"

#include <windows.h>

#include <cstring>
#include <vector>

namespace pt {
namespace win32 {
namespace {

// Long texts are typed in chunks with a pause between them: SendInput has a
// limited queue and a single huge burst gets dropped by some applications.
constexpr size_t kChunkChars = 48;
constexpr int kChunkPauseMs = 6;

bool send_unicode(const std::vector<wchar_t>& units, std::string& err) {
  if (units.empty()) return true;

  std::vector<INPUT> inputs(units.size() * 2);
  for (size_t i = 0; i < units.size(); ++i) {
    INPUT& down = inputs[i * 2];
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = 0;
    down.ki.wScan = units[i];
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    down.ki.time = 0;
    down.ki.dwExtraInfo = 0;

    INPUT& up = inputs[i * 2 + 1];
    up.type = INPUT_KEYBOARD;
    up.ki.wVk = 0;
    up.ki.wScan = units[i];
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    up.ki.time = 0;
    up.ki.dwExtraInfo = 0;
  }

  const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(),
                              sizeof(INPUT));
  if (sent != inputs.size()) {
    err = "the window did not accept the typed text (error " +
          std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

}  // namespace

bool type_text(const std::string& text, std::string& err) {
  if (text.empty()) return true;

  std::vector<wchar_t> units;
  units.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    wchar_t unit = 0;
    size_t length = 1;
    if (lead < 0x80) {
      unit = static_cast<wchar_t>(lead);
    } else if ((lead & 0xE0) == 0xC0 && i + 1 < text.size()) {
      unit = static_cast<wchar_t>(((lead & 0x1F) << 6) |
                                   (static_cast<unsigned char>(text[i + 1]) & 0x3F));
      length = 2;
    } else if ((lead & 0xF0) == 0xE0 && i + 2 < text.size()) {
      unit = static_cast<wchar_t>(((lead & 0x0F) << 12) |
                                   ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                                   (static_cast<unsigned char>(text[i + 2]) & 0x3F));
      length = 3;
    } else if ((lead & 0xF8) == 0xF0 && i + 3 < text.size()) {
      unit = static_cast<wchar_t>(((lead & 0x07) << 18) |
                                   ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 12) |
                                   ((static_cast<unsigned char>(text[i + 2]) & 0x3F) << 6) |
                                   (static_cast<unsigned char>(text[i + 3]) & 0x3F));
      length = 4;
    } else {
      unit = static_cast<wchar_t>(lead);
    }
    units.push_back(unit);
    i += length;
  }

  for (size_t offset = 0; offset < units.size(); offset += kChunkChars) {
    const size_t count = std::min(kChunkChars, units.size() - offset);
    if (!send_unicode(std::vector<wchar_t>(units.begin() + offset,
                                           units.begin() + offset + count),
                      err)) {
      return false;
    }
    if (offset + count < units.size()) Sleep(kChunkPauseMs);
  }
  return true;
}

bool press_enter(std::string& err) {
  INPUT inputs[2];
  std::memset(inputs, 0, sizeof(inputs));
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_RETURN;
  inputs[1] = inputs[0];
  inputs[1].ki.dwFlags |= KEYEVENTF_KEYUP;

  const UINT sent = SendInput(2, inputs, sizeof(INPUT));
  if (sent != 2) {
    err = "the window did not accept the Enter key (error " +
          std::to_string(GetLastError()) + ")";
    return false;
  }
  return true;
}

}  // namespace win32
}  // namespace pt
