#import <Foundation/Foundation.h>

#include <string>
#include <vector>

#include "gui/mac/app_window.hpp"
#include "platform/file.hpp"

// Files and verbs arrive from the Finder's "Open With", a drop onto the app
// icon, or the command line. The Windows front end takes the same two flags, so
// a launcher script written for one works on the other.
int main(int argc, const char* argv[]) {
    std::vector<std::wstring> files;
    auto action = seicure::gui::PendingAction::None;
    std::string dump_ui;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        // Renders the window to a PNG and exits. Used to check this front end's
        // drawing against the Windows one without a screen to capture.
        if (arg == "--dump-ui" && i + 1 < argc) {
            dump_ui = argv[++i];
            continue;
        }
        if (arg == "--encrypt") {
            action = seicure::gui::PendingAction::Encrypt;
            continue;
        }
        if (arg == "--decrypt") {
            action = seicure::gui::PendingAction::Decrypt;
            continue;
        }
        // Launch Services passes -psn_0_xxxxx when the app is opened from the
        // Finder on older systems; it is not a path.
        if (arg.rfind("-psn_", 0) == 0) {
            continue;
        }
        const std::wstring path = seicure::platform::utf8_to_wide(arg);
        if (seicure::platform::file_exists(path)) {
            files.push_back(path);
        }
    }

    return seicure::gui::mac::run(std::move(files), action, dump_ui);
}
