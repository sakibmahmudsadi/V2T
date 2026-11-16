#include "platform/win32/mic_glyph.h"

#include <algorithm>

namespace pt {
namespace win32 {

void draw_mic_glyph(HDC dc, const RECT& bounds, COLORREF colour) {
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  const int cx = bounds.left + width / 2;

  const int head_width = std::max(6, width / 3);
  const int head_height = std::max(8, height / 2);
  const int head_top = bounds.top + height / 8;
  const int head_bottom = head_top + head_height;

  const int stroke = std::max(2, width / 12);

  HPEN pen = CreatePen(PS_SOLID, stroke, colour);
  HPEN old_pen = (HPEN)SelectObject(dc, pen);
  HBRUSH brush = CreateSolidBrush(colour);
  HBRUSH old_brush = (HBRUSH)SelectObject(dc, brush);

  // The head: a filled capsule.
  RoundRect(dc, cx - head_width / 2, head_top, cx + head_width / 2, head_bottom,
            head_width, head_width);

  // The holder: a U around the lower half of the head.
  SelectObject(dc, GetStockObject(NULL_BRUSH));
  const int holder_left = cx - head_width;
  const int holder_right = cx + head_width;
  const int holder_top = head_top + head_height / 3;
  const int holder_bottom = head_bottom + head_width / 2;
  Arc(dc, holder_left, holder_top, holder_right, holder_bottom, 0, 180, 0, 180);

  // The stem and the base.
  const int stem_top = holder_bottom - stroke / 2;
  const int stem_bottom = bounds.bottom - height / 6;
  MoveToEx(dc, cx, stem_top, nullptr);
  LineTo(dc, cx, stem_bottom);
  MoveToEx(dc, cx - head_width / 2, stem_bottom, nullptr);
  LineTo(dc, cx + head_width / 2, stem_bottom);

  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(brush);
  DeleteObject(pen);
}

COLORREF mic_colour(MicColour state) {
  switch (state) {
    case MicColour::Idle: return RGB(198, 200, 210);
    case MicColour::Active: return RGB(58, 200, 110);
    case MicColour::Working: return RGB(255, 176, 46);
    case MicColour::Attention: return RGB(235, 90, 80);
  }
  return RGB(198, 200, 210);
}

}  // namespace win32
}  // namespace pt
