#pragma once

#include <algorithm>
#include <cstdint>

// Plain geometry and colour, with no platform types in sight. Everything above
// the drawing backend is written against these so that the layout and the
// painting can be compiled once and used by both the Win32 and the Cocoa front
// ends.
namespace seicure::gui {

struct Point {
    int x = 0;
    int y = 0;
};

struct Size {
    int w = 0;
    int h = 0;
};

struct Rect {
    int left   = 0;
    int top    = 0;
    int right  = 0;
    int bottom = 0;

    [[nodiscard]] constexpr int width() const noexcept { return right - left; }
    [[nodiscard]] constexpr int height() const noexcept { return bottom - top; }
    [[nodiscard]] constexpr int mid_x() const noexcept {
        return (left + right) / 2;
    }
    [[nodiscard]] constexpr int mid_y() const noexcept {
        return (top + bottom) / 2;
    }
    [[nodiscard]] constexpr bool empty() const noexcept {
        return right <= left || bottom <= top;
    }

    [[nodiscard]] constexpr bool contains(Point p) const noexcept {
        return p.x >= left && p.x < right && p.y >= top && p.y < bottom;
    }

    [[nodiscard]] constexpr Rect inset(int dx, int dy) const noexcept {
        return Rect{left + dx, top + dy, right - dx, bottom - dy};
    }

    // A horizontal strip of the given height starting at `y`. Most of the
    // layout is written as a column of these.
    [[nodiscard]] constexpr Rect strip(int y, int h) const noexcept {
        return Rect{left, y, right, y + h};
    }
};

// Straight 8-bit RGB. Deliberately not COLORREF: that is 0x00BBGGRR, and a
// channel-order mistake in a palette this monochrome would be easy to miss.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    [[nodiscard]] friend constexpr bool operator==(Color a, Color b) noexcept {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    }
};

constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    return Color{r, g, b};
}

// Scales a metric authored at 96 dpi to the display's actual dpi. macOS works
// in points and hands us a backing-scale factor instead, which the Cocoa canvas
// converts to the same 96-based number so this one rule covers both.
constexpr int scaled(int value, int dpi) noexcept {
    return static_cast<int>((static_cast<std::int64_t>(value) * dpi + 48) / 96);
}

}  // namespace seicure::gui
