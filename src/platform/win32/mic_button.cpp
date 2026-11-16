#include "platform/win32/mic_button.h"

#include "platform/win32/mic_glyph.h"

#include <shellapi.h>
#include <windowsx.h>

#include <cstdint>
#include <vector>

namespace pt {
namespace win32 {
namespace {

constexpr wchar_t kClassName[] = L"V2TMicButton";
constexpr int kButtonSize = 64;
constexpr int kMargin = 24;

MicButton* g_instance = nullptr;

std::wstring widen(const std::string& value) {
  if (value.empty()) return std::wstring();
  const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                          static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0],
                      needed);
  return result;
}

}  // namespace

MicButton::~MicButton() {
  hide();
}

bool MicButton::show(const std::string& edge, std::string& err) {
  edge_ = edge;

  if (window_ == nullptr) {
    WNDCLASSEXW window_class;
    ZeroMemory(&window_class, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &MicButton::window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = kClassName;

    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      err = "cannot register the mic button window";
      return false;
    }

    g_instance = this;
    window_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kClassName, L"", WS_POPUP, 0, 0, kButtonSize, kButtonSize, nullptr, nullptr,
        window_class.hInstance, nullptr);
    if (window_ == nullptr) {
      err = "cannot create the mic button window";
      g_instance = nullptr;
      return false;
    }

    // Click-through except on the button glyph itself.
    const HRGN region = CreateRoundRectRgn(0, 0, kButtonSize, kButtonSize, 24, 24);
    SetWindowRgn(window_, region, TRUE);
  }

  update_position();
  ShowWindow(window_, SW_SHOWNOACTIVATE);
  UpdateWindow(window_);
  return true;
}

void MicButton::hide() {
  if (window_ != nullptr) {
    ShowWindow(window_, SW_HIDE);
  }
}

void MicButton::set_state(MicButtonState state, const std::string& tooltip) {
  if (state_ == state && tooltip_ == tooltip) return;
  state_ = state;
  tooltip_ = tooltip;
  if (window_ != nullptr) {
    SetWindowTextW(window_, tooltip_.empty() ? L"" : widen(tooltip_).c_str());
    InvalidateRect(window_, nullptr, TRUE);
  }
}

void MicButton::update_position() {
  if (window_ == nullptr) return;

  RECT rect;
  SystemParametersInfo(SPI_GETWORKAREA, 0, &rect, 0);
  const int work_width = rect.right - rect.left;
  const int work_height = rect.bottom - rect.top;
  const int y = rect.top + (work_height - kButtonSize) / 2;

  if (edge_ == "left") {
    SetWindowPos(window_, HWND_TOPMOST, rect.left + kMargin, y, kButtonSize, kButtonSize,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
  } else {
    SetWindowPos(window_, HWND_TOPMOST, rect.right - kButtonSize - kMargin, y, kButtonSize,
                 kButtonSize, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  }
}

bool MicButton::render_to_file(const std::string& path, MicButtonState state) const {
  const int size = kButtonSize;

  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateCompatibleBitmap(screen, size, size);
  HGDIOBJ old = SelectObject(dc, bitmap);

  RECT bounds{0, 0, size, size};
  const HBRUSH background = CreateSolidBrush(RGB(20, 20, 24));
  FillRect(dc, &bounds, background);
  DeleteObject(background);

  MicColour colour = MicColour::Idle;
  switch (state) {
    case MicButtonState::Idle: colour = MicColour::Idle; break;
    case MicButtonState::Active: colour = MicColour::Active; break;
    case MicButtonState::Working: colour = MicColour::Working; break;
    case MicButtonState::Attention: colour = MicColour::Attention; break;
  }
  draw_mic_glyph(dc, bounds, mic_colour(colour));

  BITMAPINFO info;
  ZeroMemory(&info, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = size;
  info.bmiHeader.biHeight = -size;  // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 24;
  info.bmiHeader.biCompression = BI_RGB;

  const size_t stride = ((size * 3 + 3) / 4) * 4;
  std::vector<uint8_t> pixels(stride * static_cast<size_t>(size), 0);
  GetDIBits(dc, bitmap, 0, size, pixels.data(), &info, DIB_RGB_COLORS);

  BITMAPFILEHEADER header;
  ZeroMemory(&header, sizeof(header));
  header.bfType = 0x4D42;
  header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
  header.bfSize = static_cast<DWORD>(header.bfOffBits + pixels.size());

  HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  bool ok = false;
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    ok = WriteFile(file, &header, sizeof(header), &written, nullptr) &&
         WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr) &&
         WriteFile(file, pixels.data(), static_cast<DWORD>(pixels.size()), &written, nullptr);
    CloseHandle(file);
  }

  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  ReleaseDC(nullptr, screen);
  return ok;
}

void MicButton::paint() {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(window_, &ps);
  if (dc == nullptr) return;

  RECT bounds;
  GetClientRect(window_, &bounds);

  // A dark disc so the glyph reads on any wallpaper.
  const HBRUSH background = CreateSolidBrush(RGB(20, 20, 24));
  FillRect(dc, &bounds, background);
  DeleteObject(background);

  // One colour tells the whole story:
  //   grey  = recording, no audio arriving
  //   green = audio is arriving
  //   amber = the model is working
  //   red   = something went wrong
  MicColour colour = MicColour::Idle;
  switch (state_) {
    case MicButtonState::Idle: colour = MicColour::Idle; break;
    case MicButtonState::Active: colour = MicColour::Active; break;
    case MicButtonState::Working: colour = MicColour::Working; break;
    case MicButtonState::Attention: colour = MicColour::Attention; break;
  }
  draw_mic_glyph(dc, bounds, mic_colour(colour));

  EndPaint(window_, &ps);
}

LRESULT CALLBACK MicButton::window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  MicButton* self = g_instance;

  switch (message) {
    case WM_PAINT:
      if (self != nullptr) self->paint();
      return 0;
    case WM_LBUTTONUP:
      if (self != nullptr && self->on_click_) self->on_click_();
      return 0;
    case WM_RBUTTONUP:
      if (self != nullptr && self->on_quit_) self->on_quit_();
      return 0;
    case WM_MOUSEACTIVATE:
      // Click-through everywhere except on the glyph.
      if (GET_X_LPARAM(l_param) >= 4 && GET_X_LPARAM(l_param) <= kButtonSize - 4 &&
          GET_Y_LPARAM(l_param) >= 4 && GET_Y_LPARAM(l_param) <= kButtonSize - 4) {
        return MA_ACTIVATE;
      }
      return MA_NOACTIVATE;
    case WM_DESTROY:
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

}  // namespace win32
}  // namespace pt
