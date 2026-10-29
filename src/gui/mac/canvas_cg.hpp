#pragma once

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include "gui/ui/canvas.hpp"

namespace seicure::gui::mac {

// The four authored sizes as Core Text fonts. Created once and kept for the
// life of the window: unlike Windows there is no dpi change to react to,
// because AppKit measures in points and the backing store handles Retina.
struct Fonts {
    CTFontRef title = nullptr;
    CTFontRef body  = nullptr;
    CTFontRef bold  = nullptr;
    CTFontRef small = nullptr;

    void create();
    void destroy();

    [[nodiscard]] CTFontRef get(FontRole role) const;
};

// Canvas over a Core Graphics context. Owns nothing; the fonts and the icon
// belong to the view, which outlives any one draw.
//
// The context is expected to be the one AppKit hands to drawRect: in a flipped
// view, so y grows downward exactly as it does on Windows and the shared layout
// needs no coordinate juggling.
class CgCanvas final : public Canvas {
public:
    CgCanvas(CGContextRef ctx, const Fonts& fonts, CGImageRef icon)
        : ctx_(ctx), fonts_(fonts), icon_(icon) {}

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

    // AppKit lays out in points, and one point matches one logical pixel at
    // Windows' 96 dpi. Retina is handled below us by the backing scale factor,
    // so the shared metrics are used unscaled and this is a constant.
    [[nodiscard]] int dpi() const override { return 96; }

private:
    CGContextRef ctx_;
    const Fonts& fonts_;
    CGImageRef   icon_;
};

}  // namespace seicure::gui::mac
