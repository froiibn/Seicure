#include "gui/win32/canvas_gdi.hpp"

namespace seicure::gui::win32 {
namespace {

// GDI wants 0x00BBGGRR; the shared palette is plain RGB. This is the only place
// the two meet.
COLORREF to_colorref(Color c) {
    return RGB(c.r, c.g, c.b);
}

RECT to_rect(const Rect& r) {
    return RECT{r.left, r.top, r.right, r.bottom};
}

// Cascadia ships with Windows 11 and modern terminals; Consolas is the
// universal fallback. Both are monospace, which the whole layout assumes.
HFONT make_font(int dpi, int point_size, int weight) {
    const int height = -MulDiv(point_size, dpi, 72);
    for (const wchar_t* face : {L"Cascadia Mono", L"Consolas", L"Courier New"}) {
        HFONT font = CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, face);
        if (font != nullptr) {
            return font;
        }
    }
    return nullptr;
}

UINT align_flag(Align align) {
    switch (align) {
    case Align::Center: return DT_CENTER;
    case Align::Right:  return DT_RIGHT;
    case Align::Left:   break;
    }
    return DT_LEFT;
}

}  // namespace

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

void Fonts::create(int dpi) {
    destroy();
    title = make_font(dpi, 13, FW_SEMIBOLD);
    body  = make_font(dpi, 10, FW_NORMAL);
    bold  = make_font(dpi, 10, FW_SEMIBOLD);
    small = make_font(dpi, 9, FW_NORMAL);
}

void Fonts::destroy() {
    for (HFONT* font : {&title, &body, &bold, &small}) {
        if (*font != nullptr) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
}

HFONT Fonts::get(FontRole role) const {
    switch (role) {
    case FontRole::Title: return title;
    case FontRole::Bold:  return bold;
    case FontRole::Small: return small;
    case FontRole::Body:  break;
    }
    return body;
}

// ---------------------------------------------------------------------------
// GdiCanvas
// ---------------------------------------------------------------------------

void GdiCanvas::fill_rect(const Rect& r, Color color) {
    HBRUSH brush = CreateSolidBrush(to_colorref(color));
    RECT box = to_rect(r);
    FillRect(dc_, &box, brush);
    DeleteObject(brush);
}

void GdiCanvas::fill_round(const Rect& r, int radius, Color color) {
    const COLORREF c = to_colorref(color);
    HBRUSH brush = CreateSolidBrush(c);
    HPEN   pen   = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ old_brush = SelectObject(dc_, brush);
    HGDIOBJ old_pen   = SelectObject(dc_, pen);
    RoundRect(dc_, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(dc_, old_brush);
    SelectObject(dc_, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void GdiCanvas::stroke_round(const Rect& r, int radius, Color color, int width) {
    HPEN    pen       = CreatePen(PS_SOLID, width, to_colorref(color));
    HGDIOBJ old_pen   = SelectObject(dc_, pen);
    HGDIOBJ old_brush = SelectObject(dc_, GetStockObject(NULL_BRUSH));
    RoundRect(dc_, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(dc_, old_pen);
    SelectObject(dc_, old_brush);
    DeleteObject(pen);
}

void GdiCanvas::stroke_dashed_round(const Rect& r, int radius, Color color) {
    // PS_DASH only honours the pattern at width 1, which is what we want for
    // the drop zone's hairline outline.
    LOGBRUSH lb{BS_SOLID, to_colorref(color), 0};
    HPEN pen = ExtCreatePen(PS_GEOMETRIC | PS_DASH | PS_ENDCAP_FLAT, 1, &lb, 0,
                            nullptr);
    if (pen == nullptr) {
        stroke_round(r, radius, color, 1);
        return;
    }
    HGDIOBJ old_pen   = SelectObject(dc_, pen);
    HGDIOBJ old_brush = SelectObject(dc_, GetStockObject(NULL_BRUSH));
    RoundRect(dc_, r.left, r.top, r.right, r.bottom, radius * 2, radius * 2);
    SelectObject(dc_, old_pen);
    SelectObject(dc_, old_brush);
    DeleteObject(pen);
}

void GdiCanvas::draw_line(Point a, Point b, Color color, int width) {
    HPEN    pen     = CreatePen(PS_SOLID, width, to_colorref(color));
    HGDIOBJ old_pen = SelectObject(dc_, pen);
    MoveToEx(dc_, a.x, a.y, nullptr);
    LineTo(dc_, b.x, b.y);
    SelectObject(dc_, old_pen);
    DeleteObject(pen);
}

void GdiCanvas::draw_text(const std::wstring& text, const Rect& r,
                          FontRole font, Color color, Align align) {
    HGDIOBJ old_font = SelectObject(dc_, fonts_.get(font));
    SetTextColor(dc_, to_colorref(color));
    SetBkMode(dc_, TRANSPARENT);
    RECT box = to_rect(r);
    DrawTextW(dc_, text.c_str(), static_cast<int>(text.size()), &box,
              DT_SINGLELINE | DT_VCENTER | align_flag(align));
    SelectObject(dc_, old_font);
}

void GdiCanvas::draw_paragraph(const std::wstring& text, const Rect& r,
                               FontRole font, Color color) {
    HGDIOBJ old_font = SelectObject(dc_, fonts_.get(font));
    SetTextColor(dc_, to_colorref(color));
    SetBkMode(dc_, TRANSPARENT);
    RECT box = to_rect(r);
    DrawTextW(dc_, text.c_str(), static_cast<int>(text.size()), &box,
              DT_LEFT | DT_TOP);
    SelectObject(dc_, old_font);
}

Size GdiCanvas::measure_text(const std::wstring& text, FontRole font) {
    HGDIOBJ old_font = SelectObject(dc_, fonts_.get(font));
    SIZE size{0, 0};
    GetTextExtentPoint32W(dc_, text.c_str(), static_cast<int>(text.size()),
                          &size);
    SelectObject(dc_, old_font);
    return Size{size.cx, size.cy};
}

void GdiCanvas::draw_app_icon(const Rect& r) {
    if (icon_ == nullptr) {
        return;
    }
    DrawIconEx(dc_, r.left, r.top, icon_, r.width(), r.height(), 0, nullptr,
               DI_NORMAL);
}

}  // namespace seicure::gui::win32
