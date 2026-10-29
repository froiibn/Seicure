#pragma once

#include <string>
#include <vector>

#include "gui/ui/view.hpp"

// The Cocoa front end's only C++ entry point. Everything else about it is
// Objective-C++ and stays inside app_window.mm.
namespace seicure::gui::mac {

// Creates the window, pre-loads `files`, and runs the AppKit event loop until
// the user quits. Returns the process exit code.
//
// `dump_ui_path`, when set, renders the laid-out window to a PNG at that path
// and exits instead of running the loop. That is how the Core Graphics
// renderer gets checked against the GDI one: a screenshot taken over SSH needs
// Screen Recording permission, but a window can always draw itself.
int run(std::vector<std::wstring> files, PendingAction action,
        const std::string& dump_ui_path = {});

}  // namespace seicure::gui::mac
