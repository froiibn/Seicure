#include "gui/ui/view.hpp"

#include <algorithm>
#include <cwctype>

#include "gui/ui/theme.hpp"
#include "platform/file.hpp"

namespace seicure::gui {
namespace {

std::wstring upper(std::wstring text) {
    for (wchar_t& c : text) {
        c = static_cast<wchar_t>(std::towupper(c));
    }
    return text;
}

}  // namespace

void draw_window_glyph(Canvas& canvas, const Rect& box, WindowGlyph glyph,
                       Color color, int size) {
    const int cx = box.mid_x();
    const int cy = box.mid_y();
    const int h  = size / 2;
    const int w  = (std::max)(1, size / 10);

    switch (glyph) {
    case WindowGlyph::Minimize:
        canvas.draw_line(Point{cx - h, cy}, Point{cx + h, cy}, color, w);
        break;
    case WindowGlyph::Maximize:
        canvas.stroke_round(Rect{cx - h, cy - h, cx + h, cy + h}, 0, color, w);
        break;
    case WindowGlyph::Restore: {
        const int off = size / 4;
        canvas.stroke_round(Rect{cx - h, cy - h + off, cx + h - off, cy + h}, 0,
                            color, w);
        canvas.draw_line(Point{cx - h + off, cy - h + off},
                         Point{cx - h + off, cy - h}, color, w);
        canvas.draw_line(Point{cx - h + off, cy - h}, Point{cx + h, cy - h},
                         color, w);
        canvas.draw_line(Point{cx + h, cy - h}, Point{cx + h, cy + h - off},
                         color, w);
        break;
    }
    case WindowGlyph::Close:
        canvas.draw_line(Point{cx - h, cy - h}, Point{cx + h + 1, cy + h + 1},
                         color, w);
        canvas.draw_line(Point{cx + h, cy - h}, Point{cx - h - 1, cy + h + 1},
                         color, w);
        break;
    }
}

// ---------------------------------------------------------------------------
// Content
// ---------------------------------------------------------------------------

void View::add_files(const std::vector<std::wstring>& paths) {
    for (const auto& path : paths) {
        const bool already = std::any_of(
            files_.begin(), files_.end(),
            [&](const FileEntry& e) { return e.path == path; });
        if (already) {
            continue;
        }
        files_.push_back(FileEntry{path, platform::base_name(path),
                                   platform::file_size(path)});
    }
}

void View::clear_files() {
    files_.clear();
}

std::vector<std::wstring> View::file_paths() const {
    std::vector<std::wstring> out;
    out.reserve(files_.size());
    for (const auto& entry : files_) {
        out.push_back(entry.path);
    }
    return out;
}

void View::append_log(job::Event kind, std::wstring text) {
    log_.push_back(LogLine{kind, std::move(text)});
    while (log_.size() > static_cast<std::size_t>(theme::LOG_LINES)) {
        log_.pop_front();
    }
}

void View::set_progress(int percent, std::wstring label) {
    progress_ = percent;
    progress_label_ = std::move(label);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void View::set_client(const Rect& client, int dpi) {
    client_ = client;
    dpi_    = dpi;
}

void View::compute_layout() {
    const int pad = scale(theme::PAD);
    const int gap = scale(theme::GAP);

    // The body is a centred column with a maximum width; only the caption and
    // its buttons track the window edges.
    int left  = pad;
    int right = client_.right - pad;
    const int max_width = scale(theme::MAX_CONTENT_W);
    if (right - left > max_width) {
        const int centre = client_.right / 2;
        left  = centre - max_width / 2;
        right = centre + max_width / 2;
    }
    const int width = right - left;
    regions_.content = Rect{left, 0, right, client_.bottom};

    // Caption strip: our own title bar. On Windows the three window buttons sit
    // flush right and we draw them; on macOS the system traffic lights sit on
    // the left instead, so the buttons collapse to nothing and the app mark is
    // pushed clear of them by CaptionStyle::inset_left.
    const int caption_h = scale(theme::CAPTION_H);
    regions_.caption = Rect{0, 0, client_.right, caption_h};

    if (caption_.draw_window_buttons) {
        const int win_btn_w = scale(theme::WINBTN_W);
        regions_.close    = Rect{client_.right - win_btn_w, 0, client_.right,
                                 caption_h};
        regions_.maximize = Rect{client_.right - win_btn_w * 2, 0,
                                 client_.right - win_btn_w, caption_h};
        regions_.minimize = Rect{client_.right - win_btn_w * 3, 0,
                                 client_.right - win_btn_w * 2, caption_h};
    } else {
        regions_.close    = Rect{};
        regions_.maximize = Rect{};
        regions_.minimize = Rect{};
    }

    int y = caption_h + scale(44);  // subtitle and divider sit in between

    const int panel_h = scale(theme::DROP_H) + scale(theme::ROW_H) * 4;
    regions_.panel = Rect{left, y, right, y + panel_h};
    y += panel_h + gap + scale(20);

    const int field_h = scale(theme::FIELD_H);
    regions_.password = Rect{left, y, right, y + field_h};
    y += field_h + gap;

    const int mode_h = scale(26);
    const int mode_w = scale(120);
    regions_.mode_individual = Rect{left, y, left + mode_w, y + mode_h};
    regions_.mode_archive =
        Rect{left + mode_w + scale(8), y, left + mode_w * 2 + scale(8),
             y + mode_h};
    y += mode_h + gap;

    const int btn_h = scale(theme::BUTTON_H);
    const int btn_w = (width - scale(10) * 2) / 3;
    regions_.encrypt = Rect{left, y, left + btn_w, y + btn_h};
    regions_.decrypt = Rect{left + btn_w + scale(10), y,
                            left + btn_w * 2 + scale(10), y + btn_h};
    regions_.clear   = Rect{right - btn_w, y, right, y + btn_h};
    y += btn_h + gap;

    // Status pins to the bottom; the log fills whatever is left.
    const int status_h = scale(30);
    regions_.status = Rect{left, client_.bottom - pad - status_h, right,
                           client_.bottom - pad};
    regions_.log = Rect{left, y + scale(14), right, regions_.status.top - gap};
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

Hot View::hit_test(Point p) const {
    if (regions_.close.contains(p))            return Hot::Close;
    if (regions_.maximize.contains(p))         return Hot::Maximize;
    if (regions_.minimize.contains(p))         return Hot::Minimize;
    if (regions_.encrypt.contains(p))          return Hot::Encrypt;
    if (regions_.decrypt.contains(p))          return Hot::Decrypt;
    if (regions_.clear.contains(p))            return busy_ ? Hot::Cancel
                                                            : Hot::Clear;
    if (regions_.mode_individual.contains(p))  return Hot::ModeIndividual;
    if (regions_.mode_archive.contains(p))     return Hot::ModeArchive;
    if (regions_.panel.contains(p))            return Hot::Panel;
    return Hot::None;
}

bool View::is_caption_drag(Point p) const {
    if (!regions_.caption.contains(p)) {
        return false;
    }
    return !regions_.minimize.contains(p) && !regions_.maximize.contains(p) &&
           !regions_.close.contains(p);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void View::paint(Canvas& canvas) const {
    canvas.fill_rect(client_, theme::BG);
    // Raster texture on the bare ground only; panels paint over it.
    theme::draw_scanlines(canvas, client_, scale(theme::SCAN_STEP),
                          theme::SCANLINE);
    paint_caption(canvas);
    paint_header(canvas);
    paint_panel(canvas);
    paint_password(canvas);
    paint_modes(canvas);
    paint_buttons(canvas);
    paint_log(canvas);
    paint_status(canvas);
}

void View::paint_caption(Canvas& canvas) const {
    const Rect& caption = regions_.caption;
    const int left = scale(theme::PAD) + scale(caption_.inset_left);
    const int middle = caption.mid_y();

    // The app icon doubles as the caption mark, so the window and the taskbar
    // show the same thing.
    const int icon = scale(20);
    canvas.draw_app_icon(
        Rect{left, middle - icon / 2, left + icon, middle + icon / 2});

    const int title_right = caption_.draw_window_buttons
                                ? regions_.minimize.left
                                : regions_.content.right;
    canvas.draw_text(L"SEICURE",
                     Rect{left + scale(28), caption.top, title_right,
                          caption.bottom},
                     FontRole::Title, theme::TEXT, Align::Left);

    if (!caption_.draw_window_buttons) {
        return;
    }

    struct Button {
        const Rect& box;
        Hot         id;
        WindowGlyph glyph;
        bool        danger;
    };
    const Button buttons[] = {
        {regions_.minimize, Hot::Minimize, WindowGlyph::Minimize, false},
        {regions_.maximize, Hot::Maximize,
         maximized_ ? WindowGlyph::Restore : WindowGlyph::Maximize, false},
        {regions_.close, Hot::Close, WindowGlyph::Close, true},
    };

    for (const auto& button : buttons) {
        const bool hover = hot_ == button.id;
        if (hover) {
            canvas.fill_rect(button.box, button.danger ? theme::CLOSE_HOVER
                                                       : theme::SURFACE_HI);
        }
        draw_window_glyph(canvas, button.box, button.glyph,
                          hover ? theme::TEXT : theme::TEXT_MUTED, scale(10));
    }
}

void View::paint_header(Canvas& canvas) const {
    const int top   = scale(theme::CAPTION_H);
    const int left  = regions_.content.left;
    const int right = regions_.content.right;

    canvas.draw_text(L"file encryption with metadata hiding",
                     Rect{left, top + scale(2), right, top + scale(20)},
                     FontRole::Small, theme::TEXT_MUTED, Align::Left);
    canvas.fill_rect(Rect{left, top + scale(30), right, top + scale(31)},
                     theme::BORDER);
}

void View::paint_panel(Canvas& canvas) const {
    const Rect& r = regions_.panel;
    const int radius = scale(theme::RADIUS);

    if (files_.empty()) {
        const bool hover = hot_ == Hot::Panel;
        canvas.fill_round(r, radius, theme::BG);
        canvas.stroke_dashed_round(r, radius,
                                   hover ? theme::ACCENT_DIM : theme::BORDER);

        Rect line1 = r;
        line1.top += r.height() / 2 - scale(22);
        line1.bottom = line1.top + scale(22);
        canvas.draw_text(L"Drop files here", line1, FontRole::Body,
                         hover ? theme::TEXT : theme::TEXT_MUTED, Align::Center);

        Rect line2 = line1;
        line2.top += scale(22);
        line2.bottom += scale(22);
        canvas.draw_text(L"or click to browse", line2, FontRole::Small,
                         theme::TEXT_FAINT, Align::Center);
        return;
    }

    canvas.fill_round(r, radius, theme::SURFACE);
    canvas.stroke_round(r, radius, theme::BORDER, 1);

    const int px = scale(16);
    int y = r.top + scale(12);

    canvas.draw_text(std::to_wstring(files_.size()) +
                         (files_.size() == 1 ? L" file" : L" files"),
                     Rect{r.left + px, y, r.right - px, y + scale(18)},
                     FontRole::Bold, theme::ACCENT, Align::Left);
    y += scale(24);

    const int row = scale(theme::ROW_H);
    const std::size_t shown =
        (std::min)(files_.size(), static_cast<std::size_t>(theme::MAX_LISTED));

    for (std::size_t i = 0; i < shown; ++i) {
        if (y + row > r.bottom - scale(10)) break;

        const std::wstring size_text = job::format_size(files_[i].size);
        const int size_w = canvas.measure_text(size_text, FontRole::Small).w;

        const Rect name{r.left + px, y, r.right - px - size_w - scale(12),
                        y + row};
        canvas.draw_text(
            canvas.ellipsize(files_[i].name, FontRole::Body, name.width()),
            name, FontRole::Body, theme::TEXT, Align::Left);

        canvas.draw_text(size_text,
                         Rect{r.right - px - size_w, y, r.right - px, y + row},
                         FontRole::Small, theme::TEXT_FAINT, Align::Right);
        y += row;
    }

    if (files_.size() > shown) {
        canvas.draw_text(L"+ " + std::to_wstring(files_.size() - shown) +
                             L" more",
                         Rect{r.left + px, y, r.right - px, y + row},
                         FontRole::Small, theme::TEXT_FAINT, Align::Left);
    }
}

void View::paint_password(Canvas& canvas) const {
    const Rect& r = regions_.password;
    const int radius = scale(theme::RADIUS);

    canvas.draw_text(L"PASSWORD",
                     Rect{r.left, r.top - scale(20), r.right, r.top - scale(4)},
                     FontRole::Small, theme::TEXT_FAINT, Align::Left);

    canvas.fill_round(r, radius, theme::SURFACE);
    canvas.stroke_round(r, radius,
                        password_focused_ ? theme::ACCENT : theme::BORDER, 1);
}

void View::paint_modes(Canvas& canvas) const {
    struct Pill {
        const Rect&    box;
        const wchar_t* label;
        bool           selected;
        Hot            id;
    };
    const Pill pills[] = {
        {regions_.mode_individual, L"INDIVIDUAL", !archive_mode_,
         Hot::ModeIndividual},
        {regions_.mode_archive, L"ARCHIVE", archive_mode_, Hot::ModeArchive},
    };

    const int radius = scale(theme::RADIUS);
    for (const auto& pill : pills) {
        const bool hover = hot_ == pill.id;
        if (pill.selected) {
            canvas.fill_round(pill.box, radius, theme::ACCENT_DIM);
            canvas.stroke_round(pill.box, radius, theme::ACCENT, 1);
        } else {
            canvas.fill_round(pill.box, radius,
                              hover ? theme::SURFACE_HI : theme::BG);
            canvas.stroke_round(pill.box, radius,
                                hover ? theme::BORDER_HI : theme::BORDER, 1);
        }
        canvas.draw_text(pill.label, pill.box, FontRole::Small,
                         pill.selected ? theme::TEXT : theme::TEXT_MUTED,
                         Align::Center);
    }

    canvas.draw_text(archive_mode_ ? L"all inputs into one container"
                                   : L"one container per input",
                     Rect{regions_.mode_archive.right + scale(14),
                          regions_.mode_archive.top, regions_.log.right,
                          regions_.mode_archive.bottom},
                     FontRole::Small, theme::TEXT_FAINT, Align::Left);
}

void View::paint_buttons(Canvas& canvas) const {
    const int radius = scale(theme::RADIUS);
    const bool ready = !files_.empty() && !busy_;

    // Primary: filled accent.
    {
        const bool hover = hot_ == Hot::Encrypt && ready;
        const Color fill = !ready ? theme::SURFACE
                                  : (hover ? theme::ACCENT_HI : theme::ACCENT);
        canvas.fill_round(regions_.encrypt, radius, fill);
        // Knocked out of the accent fill, so it tracks the palette.
        canvas.draw_text(L"ENCRYPT", regions_.encrypt, FontRole::Bold,
                         ready ? theme::BG : theme::TEXT_FAINT, Align::Center);
    }

    // Secondary: outline.
    {
        const bool hover = hot_ == Hot::Decrypt && ready;
        canvas.fill_round(regions_.decrypt, radius,
                          hover ? theme::SURFACE_HI : theme::BG);
        canvas.stroke_round(regions_.decrypt, radius,
                            ready ? (hover ? theme::BORDER_HI : theme::BORDER)
                                  : theme::BORDER,
                            1);
        canvas.draw_text(L"DECRYPT", regions_.decrypt, FontRole::Bold,
                         ready ? theme::TEXT : theme::TEXT_FAINT, Align::Center);
    }

    // Tertiary: text only. Becomes Cancel while a job runs.
    {
        const bool hover = hot_ == Hot::Clear || hot_ == Hot::Cancel;
        const bool active = busy_ || !files_.empty();
        canvas.draw_text(busy_ ? L"CANCEL" : L"CLEAR", regions_.clear,
                         FontRole::Body,
                         !active ? theme::TEXT_FAINT
                                 : (hover ? theme::TEXT : theme::TEXT_MUTED),
                         Align::Center);
    }
}

void View::paint_log(Canvas& canvas) const {
    const Rect& r = regions_.log;
    const int row = scale(20);
    int y = r.top;

    if (log_.empty()) {
        canvas.draw_paragraph(
            L"Encrypted files are named with random hex. Names,\n"
            L"timestamps, attributes and size are hidden inside.",
            r, FontRole::Small, theme::TEXT_FAINT);
        return;
    }

    for (const auto& line : log_) {
        if (y + row > r.bottom) break;

        Color color = theme::TEXT_MUTED;
        const wchar_t* marker = L"[--]";
        if (line.kind == job::Event::Success) {
            color = theme::OK;
            marker = L"[OK]";
        } else if (line.kind == job::Event::Error) {
            color = theme::ERR;
            marker = L"[!!]";
        }

        canvas.draw_text(marker, Rect{r.left, y, r.left + scale(40), y + row},
                         FontRole::Small, color, Align::Left);

        const Rect text{r.left + scale(44), y, r.right, y + row};
        canvas.draw_text(
            canvas.ellipsize(line.text, FontRole::Small, text.width()), text,
            FontRole::Small,
            line.kind == job::Event::Error ? theme::ERR : theme::TEXT_MUTED,
            Align::Left);
        y += row;
    }
}

void View::paint_status(Canvas& canvas) const {
    const Rect& r = regions_.status;

    canvas.fill_rect(Rect{r.left, r.top, r.right, r.top + 1}, theme::BORDER);

    // A block gauge rather than a smooth bar: the progress is drawn out of
    // characters, the way a terminal would have done it.
    std::wstring line = L"> ";
    if (progress_ >= 0) {
        constexpr int kCells = 24;
        const int filled = (progress_ * kCells) / 100;
        line += upper(progress_label_);
        line += L" [";
        line.append(static_cast<std::size_t>(filled), L'█');
        line.append(static_cast<std::size_t>(kCells - filled), L'░');
        line += L"] " + std::to_wstring(progress_) + L"%";
    } else {
        line += upper(status_);
    }

    const Rect text{r.left, r.top + scale(8), r.right, r.bottom};
    canvas.draw_text(line, text, FontRole::Small,
                     progress_ >= 0 ? theme::ACCENT : theme::TEXT_MUTED,
                     Align::Left);

    // Block cursor, parked after the text and blinking on a timer.
    if (caret_on_ && progress_ < 0) {
        const int extent = canvas.measure_text(line + L" ", FontRole::Small).w;
        const int height = scale(13);
        const int middle = text.mid_y();
        canvas.fill_rect(Rect{text.left + extent, middle - height / 2,
                              text.left + extent + scale(8),
                              middle + height / 2},
                         theme::ACCENT);
    }
}

}  // namespace seicure::gui
