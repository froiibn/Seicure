#pragma once

#include <windows.h>

#include "gui/ui/canvas.hpp"

namespace seicure::gui::win32 {

// The four authored sizes, recreated whenever the dpi changes.
struct Fonts {
    HFONT title = nullptr;
    HFONT body  = nullptr;
    HFONT bold  = nullptr;
    HFONT small = nullptr;

    void create(int dpi);
    void destroy();

    [[nodiscard]] HFONT get(FontRole role) const;
};

// Canvas over a GDI device context. Owns nothing: the fonts and the icon belong
// to the window, which outlives any one paint.
class GdiCanvas final : public Canvas {
public:
    GdiCanvas(HDC dc, const Fonts& fonts, HICON icon, int dpi)
        : dc_(dc), fonts_(fonts), icon_(icon), dpi_(dpi) {}

    void fill_rect(const Rect& r, Color color) override;
    void fill_round(const Rect& r, int radius, Color color) override;
    void stroke_round(const Rect& r, int radius, Color color,
                      int width) override;
    void stroke_dashed_round(const Rect& r, int radius, Color color) override;
    void draw_line(Point a, Point b, Color color, int width) override;

    void draw_text(const std::wstring& text, const Rect& r, FontRole font,
                   Color color, Align align) override;
    void draw_paragraph(const std::wstring& text, const Rect& r, FontRole font,
                        Color color) override;
    [[nodiscard]] Size measure_text(const std::wstring& text,
                                    FontRole font) override;

    void draw_app_icon(const Rect& r) override;

    [[nodiscard]] int dpi() const override { return dpi_; }

private:
    HDC          dc_;
    const Fonts& fonts_;
    HICON        icon_;
    int          dpi_;
};

}  // namespace seicure::gui::win32
