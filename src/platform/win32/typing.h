// Typing text into whatever window has focus.
//
// Uses SendInput with KEYEVENTF_UNICODE, so the text lands exactly as it is
// (any layout, any keyboard) and the clipboard is never touched.
#pragma once

#include <string>

namespace pt {
namespace win32 {

// Types `text` at the cursor of the focused window.
bool type_text(const std::string& text, std::string& err);

// Sends a single Enter keypress.
bool press_enter(std::string& err);

}  // namespace win32
}  // namespace pt
