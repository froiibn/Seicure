#include "gui/mac/canvas_cg.hpp"

#import <AppKit/AppKit.h>

#include <vector>

namespace seicure::gui::mac {
namespace {

// Windows authors its fonts in points against 96 dpi, where CreateFont turns
// N points into N * 96/72 pixels. AppKit's points are already those pixels, so
// the same design size has to be multiplied by 4/3 here to come out the same
// physical size on screen.
constexpr double kPointScale = 96.0 / 72.0;

CGRect to_cgrect(const Rect& r) {
    return CGRectMake(r.left, r.top, r.width(), r.height());
}

void set_fill(CGContextRef ctx, Color c) {
    CGContextSetRGBFillColor(ctx, c.r / 255.0, c.g / 255.0, c.b / 255.0, 1.0);
}

void set_stroke(CGContextRef ctx, Color c) {
    CGContextSetRGBStrokeColor(ctx, c.r / 255.0, c.g / 255.0, c.b / 255.0, 1.0);
}

// A stroke sits centred on the path, so stroking the exact rectangle would
// straddle the boundary and land on two half-covered pixel columns. Insetting
// by half the width puts it back inside the shape, which is where GDI's
// RoundRect draws it.
CGRect stroke_rect(const Rect& r, int width) {
    return CGRectInset(to_cgrect(r), width * 0.5, width * 0.5);
}

CGPathRef make_round_path(CGRect box, int radius) {
    if (radius <= 0) {
        return CGPathCreateWithRect(box, nullptr);
    }
    return CGPathCreateWithRoundedRect(box, radius, radius, nullptr);
}

// std::wstring is UTF-32 on this platform; CFString wants UTF-16 or a known
// wide encoding. kCFStringEncodingUTF32LE is exact and avoids a manual
// surrogate conversion.
CFStringRef copy_cfstring(const std::wstring& text) {
    return CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
        static_cast<CFIndex>(text.size() * sizeof(wchar_t)),
        kCFStringEncodingUTF32LE, false);
}

// One laid-out line, ready to measure or draw. Callers must release it.
CTLineRef make_line(const std::wstring& text, CTFontRef font) {
    CFStringRef string = copy_cfstring(text);
    if (string == nullptr) {
        return nullptr;
    }
    // Without kCTForegroundColorFromContextAttributeName, Core Text ignores the
    // context's fill colour and draws in black -- which on this palette means
    // black text on a black ground, invisible everywhere except the one button
    // whose text is meant to be black anyway.
    CFStringRef keys[] = {kCTFontAttributeName,
                          kCTForegroundColorFromContextAttributeName};
    CFTypeRef   values[] = {font, kCFBooleanTrue};
    CFDictionaryRef attributes = CFDictionaryCreate(
        kCFAllocatorDefault, reinterpret_cast<const void**>(keys),
        static_cast<const void**>(values), 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFAttributedStringRef attributed =
        CFAttributedStringCreate(kCFAllocatorDefault, string, attributes);
    CTLineRef line = CTLineCreateWithAttributedString(attributed);

    CFRelease(attributed);
    CFRelease(attributes);
    CFRelease(string);
    return line;
}

std::vector<std::wstring> split_lines(const std::wstring& text) {
    std::vector<std::wstring> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t at = text.find(L'\n', start);
        if (at == std::wstring::npos) {
            out.push_back(text.substr(start));
            break;
        }
        out.push_back(text.substr(start, at - start));
        start = at + 1;
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

void Fonts::create() {
    destroy();
    // monospacedSystemFontOfSize gives SF Mono, which ships with every macOS
    // this build targets. Asking by family name would risk a miss and a
    // proportional fallback, which this layout cannot absorb.
    auto make = [](double points, NSFontWeight weight) -> CTFontRef {
        NSFont* font = [NSFont monospacedSystemFontOfSize:points * kPointScale
                                                   weight:weight];
        return static_cast<CTFontRef>(CFRetain(static_cast<const void*>(font)));
    };
    title = make(13, NSFontWeightSemibold);
    body  = make(10, NSFontWeightRegular);
    bold  = make(10, NSFontWeightSemibold);
    small = make(9,  NSFontWeightRegular);
}

void Fonts::destroy() {
    for (CTFontRef* font : {&title, &body, &bold, &small}) {
        if (*font != nullptr) {
            CFRelease(*font);
            *font = nullptr;
        }
    }
}

CTFontRef Fonts::get(FontRole role) const {
    switch (role) {
    case FontRole::Title: return title;
    case FontRole::Bold:  return bold;
    case FontRole::Small: return small;
    case FontRole::Body:  break;
    }
    return body;
}

// ---------------------------------------------------------------------------
// Fills and strokes
// ---------------------------------------------------------------------------

void CgCanvas::fill_rect(const Rect& r, Color color) {
    set_fill(ctx_, color);
    CGContextFillRect(ctx_, to_cgrect(r));
}

void CgCanvas::fill_round(const Rect& r, int radius, Color color) {
    CGPathRef path = make_round_path(to_cgrect(r), radius);
    set_fill(ctx_, color);
    CGContextAddPath(ctx_, path);
    CGContextFillPath(ctx_);
    CGPathRelease(path);
}

void CgCanvas::stroke_round(const Rect& r, int radius, Color color, int width) {
    CGPathRef path = make_round_path(stroke_rect(r, width), radius);
    set_stroke(ctx_, color);
    CGContextSetLineWidth(ctx_, width);
    CGContextAddPath(ctx_, path);
    CGContextStrokePath(ctx_);
    CGPathRelease(path);
}

void CgCanvas::stroke_dashed_round(const Rect& r, int radius, Color color) {
    // Matches the hairline dash GDI's PS_DASH produces at width 1.
    const CGFloat pattern[] = {3.0, 3.0};
    CGContextSaveGState(ctx_);
    CGContextSetLineDash(ctx_, 0.0, pattern, 2);
    stroke_round(r, radius, color, 1);
    CGContextRestoreGState(ctx_);
}

void CgCanvas::draw_line(Point a, Point b, Color color, int width) {
    set_stroke(ctx_, color);
    CGContextSetLineWidth(ctx_, width);
    // Half-pixel offset so an odd-width line lands on one row of pixels rather
    // than smearing across two.
    const CGFloat off = (width % 2 == 1) ? 0.5 : 0.0;
    CGContextBeginPath(ctx_);
    CGContextMoveToPoint(ctx_, a.x + off, a.y + off);
    CGContextAddLineToPoint(ctx_, b.x + off, b.y + off);
    CGContextStrokePath(ctx_);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void CgCanvas::draw_text(const std::wstring& text, const Rect& r,
                         FontRole font, Color color, Align align) {
    if (text.empty()) {
        return;
    }
    CTFontRef ct = fonts_.get(font);
    CTLineRef line = make_line(text, ct);
    if (line == nullptr) {
        return;
    }

    CGFloat ascent = 0;
    CGFloat descent = 0;
    CGFloat leading = 0;
    const double width =
        CTLineGetTypographicBounds(line, &ascent, &descent, &leading);

    double x = r.left;
    if (align == Align::Center) {
        x = r.left + (r.width() - width) / 2.0;
    } else if (align == Align::Right) {
        x = r.right - width;
    }
    // Vertically centred on the ink, matching DT_VCENTER.
    const double y = r.mid_y() + (ascent - descent) / 2.0;

    CGContextSaveGState(ctx_);
    set_fill(ctx_, color);
    // The view is flipped, so the text matrix has to flip back or every glyph
    // comes out mirrored.
    CGContextSetTextMatrix(ctx_, CGAffineTransformMakeScale(1.0, -1.0));
    CGContextSetTextPosition(ctx_, x, y);
    CTLineDraw(line, ctx_);
    CGContextRestoreGState(ctx_);

    CFRelease(line);
}

void CgCanvas::draw_paragraph(const std::wstring& text, const Rect& r,
                              FontRole font, Color color) {
    CTFontRef ct = fonts_.get(font);
    const double line_height = CTFontGetAscent(ct) + CTFontGetDescent(ct) +
                               CTFontGetLeading(ct);

    int y = r.top;
    for (const auto& piece : split_lines(text)) {
        const int bottom = y + static_cast<int>(line_height + 0.5);
        if (bottom > r.bottom) {
            break;
        }
        draw_text(piece, Rect{r.left, y, r.right, bottom}, font, color,
                  Align::Left);
        y = bottom;
    }
}

Size CgCanvas::measure_text(const std::wstring& text, FontRole font) {
    if (text.empty()) {
        return Size{0, 0};
    }
    CTLineRef line = make_line(text, fonts_.get(font));
    if (line == nullptr) {
        return Size{0, 0};
    }
    CGFloat ascent = 0;
    CGFloat descent = 0;
    CGFloat leading = 0;
    const double width =
        CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    CFRelease(line);
    return Size{static_cast<int>(width + 0.5),
                static_cast<int>(ascent + descent + 0.5)};
}

void CgCanvas::draw_app_icon(const Rect& r) {
    if (icon_ == nullptr) {
        return;
    }
    CGContextSaveGState(ctx_);
    // CGImage draws bottom-up; in a flipped context that would land upside
    // down, so flip back over the destination rectangle.
    CGContextTranslateCTM(ctx_, 0, r.top + r.bottom);
    CGContextScaleCTM(ctx_, 1, -1);
    CGContextDrawImage(ctx_, to_cgrect(r), icon_);
    CGContextRestoreGState(ctx_);
}

}  // namespace seicure::gui::mac
