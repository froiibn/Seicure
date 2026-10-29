#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "gui/app_window.hpp"

namespace {

// Per-session, per-user: two desktops or two accounts each get their own
// instance, which is what you want for a tool that holds a password.
constexpr const wchar_t* kInstanceMutex = L"Local\\SeicureSingleInstance";

struct Launch {
    std::vector<std::wstring>   files;
    seicure::gui::PendingAction action = seicure::gui::PendingAction::None;
};

// Files and verbs arrive from "Open with", a drop onto the executable, or the
// shell verbs the installer registers.
Launch parse_command_line() {
    Launch launch;
    int count = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv == nullptr) return launch;

    for (int i = 1; i < count; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--encrypt") {
            launch.action = seicure::gui::PendingAction::Encrypt;
            continue;
        }
        if (arg == L"--decrypt") {
            launch.action = seicure::gui::PendingAction::Decrypt;
            continue;
        }
        const DWORD attrs = GetFileAttributesW(arg.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            launch.files.push_back(arg);
        }
    }
    LocalFree(argv);
    return launch;
}

// Per-monitor v2 awareness keeps text crisp when the window is dragged between
// displays. Resolved dynamically: the API is Windows 10 1703+.
void enable_dpi_awareness() {
    using SetContextFn = BOOL(WINAPI*)(HANDLE);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return;

    auto set_context = reinterpret_cast<SetContextFn>(reinterpret_cast<void*>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
    if (set_context != nullptr) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        if (set_context(reinterpret_cast<HANDLE>(-4))) return;
    }

    using SetDpiAwareFn = BOOL(WINAPI*)(void);
    auto set_aware = reinterpret_cast<SetDpiAwareFn>(
        reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDPIAware")));
    if (set_aware != nullptr) {
        set_aware();
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    enable_dpi_awareness();
    const Launch launch = parse_command_line();

    // Explorer starts one process per selected file. Hand the paths to the
    // window that already exists instead of opening a second one.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
    const bool already_running =
        mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;

    if (already_running) {
        if (seicure::gui::forward_to_running_instance(launch.files,
                                                      launch.action)) {
            CloseHandle(mutex);
            return 0;
        }
        // The mutex outlived its window (a crash, say); carry on and open one.
    }

    if (!seicure::gui::AppWindow::register_class(instance)) {
        MessageBoxW(nullptr, L"Failed to register the window class.", L"Seicure",
                    MB_ICONERROR);
        return 1;
    }

    seicure::gui::AppWindow* window =
        seicure::gui::AppWindow::create(instance, launch.files, launch.action);
    if (window == nullptr) {
        MessageBoxW(nullptr, L"Failed to create the window.", L"Seicure",
                    MB_ICONERROR);
        return 1;
    }

    ShowWindow(window->handle(), show);
    UpdateWindow(window->handle());

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    delete window;
    if (mutex != nullptr) {
        CloseHandle(mutex);
    }
    return 0;
}
