// The microphone glyph, shared by the on-screen button and the tray icon.
//
// One drawing routine, so the button and the tray icon always look the same.
#pragma once

#include <windows.h>

namespace pt {
namespace win32 {

// Draws a microphone: a filled capsule (the head), a holder around it, a stem
// and a base line.
void draw_mic_glyph(HDC dc, const RECT& bounds, COLORREF colour);

// The colour of each app state.
enum class MicColour {
  Idle,      // recording, but the microphone is quiet
  Active,    // audio is arriving
  Working,   // the model is decoding
  Attention  // something went wrong
};

COLORREF mic_colour(MicColour state);

}  // namespace win32
}  // namespace pt
