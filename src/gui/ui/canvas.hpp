#pragma once

#include <string>

#include "gui/ui/geometry.hpp"

// The drawing surface, as the shared view code sees it. GDI and Core Graphics
// each implement this; nothing above it knows which one is running.
//
// The interface is deliberately small. It is not a general 2D API -- it is
// exactly the set of operations this one screen needs, which is why it fits in
// a dozen calls and why porting it to a second platform is a day's work rather
// than a project.
namespace seicure::gui {

// Four authored sizes rather than free-form point sizes: the design uses these
// and nothing else, and naming them keeps the two backends honest about
// producing the same thing.
enum class FontRole { Title, Body, Bold, Small };

enum class Align { Left, Center, Right };

class Canvas {
public:
    virtual ~Canvas() = default;

    // --- fills and strokes --------------------------------------------------
    virtual void fill_rect(const Rect& r, Color color) = 0;
    virtual void fill_round(const Rect& r, int radius, Color color) = 0;
    virtual void stroke_round(const Rect& r, int radius, Color color,
                              int width) = 0;
    // The empty drop zone. Dashes read as "nothing here yet" in a way a solid
    // outline does not.
    virtual void stroke_dashed_round(const Rect& r, int radius,
                                     Color color) = 0;
    virtual void draw_line(Point a, Point b, Color color, int width) = 0;

    // --- text ---------------------------------------------------------------
    // Single line, vertically centred in `r`, horizontally per `align`. The one
    // multi-line case in the design goes through draw_paragraph instead, so the
    // common path stays simple.
    virtual void draw_text(const std::wstring& text, const Rect& r,
                           FontRole font, Color color, Align align) = 0;
    virtual void draw_paragraph(const std::wstring& text, const Rect& r,
                                FontRole font, Color color) = 0;
    [[nodiscard]] virtual Size measure_text(const std::wstring& text,
                                            FontRole font) = 0;

    // --- the app mark -------------------------------------------------------
    // Drawn from the platform's own icon resource, so the caption and the
    // taskbar/Dock always show the same artwork.
    virtual void draw_app_icon(const Rect& r) = 0;

    // --- scaling ------------------------------------------------------------
    // Every metric in theme.hpp is authored at 96 dpi and passed through here.
    [[nodiscard]] virtual int dpi() const = 0;
    [[nodiscard]] int scale(int value) const { return scaled(value, dpi()); }

    // Shortens `text` with a trailing ellipsis until it fits `width`. Lives
    // here because it needs measurement, and both backends want it identically.
    [[nodiscard]] std::wstring ellipsize(const std::wstring& text,
                                         FontRole font, int width);
};

}  // namespace seicure::gui
