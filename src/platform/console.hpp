#pragma once

#include <string>

#include "core/secure_buffer.hpp"

namespace seicure::platform {

// Reads a password with terminal echo disabled and returns it as UTF-8 bytes
// in locked, self-wiping memory. Never lands in a std::string, whose
// reallocations would scatter plaintext copies across the heap.
//
// When `confirm` is set, prompts twice and requires a match.
SecureBuffer read_password(const wchar_t* prompt, bool confirm);

// Writes UTF-8 text to the console as UTF-16 so non-ASCII filenames survive
// regardless of the active code page.
void print(const std::wstring& text);
void print_error(const std::wstring& text);

// False when stdout is redirected to a file or pipe, where carriage-return
// progress updates would be recorded as noise rather than overwritten.
bool stdout_is_console();

}  // namespace seicure::platform
