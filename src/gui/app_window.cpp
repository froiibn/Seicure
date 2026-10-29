#include "gui/app_window.hpp"

#include <shellapi.h>
#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <memory>

#include "gui/ui/theme.hpp"
#include "platform/file.hpp"

namespace seicure::gui {
namespace {

constexpr const wchar_t* kClassName = kWindowClass;
constexpr int            kEditId    = 1001;
constexpr int            kIconId    = 1;  // must match app.rc
constexpr UINT_PTR       kCaretTimer = 1;
constexpr UINT           kCaretMs    = 530;  // the classic Windows blink rate

// The shared palette is plain RGB; GDI wants 0x00BBGGRR. Painting goes through
// the canvas, so this is only needed for the few places Win32 itself asks for a
// colour -- the edit control's background, mostly.
COLORREF cr(Color c) {
    return RGB(c.r, c.g, c.b);
}

RECT to_rect(const Rect& r) {
    return RECT{r.left, r.top, r.right, r.bottom};
}

// Pulls one size out of the multi-size .ico. LoadImage picks the closest
// authored variant rather than resampling the 256px one, which is the whole
// point of authoring 16/20/24 by hand.
HICON load_app_icon(HINSTANCE instance, int size) {
    return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kIconId),
                                         IMAGE_ICON, size, size,
                                         LR_DEFAULTCOLOR));
}

int dpi_for(HWND hwnd) {
    // GetDpiForWindow is Windows 10 1607+; resolve dynamically so the binary
    // still loads on older systems.
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static auto fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void*>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    if (fn != nullptr) {
        const UINT dpi = fn(hwnd);
        if (dpi != 0) return static_cast<int>(dpi);
    }
    HDC dc = GetDC(hwnd);
    const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);
    return dpi == 0 ? 96 : dpi;
}

// The system caption is removed in WM_NCCALCSIZE, so nothing here needs the
// dark-title-bar attribute. Rounded corners still come from DWM, and asking
// for them explicitly keeps the shape right on Windows 11.
void request_rounded_corners(HWND hwnd) {
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm == nullptr) return;

    auto set_attribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        reinterpret_cast<void*>(GetProcAddress(dwm, "DwmSetWindowAttribute")));
    if (set_attribute != nullptr) {
        // DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2.
        const DWORD preference = 2;
        set_attribute(hwnd, 33, &preference, sizeof(preference));
    }
    FreeLibrary(dwm);
}

// Payload for WM_COPYDATA when a second launch hands its files to the first.
// dwData carries the action; the buffer is newline-separated paths.
constexpr ULONG_PTR kCopyDataId = 0x5E1C;

std::vector<std::wstring> split_lines(const std::wstring& blob) {
    std::vector<std::wstring> out;
    std::size_t start = 0;
    while (start <= blob.size()) {
        const std::size_t at = blob.find(L'\n', start);
        const std::wstring piece = blob.substr(
            start, at == std::wstring::npos ? std::wstring::npos : at - start);
        if (!piece.empty()) out.push_back(piece);
        if (at == std::wstring::npos) break;
        start = at + 1;
    }
    return out;
}

}  // namespace

bool forward_to_running_instance(const std::vector<std::wstring>& files,
                                 PendingAction action) {
    HWND existing = FindWindowW(kWindowClass, nullptr);
    if (existing == nullptr) {
        return false;
    }

    std::wstring blob;
    for (const auto& f : files) {
        blob += f;
        blob += L'\n';
    }

    COPYDATASTRUCT data{};
    data.dwData = kCopyDataId + static_cast<ULONG_PTR>(action);
    data.cbData = static_cast<DWORD>((blob.size() + 1) * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(blob.c_str());

    SendMessageW(existing, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));

    // Bring the window the user is actually going to type into forward.
    ShowWindow(existing, SW_RESTORE);
    SetForegroundWindow(existing);
    return true;
}

// ---------------------------------------------------------------------------
// Window plumbing
// ---------------------------------------------------------------------------

bool AppWindow::register_class(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &AppWindow::window_proc;
    wc.hInstance     = instance;
    wc.lpszClassName = kClassName;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // fully custom painted

    // Alt+Tab and the taskbar take the large icon; the small one shows in the
    // window list and thumbnails.
    wc.hIcon = load_app_icon(instance, GetSystemMetrics(SM_CXICON));
    wc.hIconSm = load_app_icon(instance, GetSystemMetrics(SM_CXSMICON));
    if (wc.hIcon == nullptr) {
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    return RegisterClassExW(&wc) != 0;
}

AppWindow* AppWindow::create(HINSTANCE instance,
                             std::vector<std::wstring> initial_files,
                             PendingAction action) {
    auto* self = new AppWindow();

    self->hwnd_ = CreateWindowExW(
        0, kClassName, L"Seicure", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, theme::WINDOW_W, theme::WINDOW_H,
        nullptr, nullptr, instance, self);

    if (self->hwnd_ == nullptr) {
        delete self;
        return nullptr;
    }

    // CreateWindow takes physical pixels, so the design size has to be scaled
    // by the DPI of the monitor the window actually landed on -- otherwise the
    // window comes up a third too small on a 150% display. The DPI is only
    // knowable once the window exists, hence the resize here.
    RECT desired{0, 0, scaled(theme::WINDOW_W, self->dpi_),
                 scaled(theme::WINDOW_H, self->dpi_)};
    AdjustWindowRectEx(&desired, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const int width  = desired.right - desired.left;
    const int height = desired.bottom - desired.top;

    // Centre on the work area of the current monitor.
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (GetMonitorInfoW(MonitorFromWindow(self->hwnd_, MONITOR_DEFAULTTONEAREST),
                        &monitor)) {
        x = monitor.rcWork.left +
            ((monitor.rcWork.right - monitor.rcWork.left) - width) / 2;
        y = monitor.rcWork.top +
            ((monitor.rcWork.bottom - monitor.rcWork.top) - height) / 2;
    }
    // SWP_FRAMECHANGED forces WM_NCCALCSIZE to run so the custom frame takes
    // effect before the window is first shown.
    SetWindowPos(self->hwnd_, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    if (!initial_files.empty()) {
        self->add_files(std::move(initial_files));
    }
    self->set_pending_action(action);
    return self;
}

LRESULT CALLBACK AppWindow::window_proc(HWND hwnd, UINT message, WPARAM wp,
                                        LPARAM lp) {
    AppWindow* self = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<AppWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self != nullptr) {
        return self->dispatch(message, wp, lp);
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

LRESULT AppWindow::dispatch(UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE:
        on_create();
        return 0;

    case WM_ERASEBKGND:
        return 1;  // painted in full by WM_PAINT; erasing here would flicker

    case WM_NCCALCSIZE: {
        if (wp == FALSE) {
            break;
        }
        // Reclaim the caption strip into the client area so the app header is
        // the title bar. The left/right/bottom frame is left to DefWindowProc,
        // which is what keeps resizing, snapping and the drop shadow native.
        auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
        const LONG requested_top = params->rgrc[0].top;
        DefWindowProcW(hwnd_, message, wp, lp);
        params->rgrc[0].top = requested_top;

        if (IsZoomed(hwnd_)) {
            // A maximized window extends past the monitor by the frame
            // thickness; without this inset the caption sits off-screen.
            const int overhang = GetSystemMetrics(SM_CYFRAME) +
                                 GetSystemMetrics(SM_CXPADDEDBORDER);
            params->rgrc[0].top += overhang;
        }
        return 0;
    }

    case WM_NCHITTEST:
        return hit_test_frame(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});

    case WM_NCACTIVATE:
        // We own the entire frame; let DefWindowProc skip its caption repaint.
        invalidate();
        return DefWindowProcW(hwnd_, message, wp, -1);

    case WM_PAINT:
        on_paint();
        return 0;

    case WM_SIZE:
        relayout();
        invalidate();
        return 0;

    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lp);
        on_dpi_changed(static_cast<int>(LOWORD(wp)), suggested);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize.x = scale(theme::MIN_W);
        info->ptMinTrackSize.y = scale(theme::MIN_H);
        return 0;
    }

    case WM_DROPFILES:
        on_drop(reinterpret_cast<HDROP>(wp));
        return 0;

    case WM_COPYDATA: {
        // A second launch handing over its files rather than opening its own
        // window. Explorer starts one process per selected file.
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lp);
        if (data == nullptr || data->lpData == nullptr ||
            data->dwData < kCopyDataId || data->dwData > kCopyDataId + 2) {
            return 0;
        }
        add_files(split_lines(static_cast<const wchar_t*>(data->lpData)));
        set_pending_action(
            static_cast<PendingAction>(data->dwData - kCopyDataId));
        return 1;
    }

    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        on_click(pt);
        return 0;
    }

    case WM_MOUSEMOVE: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        on_mouse_move(pt);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (view_.hot() != Hot::None) {
            view_.set_hot(Hot::None);
            invalidate();
        }
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetTextColor(dc, cr(theme::TEXT));
        SetBkColor(dc, cr(theme::SURFACE));
        return reinterpret_cast<LRESULT>(edit_brush_);
    }

    case WM_COMMAND:
        if (LOWORD(wp) == kEditId) {
            const WORD code = HIWORD(wp);
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                view_.set_password_focused(code == EN_SETFOCUS);
                invalidate();
            }
        }
        return 0;

    case WM_TIMER:
        if (wp == kCaretTimer) {
            view_.set_caret_on(!view_.caret_on());
            // Repaint only the status strip; blinking must not cost a full
            // redraw twice a second.
            const RECT status = to_rect(view_.regions().status);
            InvalidateRect(hwnd_, &status, FALSE);
        }
        return 0;

    case WM_JOB_PROGRESS:
        view_.set_progress_percent(static_cast<int>(wp));
        invalidate();
        return 0;

    case WM_JOB_LOG: {
        std::unique_ptr<LogLine> line(reinterpret_cast<LogLine*>(lp));
        append_log(line->kind, std::move(line->text));
        return 0;
    }

    case WM_JOB_DONE: {
        std::unique_ptr<job::Result> result(reinterpret_cast<job::Result*>(lp));
        finish_job(*result);
        return 0;
    }

    case WM_CLOSE:
        if (busy_.load()) {
            // Let the worker unwind first: killing it mid-write would leave
            // temporary files behind. finish_job() completes the close.
            closing_ = true;
            cancel_.store(true);
            view_.set_status(L"cancelling...");
            invalidate();
            return 0;
        }
        DestroyWindow(hwnd_);
        return 0;

    case WM_DESTROY:
        on_destroy();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wp, lp);
}

void AppWindow::on_create() {
    dpi_ = dpi_for(hwnd_);
    fonts_.create(dpi_);
    edit_brush_ = CreateSolidBrush(cr(theme::SURFACE));
    request_rounded_corners(hwnd_);

    caption_icon_ = load_app_icon(
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE)),
        scale(20));

    // Windows draws its own minimise/maximise/close in the caption strip.
    view_.set_caption_style(CaptionStyle{true, 0});

    DragAcceptFiles(hwnd_, TRUE);

    // Enter in the password box runs the job. Without this the context-menu
    // flow would be: right-click, type password, then reach for the mouse.
    static WNDPROC original_edit_proc = nullptr;
    static auto edit_proc = [](HWND edit, UINT msg, WPARAM wp,
                               LPARAM lp) -> LRESULT {
        if (msg == WM_CHAR && wp == VK_RETURN) {
            auto* self = reinterpret_cast<AppWindow*>(
                GetWindowLongPtrW(GetParent(edit), GWLP_USERDATA));
            if (self != nullptr) {
                self->run_pending_action();
            }
            return 0;  // swallow it so the control does not beep
        }
        return CallWindowProcW(original_edit_proc, edit, msg, wp, lp);
    };

    password_edit_ = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL | ES_LEFT,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(kEditId),
        reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE)),
        nullptr);
    SendMessageW(password_edit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(fonts_.body), TRUE);

    original_edit_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        password_edit_, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(static_cast<WNDPROC>(edit_proc))));

    SetTimer(hwnd_, kCaretTimer, kCaretMs, nullptr);
    relayout();
}

void AppWindow::on_dpi_changed(int dpi, const RECT* suggested) {
    dpi_ = dpi;
    fonts_.create(dpi_);

    // Reload rather than stretch: a different DPI wants a different authored
    // size out of the .ico.
    if (caption_icon_ != nullptr) {
        DestroyIcon(caption_icon_);
    }
    caption_icon_ = load_app_icon(
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE)),
        scale(20));

    SendMessageW(password_edit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(fonts_.body), TRUE);
    if (suggested != nullptr) {
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    relayout();
    invalidate();
}

void AppWindow::on_destroy() {
    KillTimer(hwnd_, kCaretTimer);
    cancel_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    fonts_.destroy();
    if (edit_brush_ != nullptr) {
        DeleteObject(edit_brush_);
        edit_brush_ = nullptr;
    }
    if (caption_icon_ != nullptr) {
        DestroyIcon(caption_icon_);
        caption_icon_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void AppWindow::relayout() {
    RECT client;
    GetClientRect(hwnd_, &client);

    view_.set_maximized(IsZoomed(hwnd_) != 0);
    view_.set_client(Rect{client.left, client.top, client.right, client.bottom},
                     dpi_);
    view_.compute_layout();

    if (password_edit_ != nullptr) {
        const Rect inner =
            view_.regions().password.inset(scale(12), scale(9));
        SetWindowPos(password_edit_, nullptr, inner.left, inner.top,
                     inner.width(), inner.height(), SWP_NOZORDER);
    }
}

LRESULT AppWindow::hit_test_frame(POINT screen) {
    POINT pt = screen;
    ScreenToClient(hwnd_, &pt);

    RECT client;
    GetClientRect(hwnd_, &client);

    // Resize edges. A maximized window has none.
    if (!IsZoomed(hwnd_)) {
        const int band = scale(theme::RESIZE_BAND);
        const bool left   = pt.x < band;
        const bool right  = pt.x >= client.right - band;
        const bool top    = pt.y < band;
        const bool bottom = pt.y >= client.bottom - band;
        if (top && left)     return HTTOPLEFT;
        if (top && right)    return HTTOPRIGHT;
        if (bottom && left)  return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (top)             return HTTOP;
        if (bottom)          return HTBOTTOM;
        if (left)            return HTLEFT;
        if (right)           return HTRIGHT;
    }

    // HTCAPTION gives dragging, double-click to maximize, snap layouts and the
    // system menu for free; our own buttons take their clicks as HTCLIENT.
    if (view_.is_caption_drag(Point{pt.x, pt.y})) {
        return HTCAPTION;
    }
    return HTCLIENT;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void AppWindow::on_paint() {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd_, &ps);

    RECT client;
    GetClientRect(hwnd_, &client);

    // Double buffered: the layout repaints wholesale on every hover change.
    HDC     mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, client.right, client.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);

    win32::GdiCanvas canvas(mem, fonts_, caption_icon_, dpi_);
    view_.paint(canvas);

    // Blit only the damaged region: the caret timer invalidates a thin strip.
    const RECT& damage = ps.rcPaint;
    BitBlt(screen, damage.left, damage.top, damage.right - damage.left,
           damage.bottom - damage.top, mem, damage.left, damage.top, SRCCOPY);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void AppWindow::on_mouse_move(POINT pt) {
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
    TrackMouseEvent(&track);

    const Hot next = view_.hit_test(Point{pt.x, pt.y});
    if (next != view_.hot()) {
        view_.set_hot(next);
        invalidate();
    }
}

void AppWindow::on_click(POINT pt) {
    switch (view_.hit_test(Point{pt.x, pt.y})) {
    case Hot::Panel:
        if (!busy_.load()) browse_for_files();
        break;
    case Hot::ModeIndividual:
        view_.set_archive_mode(false);
        invalidate();
        break;
    case Hot::ModeArchive:
        view_.set_archive_mode(true);
        invalidate();
        break;
    case Hot::Encrypt:
        start_job(true);
        break;
    case Hot::Decrypt:
        start_job(false);
        break;
    case Hot::Clear:
        clear_files();
        break;
    case Hot::Cancel:
        cancel_.store(true);
        view_.set_status(L"cancelling...");
        invalidate();
        break;
    case Hot::Minimize:
        ShowWindow(hwnd_, SW_MINIMIZE);
        break;
    case Hot::Maximize:
        ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
        break;
    case Hot::Close:
        SendMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    default:
        SetFocus(hwnd_);
        break;
    }
}

void AppWindow::on_drop(HDROP drop) {
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> paths;
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(length, L'\0');
        DragQueryFileW(drop, i, path.data(), length + 1);
        // Directories are not handled yet; silently skipping would be worse
        // than saying so.
        const DWORD attrs = GetFileAttributesW(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            append_log(job::Event::Error,
                       platform::base_name(path) + L": folders are not supported yet");
            continue;
        }
        paths.push_back(std::move(path));
    }
    DragFinish(drop);
    add_files(std::move(paths));
}

void AppWindow::browse_for_files() {
    // Multi-select needs a generous buffer: the result is one directory
    // followed by NUL-separated file names.
    std::vector<wchar_t> buffer(64 * 1024, L'\0');

    OPENFILENAMEW ofn{};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = hwnd_;
    ofn.lpstrFile    = buffer.data();
    ofn.nMaxFile     = static_cast<DWORD>(buffer.size());
    ofn.lpstrFilter  = L"All files\0*.*\0Seicure containers\0*.sef\0";
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST |
                OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) {
        return;
    }

    std::vector<std::wstring> paths;
    const std::wstring directory = buffer.data();
    const wchar_t* cursor = buffer.data() + directory.size() + 1;

    if (*cursor == L'\0') {
        paths.push_back(directory);  // single selection: the full path
    } else {
        while (*cursor != L'\0') {
            const std::wstring name = cursor;
            paths.push_back(platform::join_path(directory, name));
            cursor += name.size() + 1;
        }
    }
    add_files(std::move(paths));
}

void AppWindow::add_files(std::vector<std::wstring> paths) {
    view_.add_files(paths);
    const std::size_t count = view_.files().size();
    view_.set_status(count == 0
                         ? L"ready"
                         : std::to_wstring(count) + L" file(s) selected");
    invalidate();
}

void AppWindow::set_pending_action(PendingAction action) {
    pending_ = action;
    if (action == PendingAction::None) {
        return;
    }
    // The launcher already said what to do, so the only thing left is the
    // password. Put the caret there and say so.
    view_.set_status(action == PendingAction::Encrypt
                         ? L"enter password, then press Enter to encrypt"
                         : L"enter password, then press Enter to decrypt");
    SetFocus(password_edit_);
    invalidate();
}

void AppWindow::run_pending_action() {
    if (busy_.load() || view_.files().empty()) {
        return;
    }
    start_job(pending_ != PendingAction::Decrypt);
}

void AppWindow::clear_files() {
    if (busy_.load()) return;
    view_.clear_files();
    view_.clear_log();
    view_.clear_progress();
    view_.set_status(L"ready");
    invalidate();
}

void AppWindow::append_log(job::Event kind, std::wstring text) {
    view_.append_log(kind, std::move(text));
    invalidate();
}

SecureBuffer AppWindow::take_password() {
    const int length = GetWindowTextLengthW(password_edit_);
    if (length <= 0) {
        return SecureBuffer(0);
    }

    std::vector<wchar_t> wide(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(password_edit_, wide.data(), length + 1);

    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), length,
                                           nullptr, 0, nullptr, nullptr);
    SecureBuffer out(needed > 0 ? static_cast<std::size_t>(needed) : 0);
    if (needed > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), length,
                            reinterpret_cast<char*>(out.data()), needed,
                            nullptr, nullptr);
    }
    secure_zero(wide.data(), wide.size() * sizeof(wchar_t));
    return out;
}

// ---------------------------------------------------------------------------
// Job control
// ---------------------------------------------------------------------------

void AppWindow::start_job(bool encrypting) {
    if (busy_.load() || view_.files().empty()) {
        return;
    }

    SecureBuffer password = take_password();
    if (password.empty()) {
        append_log(job::Event::Error, L"enter a password first");
        SetFocus(password_edit_);
        return;
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    busy_.store(true);
    cancel_.store(false);
    view_.set_busy(true);
    const std::wstring label = encrypting ? L"encrypting" : L"decrypting";
    view_.set_progress(0, label);
    view_.set_status(label);
    view_.clear_log();
    invalidate();

    job::Options options;
    options.archive = view_.archive_mode() && encrypting;

    HWND target = hwnd_;
    auto inputs = view_.file_paths();
    auto secret = std::make_shared<SecureBuffer>(std::move(password));

    worker_ = std::thread([this, target, inputs, secret, options, encrypting] {
        auto log = [target](job::Event kind, const std::wstring& text) {
            PostMessageW(target, WM_JOB_LOG, 0,
                         reinterpret_cast<LPARAM>(new LogLine{kind, text}));
        };

        int last_percent = -1;
        auto progress = [this, target, &last_percent](
                            int percent, const std::wstring&) {
            if (cancel_.load()) {
                return false;
            }
            // Only post on change: the engine calls back once per chunk, which
            // would otherwise flood the message queue on a large file.
            if (percent != last_percent) {
                last_percent = percent;
                PostMessageW(target, WM_JOB_PROGRESS,
                             static_cast<WPARAM>(percent), 0);
            }
            return true;
        };

        job::Result result;
        try {
            result = encrypting
                         ? job::encrypt(inputs, secret->data(), secret->size(),
                                        options, log, progress)
                         : job::decrypt(inputs, secret->data(), secret->size(),
                                        options, log, progress);
        } catch (const std::exception& e) {
            log(job::Event::Error, platform::utf8_to_wide(e.what()));
            result.failed = 1;
        }

        PostMessageW(target, WM_JOB_DONE, 0,
                     reinterpret_cast<LPARAM>(new job::Result(result)));
    });
}

void AppWindow::finish_job(const job::Result& result) {
    busy_.store(false);
    view_.set_busy(false);
    view_.clear_progress();

    if (result.cancelled) {
        view_.set_status(L"cancelled");
        append_log(job::Event::Info, L"cancelled -- nothing was written");
    } else if (result.failed > 0) {
        view_.set_status(std::to_wstring(result.failed) + L" failed");
    } else {
        view_.set_status(L"done  " + std::to_wstring(result.succeeded) +
                         L" ok");
    }

    // The password field is cleared on completion so it does not linger on
    // screen or in the control between operations.
    SetWindowTextW(password_edit_, L"");
    invalidate();

    if (closing_) {
        DestroyWindow(hwnd_);
    }
}

}  // namespace seicure::gui
