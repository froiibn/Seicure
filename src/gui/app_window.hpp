#pragma once

#include <windows.h>
#include <shellapi.h>  // HDROP

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "core/secure_buffer.hpp"
#include "gui/ui/view.hpp"
#include "gui/win32/canvas_gdi.hpp"
#include "job/job.hpp"

namespace seicure::gui {

// Posted from the worker thread back to the UI thread.
inline constexpr UINT WM_JOB_PROGRESS = WM_APP + 1;  // wParam: percent
inline constexpr UINT WM_JOB_LOG      = WM_APP + 2;  // lParam: LogLine*
inline constexpr UINT WM_JOB_DONE     = WM_APP + 3;  // lParam: Result*

// PendingAction lives in ui/view.hpp: the Finder needs the same idea.

// Window class name, also used to find a running instance.
inline constexpr const wchar_t* kWindowClass = L"SeicureMainWindow";

// Hands a file list to an already-running instance. Returns false when there
// is none. Explorer launches one process per selected file, so without this a
// ten-file selection opens ten windows.
bool forward_to_running_instance(const std::vector<std::wstring>& files,
                                 PendingAction action);

// The Win32 host. It owns the window, the message loop, the worker thread and
// the one real child control (the password field); everything about what the
// screen looks like lives in View, which macOS shares.
class AppWindow {
public:
    static bool register_class(HINSTANCE instance);
    // `initial_files` lets the window open pre-loaded, which is what makes
    // "Open with" and dropping files onto the executable work.
    static AppWindow* create(HINSTANCE instance,
                             std::vector<std::wstring> initial_files = {},
                             PendingAction action = PendingAction::None);

    [[nodiscard]] HWND handle() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT dispatch(UINT message, WPARAM wp, LPARAM lp);

    void on_create();
    void on_dpi_changed(int dpi, const RECT* suggested);
    void on_paint();
    void on_drop(HDROP drop);
    void on_click(POINT pt);
    void on_mouse_move(POINT pt);
    void on_destroy();

    // Hands the client rect to the view and puts the password control where the
    // view says the field goes.
    void relayout();
    // Resolves the custom frame: resize edges, the draggable caption, and the
    // window buttons we draw ourselves.
    LRESULT hit_test_frame(POINT screen);

    void browse_for_files();
    void add_files(std::vector<std::wstring> paths);
    void set_pending_action(PendingAction action);
    // Runs whatever the launcher asked for, defaulting to encryption.
    void run_pending_action();
    void clear_files();
    void start_job(bool encrypting);
    void finish_job(const job::Result& result);
    void append_log(job::Event kind, std::wstring text);

    // Pulls the password out of the edit control into locked memory and blanks
    // the control. The control's own heap buffer is beyond our reach, which is
    // a documented limitation.
    SecureBuffer take_password();

    int  scale(int value) const { return scaled(value, dpi_); }
    void invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }

    HWND         hwnd_ = nullptr;
    HWND         password_edit_ = nullptr;
    HBRUSH       edit_brush_ = nullptr;
    HICON        caption_icon_ = nullptr;
    int          dpi_ = 96;
    win32::Fonts fonts_;
    View         view_;

    bool          closing_ = false;
    PendingAction pending_ = PendingAction::None;

    // Shared with the worker thread; the view keeps its own plain copy for
    // painting, updated on the UI thread whenever this changes.
    std::thread       worker_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_{false};
};

}  // namespace seicure::gui
