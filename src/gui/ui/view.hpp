#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "gui/ui/canvas.hpp"
#include "gui/ui/geometry.hpp"
#include "job/job.hpp"

// The screen itself: what is on it, where each piece goes, how it is drawn and
// what is under the cursor. No windowing, no threads, no file dialogs -- those
// belong to the host, which owns a View and drives it.
//
// Splitting it out this way is what lets Windows and macOS share the design
// rather than maintain two drifting copies of it.
namespace seicure::gui {

// What the launcher wants done. The shell verbs and the Finder both pass this
// so a right-click on "Decrypt with Seicure" arrives already pointed at
// decryption instead of dropping the user in front of two equally-lit buttons.
enum class PendingAction { None, Encrypt, Decrypt };

// Every clickable region, resolved once per layout and shared by painting and
// hit-testing so the two can never disagree.
enum class Hot { None, Panel, ModeIndividual, ModeArchive, Encrypt, Decrypt,
                 Clear, Cancel, Minimize, Maximize, Close };

struct LogLine {
    job::Event   kind;
    std::wstring text;
};

// The size is captured when the file is added rather than looked up while
// painting: the old code stat'ed every listed file on every repaint, which is
// a syscall per file per hover.
struct FileEntry {
    std::wstring path;
    std::wstring name;
    std::uint64_t size = 0;
};

struct Regions {
    Rect content;  // left/right bounds of the centred body column
    Rect caption;
    Rect minimize;
    Rect maximize;
    Rect close;
    Rect panel;
    Rect password;
    Rect mode_individual;
    Rect mode_archive;
    Rect encrypt;
    Rect decrypt;
    Rect clear;
    Rect log;
    Rect status;
};

// How the host wants the caption strip treated. Windows draws its own minimise,
// maximise and close buttons on the right; macOS puts system traffic lights on
// the left and wants the title pushed clear of them instead.
struct CaptionStyle {
    bool draw_window_buttons = true;
    int  inset_left  = 0;   // extra space before the app mark, at 96 dpi
};

class View {
public:
    // --- geometry -----------------------------------------------------------
    void set_client(const Rect& client, int dpi);
    void set_caption_style(const CaptionStyle& style) { caption_ = style; }
    void compute_layout();
    [[nodiscard]] const Regions& regions() const noexcept { return regions_; }

    // --- painting -----------------------------------------------------------
    void paint(Canvas& canvas) const;

    // --- hit testing --------------------------------------------------------
    [[nodiscard]] Hot hit_test(Point p) const;
    // True where the host should let the window be dragged: the caption strip,
    // minus anything clickable sitting in it.
    [[nodiscard]] bool is_caption_drag(Point p) const;

    // --- content ------------------------------------------------------------
    void add_files(const std::vector<std::wstring>& paths);
    void clear_files();
    [[nodiscard]] const std::vector<FileEntry>& files() const noexcept {
        return files_;
    }
    [[nodiscard]] std::vector<std::wstring> file_paths() const;

    void append_log(job::Event kind, std::wstring text);
    void clear_log() { log_.clear(); }

    // --- state the host owns and the view only displays ----------------------
    void set_hot(Hot hot) { hot_ = hot; }
    [[nodiscard]] Hot hot() const noexcept { return hot_; }
    void set_archive_mode(bool on) { archive_mode_ = on; }
    [[nodiscard]] bool archive_mode() const noexcept { return archive_mode_; }
    void set_busy(bool on) { busy_ = on; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    void set_maximized(bool on) { maximized_ = on; }
    void set_password_focused(bool on) { password_focused_ = on; }
    void set_caret_on(bool on) { caret_on_ = on; }
    [[nodiscard]] bool caret_on() const noexcept { return caret_on_; }
    void set_status(std::wstring text) { status_ = std::move(text); }
    // The label is set once when the job starts; the percentage then arrives
    // once per chunk and must not disturb it.
    void set_progress(int percent, std::wstring label);
    void set_progress_percent(int percent) { progress_ = percent; }
    void clear_progress() { progress_ = -1; }
    [[nodiscard]] int progress() const noexcept { return progress_; }

    [[nodiscard]] int dpi() const noexcept { return dpi_; }
    [[nodiscard]] int scale(int value) const { return scaled(value, dpi_); }

private:
    void paint_caption(Canvas& canvas) const;
    void paint_header(Canvas& canvas) const;
    void paint_panel(Canvas& canvas) const;
    void paint_password(Canvas& canvas) const;
    void paint_modes(Canvas& canvas) const;
    void paint_buttons(Canvas& canvas) const;
    void paint_log(Canvas& canvas) const;
    void paint_status(Canvas& canvas) const;

    Rect         client_;
    int          dpi_ = 96;
    CaptionStyle caption_;
    Regions      regions_;

    std::vector<FileEntry> files_;
    std::deque<LogLine>    log_;

    bool         archive_mode_     = false;
    bool         busy_             = false;
    bool         maximized_        = false;
    bool         password_focused_ = false;
    bool         caret_on_         = true;
    Hot          hot_              = Hot::None;
    std::wstring status_           = L"ready";
    std::wstring progress_label_;
    int          progress_ = -1;   // -1: idle
};

// The three window-button glyphs, drawn with strokes rather than a symbol font
// because Segoe MDL2 is not guaranteed and these are four straight lines.
enum class WindowGlyph { Minimize, Maximize, Restore, Close };
void draw_window_glyph(Canvas& canvas, const Rect& box, WindowGlyph glyph,
                       Color color, int size);

}  // namespace seicure::gui
