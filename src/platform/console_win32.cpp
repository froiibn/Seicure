#include "platform/console.hpp"

#include <windows.h>

#include <cstring>
#include <stdexcept>
#include <vector>

#include "platform/file.hpp"

namespace seicure::platform {
namespace {

// Restores the console mode even if reading throws.
class EchoSuppressor {
public:
    explicit EchoSuppressor(HANDLE in) : in_(in) {
        if (GetConsoleMode(in_, &saved_)) {
            active_ = true;
            SetConsoleMode(in_, saved_ & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
        }
    }
    ~EchoSuppressor() {
        if (active_) {
            SetConsoleMode(in_, saved_);
        }
    }
    EchoSuppressor(const EchoSuppressor&) = delete;
    EchoSuppressor& operator=(const EchoSuppressor&) = delete;

private:
    HANDLE in_;
    DWORD  saved_ = 0;
    bool   active_ = false;
};

// Reads one line of UTF-16 from the console into a wiped buffer.
std::vector<wchar_t> read_line_secret(HANDLE in) {
    std::vector<wchar_t> line;
    wchar_t ch = 0;
    DWORD read = 0;
    while (ReadConsoleW(in, &ch, 1, &read, nullptr) && read == 1) {
        if (ch == L'\r') continue;
        if (ch == L'\n') break;
        if (ch == L'\b') {  // backspace
            if (!line.empty()) line.pop_back();
            continue;
        }
        line.push_back(ch);
    }
    return line;
}

void wipe(std::vector<wchar_t>& v) {
    if (!v.empty()) {
        secure_zero(v.data(), v.size() * sizeof(wchar_t));
    }
    v.clear();
}

// Reads one line from a pipe or file. ReadConsoleW only works on a real
// console handle, so without this the tool could not be scripted or tested.
SecureBuffer read_line_redirected(HANDLE in) {
    std::vector<std::uint8_t> bytes;
    std::uint8_t ch = 0;
    DWORD read = 0;
    while (ReadFile(in, &ch, 1, &read, nullptr) && read == 1) {
        if (ch == '\r') continue;
        if (ch == '\n') break;
        bytes.push_back(ch);
    }

    // Strip a leading UTF-8 BOM. PowerShell prepends EF BB BF when it pipes a
    // string to a native process, so `"pw" | seicure enc` would otherwise
    // derive a different key than the same password typed at the prompt --
    // and the file could never be opened interactively again.
    std::size_t start = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
        bytes[2] == 0xBF) {
        start = 3;
    }

    const std::size_t length = bytes.size() - start;
    SecureBuffer out(length);
    if (length > 0) {
        std::memcpy(out.data(), bytes.data() + start, length);
    }
    if (!bytes.empty()) {
        secure_zero(bytes.data(), bytes.size());
    }
    return out;
}

SecureBuffer to_utf8_secure(const std::vector<wchar_t>& line) {
    if (line.empty()) {
        return SecureBuffer(0);
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, line.data(),
                                           static_cast<int>(line.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        throw std::runtime_error("password encoding failed");
    }
    SecureBuffer out(static_cast<std::size_t>(needed));
    WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()),
                        reinterpret_cast<char*>(out.data()), needed,
                        nullptr, nullptr);
    return out;
}

}  // namespace

bool stdout_is_console() {
    return GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR;
}

void print(const std::wstring& text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    if (GetFileType(out) == FILE_TYPE_CHAR) {
        WriteConsoleW(out, text.data(), static_cast<DWORD>(text.size()),
                      &written, nullptr);
    } else {
        // Redirected to a file or pipe: emit UTF-8 bytes instead.
        const std::string utf8 = wide_to_utf8(text);
        WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
                  nullptr);
    }
}

void print_error(const std::wstring& text) {
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    if (GetFileType(err) == FILE_TYPE_CHAR) {
        WriteConsoleW(err, text.data(), static_cast<DWORD>(text.size()),
                      &written, nullptr);
    } else {
        const std::string utf8 = wide_to_utf8(text);
        WriteFile(err, utf8.data(), static_cast<DWORD>(utf8.size()), &written,
                  nullptr);
    }
}

SecureBuffer read_password(const wchar_t* prompt, bool confirm) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);

    // Redirected stdin: take one line as UTF-8 and skip the confirmation,
    // since a script supplies the password exactly once.
    if (GetFileType(in) != FILE_TYPE_CHAR) {
        return read_line_redirected(in);
    }

    EchoSuppressor suppress(in);

    print(prompt);
    std::vector<wchar_t> first = read_line_secret(in);
    print(L"\n");

    if (confirm) {
        print(L"Confirm password: ");
        std::vector<wchar_t> second = read_line_secret(in);
        print(L"\n");

        const bool same = first.size() == second.size() &&
                          constant_time_equal(first.data(), second.data(),
                                              first.size() * sizeof(wchar_t));
        wipe(second);
        if (!same) {
            wipe(first);
            throw std::runtime_error("passwords do not match");
        }
    }

    SecureBuffer out = to_utf8_secure(first);
    wipe(first);
    return out;
}

}  // namespace seicure::platform
