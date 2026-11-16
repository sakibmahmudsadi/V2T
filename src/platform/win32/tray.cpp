#include "platform/win32/tray.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "platform/win32/mic_glyph.h"

namespace pt {
namespace win32 {
namespace {

constexpr int kIconSize = 32;  // the shell scales it down

std::wstring widen(const std::string& value) {
  if (value.empty()) return std::wstring();
  const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                          static_cast<int>(value.size()), nullptr, 0);
  std::wstring result(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0],
                      needed);
  return result;
}

MicColour glyph_for(TrayState state) {
  switch (state) {
    case TrayState::Waiting: return MicColour::Idle;
    case TrayState::Recording: return MicColour::Active;
    case TrayState::Working: return MicColour::Working;
    case TrayState::Error: return MicColour::Attention;
  }
  return MicColour::Idle;
}

// The icon is a rounded dark badge with the microphone on it: the badge is
// what the shell shows, the glyph carries the state.
struct IconPixels {
  std::vector<uint8_t> bgra;  // kIconSize * kIconSize * 4
};

IconPixels render_pixels(MicColour state) {
  IconPixels pixels;
  pixels.bgra.assign(static_cast<size_t>(kIconSize) * kIconSize * 4, 0);

  BITMAPINFO info;
  ZeroMemory(&info, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = kIconSize;
  info.bmiHeader.biHeight = -kIconSize;  // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HDC dc = CreateCompatibleDC(screen);
  HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bitmap == nullptr || bits == nullptr) {
    if (bitmap != nullptr) DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return pixels;
  }

  HGDIOBJ old = SelectObject(dc, bitmap);

  // The badge.
  const COLORREF badge = RGB(24, 24, 28);
  const HBRUSH brush = CreateSolidBrush(badge);
  const RECT full{0, 0, kIconSize, kIconSize};
  FillRect(dc, &full, brush);
  DeleteObject(brush);

  // The glyph.
  const RECT glyph_bounds{4, 3, kIconSize - 4, kIconSize - 3};
  draw_mic_glyph(dc, glyph_bounds, mic_colour(state));

  GdiFlush();
  std::memcpy(pixels.bgra.data(), bits, pixels.bgra.size());

  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  ReleaseDC(nullptr, screen);

  // Alpha: everything outside the rounded badge is transparent.
  const double radius = 6.0;
  for (int y = 0; y < kIconSize; ++y) {
    for (int x = 0; x < kIconSize; ++x) {
      const size_t index = (static_cast<size_t>(y) * kIconSize + x) * 4;
      // Distance to the rounded square.
      const double dx = std::max(std::max(radius - x, 0.0),
                                 static_cast<double>(x - (kIconSize - 1 - radius)));
      const double dy = std::max(std::max(radius - y, 0.0),
                                 static_cast<double>(y - (kIconSize - 1 - radius)));
      const bool inside = (dx * dx + dy * dy) <= (radius * radius);
      pixels.bgra[index + 3] = inside ? 255 : 0;
    }
  }
  return pixels;
}

// Builds a shell icon (colour bitmap + 1-bit mask) from the rendered pixels.
HICON make_icon(MicColour state) {
  const IconPixels pixels = render_pixels(state);

  BITMAPINFO colour_info;
  ZeroMemory(&colour_info, sizeof(colour_info));
  colour_info.bmiHeader.biSize = sizeof(colour_info.bmiHeader);
  colour_info.bmiHeader.biWidth = kIconSize;
  colour_info.bmiHeader.biHeight = -kIconSize;
  colour_info.bmiHeader.biPlanes = 1;
  colour_info.bmiHeader.biBitCount = 32;
  colour_info.bmiHeader.biCompression = BI_RGB;

  void* colour_bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP colour_bitmap = CreateDIBSection(screen, &colour_info, DIB_RGB_COLORS, &colour_bits,
                                           nullptr, 0);
  if (colour_bitmap == nullptr || colour_bits == nullptr) {
    if (colour_bitmap != nullptr) DeleteObject(colour_bitmap);
    ReleaseDC(nullptr, screen);
    return nullptr;
  }
  std::memcpy(colour_bits, pixels.bgra.data(), pixels.bgra.size());

  // The mask: 0 where the icon is opaque.
  const int mask_stride = ((kIconSize + 31) / 32) * 4;
  std::vector<uint8_t> mask(static_cast<size_t>(mask_stride) * kIconSize, 0);
  for (int y = 0; y < kIconSize; ++y) {
    for (int x = 0; x < kIconSize; ++x) {
      const size_t index = (static_cast<size_t>(y) * kIconSize + x) * 4;
      if (pixels.bgra[index + 3] == 0) {
        mask[static_cast<size_t>(y) * mask_stride + (x / 8)] |=
            static_cast<uint8_t>(0x80u >> (x % 8));
      }
    }
  }

  BITMAPINFO mask_info;
  ZeroMemory(&mask_info, sizeof(mask_info));
  mask_info.bmiHeader.biSize = sizeof(mask_info.bmiHeader);
  mask_info.bmiHeader.biWidth = kIconSize;
  mask_info.bmiHeader.biHeight = -kIconSize;
  mask_info.bmiHeader.biPlanes = 1;
  mask_info.bmiHeader.biBitCount = 1;
  mask_info.bmiHeader.biCompression = BI_RGB;

  void* mask_bits = nullptr;
  HBITMAP mask_bitmap = CreateDIBSection(screen, &mask_info, DIB_RGB_COLORS, &mask_bits, nullptr,
                                         0);
  if (mask_bitmap == nullptr || mask_bits == nullptr) {
    if (mask_bitmap != nullptr) DeleteObject(mask_bitmap);
    DeleteObject(colour_bitmap);
    ReleaseDC(nullptr, screen);
    return nullptr;
  }
  std::memcpy(mask_bits, mask.data(), mask.size());

  ICONINFO info;
  ZeroMemory(&info, sizeof(info));
  info.hbmMask = mask_bitmap;
  info.hbmColor = colour_bitmap;

  HICON icon = CreateIconIndirect(&info);

  DeleteObject(mask_bitmap);
  DeleteObject(colour_bitmap);
  ReleaseDC(nullptr, screen);
  return icon;
}

}  // namespace

bool render_tray_icon(const std::string& path, TrayState state) {
  const IconPixels pixels = render_pixels(glyph_for(state));

  BITMAPINFO info;
  ZeroMemory(&info, sizeof(info));
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = kIconSize;
  info.bmiHeader.biHeight = -kIconSize;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 24;
  info.bmiHeader.biCompression = BI_RGB;

  const size_t stride = ((kIconSize * 3 + 3) / 4) * 4;
  std::vector<uint8_t> bmp(stride * static_cast<size_t>(kIconSize), 0);
  for (int y = 0; y < kIconSize; ++y) {
    for (int x = 0; x < kIconSize; ++x) {
      const size_t source = (static_cast<size_t>(y) * kIconSize + x) * 4;
      const size_t target = static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 3;
      bmp[target + 0] = pixels.bgra[source + 0];
      bmp[target + 1] = pixels.bgra[source + 1];
      bmp[target + 2] = pixels.bgra[source + 2];
    }
  }

  BITMAPFILEHEADER header;
  ZeroMemory(&header, sizeof(header));
  header.bfType = 0x4D42;
  header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
  header.bfSize = static_cast<DWORD>(header.bfOffBits + bmp.size());

  HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  bool ok = false;
  if (file != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    ok = WriteFile(file, &header, sizeof(header), &written, nullptr) &&
         WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr) &&
         WriteFile(file, bmp.data(), static_cast<DWORD>(bmp.size()), &written, nullptr);
    CloseHandle(file);
  }
  return ok;
}

TrayIcon::~TrayIcon() {
  destroy();
}

bool TrayIcon::create(HWND owner, const std::string& tooltip, std::string& err) {
  owner_ = owner;

  icons_[0] = make_icon(glyph_for(TrayState::Waiting));
  icons_[1] = make_icon(glyph_for(TrayState::Recording));
  icons_[2] = make_icon(glyph_for(TrayState::Working));
  icons_[3] = make_icon(glyph_for(TrayState::Error));
  for (HICON icon : icons_) {
    if (icon == nullptr) {
      err = "cannot build the tray icon";
      return false;
    }
  }

  ZeroMemory(&data_, sizeof(data_));
  data_.cbSize = sizeof(NOTIFYICONDATAW);
  data_.hWnd = owner;
  data_.uID = 1;
  data_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  data_.uCallbackMessage = kTrayCallbackMessage;
  data_.hIcon = icons_[0];
  wcsncpy(data_.szTip, widen(tooltip).c_str(), 63);

  if (!Shell_NotifyIconW(NIM_ADD, &data_)) {
    err = "cannot add the tray icon (Windows error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  created_ = true;

  // Ask for the modern behaviour: the icon lands in the overflow area of the
  // notification area until the user pins it.
  NOTIFYICONDATAW version = data_;
  version.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &version);
  return true;
}

void TrayIcon::set_state(TrayState state, const std::string& tooltip) {
  if (!created_) return;
  const int index = static_cast<int>(state);
  if (index >= 0 && index <= 3 && icons_[index] != nullptr) {
    data_.hIcon = icons_[index];
  }
  wcsncpy(data_.szTip, widen(tooltip).c_str(), 63);
  Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::destroy() {
  if (created_) {
    Shell_NotifyIconW(NIM_DELETE, &data_);
    created_ = false;
  }
  for (HICON& icon : icons_) {
    if (icon != nullptr) {
      DestroyIcon(icon);
      icon = nullptr;
    }
  }
}

}  // namespace win32
}  // namespace pt
