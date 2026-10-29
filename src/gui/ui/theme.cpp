#include "gui/ui/theme.hpp"

namespace seicure::gui {

std::wstring Canvas::ellipsize(const std::wstring& text, FontRole font,
                               int width) {
    if (measure_text(text, font).w <= width) {
        return text;
    }
    // Trim from the end rather than the middle: these are file names, and the
    // extension is worth less than the start of the name when only one can fit.
    std::wstring out = text;
    while (out.size() > 4 && measure_text(out + L"...", font).w > width) {
        out.pop_back();
    }
    return out + L"...";
}

namespace theme {

void draw_scanlines(Canvas& canvas, const Rect& r, int step, Color color) {
    if (step < 2) {
        return;
    }
    for (int y = r.top; y < r.bottom; y += step) {
        canvas.fill_rect(Rect{r.left, y, r.right, y + 1}, color);
    }
}

}  // namespace theme
}  // namespace seicure::gui
