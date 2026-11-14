// The mic button — the app's only visible element.
//
// A small always-on-top popup docked to the right edge of the screen. It is
// click-through everywhere except on the button itself, so it never gets in
// the way of the window you are typing into.
//
//   click       -> stop the recording
//   right-click -> quit the app
#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace pt {
namespace win32 {

enum class MicButtonState {
  Idle,       // recording, but no audio is arriving
  Active,     // audio is arriving
  Working,    // the model is decoding
  Attention,  // something went wrong (the tooltip says what)
};

class MicButton {
 public:
  ~MicButton();

  // `on_click` fires when the button is clicked, `on_quit` on right-click.
  void set_callbacks(std::function<void()> on_click, std::function<void()> on_quit) {
    on_click_ = std::move(on_click);
    on_quit_ = std::move(on_quit);
  }

  // Creates the window and shows it.
  bool show(const std::string& edge, std::string& err);

  // Hides the window (the app shows nothing while idle).
  void hide();

  void set_state(MicButtonState state, const std::string& tooltip = std::string());
  MicButtonState state() const { return state_; }

 private:
  static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  void paint();
  void update_position();

  HWND window_ = nullptr;
  HICON icon_ = nullptr;
  MicButtonState state_ = MicButtonState::Idle;
  std::string tooltip_;
  std::string edge_ = "right";
  std::function<void()> on_click_;
  std::function<void()> on_quit_;
};

}  // namespace win32
}  // namespace pt
