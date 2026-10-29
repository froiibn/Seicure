#pragma once

#include "gui/ui/canvas.hpp"
#include "gui/ui/geometry.hpp"

// The visual language, with no platform types in it. Both front ends compile
// against this file, so the palette and the metrics cannot drift apart between
// Windows and macOS.
namespace seicure::gui::theme {

// --- colour -----------------------------------------------------------------
// Phosphor terminal: near-black carrying a green cast rather than neutral grey,
// so the whole surface reads like a CRT rather than a dark-mode app. One bright
// green does all the signalling; the greys are green-tinted at every step.
//
// Errors are the single deliberate break from the green family -- an alarm has
// to be unmistakable, and a "slightly different green" is not.
inline constexpr Color BG          = rgb(0x00, 0x00, 0x00);
inline constexpr Color SURFACE     = rgb(0x06, 0x0E, 0x07);
inline constexpr Color SURFACE_HI  = rgb(0x0D, 0x1A, 0x0F);
inline constexpr Color BORDER      = rgb(0x17, 0x2C, 0x1B);
inline constexpr Color BORDER_HI   = rgb(0x2A, 0x4A, 0x30);
// Faint horizontal rules over the empty ground, the way a CRT shows its raster.
// Only on the background: laying them over text would cost a third of its
// contrast for the sake of an effect.
inline constexpr Color SCANLINE    = rgb(0x06, 0x0C, 0x07);

inline constexpr Color TEXT        = rgb(0xB8, 0xF0, 0xBE);
inline constexpr Color TEXT_MUTED  = rgb(0x6A, 0x9A, 0x72);
inline constexpr Color TEXT_FAINT  = rgb(0x42, 0x6B, 0x48);

inline constexpr Color ACCENT      = rgb(0x33, 0xFF, 0x6B);
inline constexpr Color ACCENT_HI   = rgb(0x70, 0xFF, 0x9B);
inline constexpr Color ACCENT_DIM  = rgb(0x11, 0x3D, 0x1E);

inline constexpr Color OK          = rgb(0x4C, 0xE8, 0x6E);
inline constexpr Color ERR         = rgb(0xFF, 0x5A, 0x4A);
inline constexpr Color CLOSE_HOVER = rgb(0xA3, 0x2A, 0x20);

// --- metrics (logical pixels at 96 dpi) -------------------------------------
inline constexpr int WINDOW_W   = 600;
inline constexpr int WINDOW_H   = 640;
// Body content stops widening past this and centres instead. Maximized on a
// wide monitor, a full-width password field and drop zone look broken.
inline constexpr int MAX_CONTENT_W = 620;
inline constexpr int PAD        = 24;
inline constexpr int GAP        = 14;
// Barely rounded. A 6px radius reads as a modern app card; hard corners read as
// a terminal.
inline constexpr int RADIUS     = 2;
inline constexpr int SCAN_STEP  = 3;   // one faint line every N pixels
inline constexpr int ROW_H      = 22;
inline constexpr int DROP_H     = 84;
inline constexpr int FIELD_H    = 36;
inline constexpr int BUTTON_H   = 38;
inline constexpr int LOG_LINES  = 7;
inline constexpr int MAX_LISTED = 6;

// Custom frame: the caption strip lives inside the client area so the app
// header *is* the title bar. RESIZE_BAND is the grab margin along the edges.
//
// macOS keeps its own traffic lights in that strip rather than drawing the
// three buttons we draw on Windows, so WINBTN_W is unused there -- the height
// and the drag behaviour are what carry across.
inline constexpr int CAPTION_H   = 46;
inline constexpr int WINBTN_W    = 46;
inline constexpr int RESIZE_BAND = 6;

// Minimum window size, below which the column layout stops making sense.
inline constexpr int MIN_W = 480;
inline constexpr int MIN_H = 600;

// --- shared drawing ---------------------------------------------------------
// Implemented once on top of Canvas rather than per backend, because it is
// nothing but a stack of one-pixel fills.
void draw_scanlines(Canvas& canvas, const Rect& r, int step, Color color);

}  // namespace seicure::gui::theme
