#include "platform/console.hpp"

#include <termios.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <vector>

#include "platform/file.hpp"

namespace seicure::platform {
namespace {

// Restores the terminal even if reading throws, so a failure never leaves the
// user typing blind.
class EchoSuppressor {
public:
    explicit EchoSuppressor(int fd) : fd_(fd) {
        if (::tcgetattr(fd_, &saved_) == 0) {
            active_ = true;
            termios quiet = saved_;
            quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
            ::tcsetattr(fd_, TCSAFLUSH, &quiet);
        }
    }
    ~EchoSuppressor() {
        if (active_) {
            ::tcsetattr(fd_, TCSAFLUSH, &saved_);
        }
    }
    EchoSuppressor(const EchoSuppressor&) = delete;
    EchoSuppressor& operator=(const EchoSuppressor&) = delete;

private:
    int     fd_;
    termios saved_{};
    bool    active_ = false;
};

// Reads one line of raw bytes. stdin is already UTF-8 on POSIX, so unlike the
// Windows path there is no encoding conversion to do.
SecureBuffer read_line(int fd) {
    std::vector<std::uint8_t> bytes;
    std::uint8_t c = 0;
    while (true) {
        const ssize_t got = ::read(fd, &c, 1);
        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (got == 0 || c == '\n') break;
        if (c == '\r') continue;
        if (c == 0x7F || c == '\b') {  // backspace
            if (!bytes.empty()) bytes.pop_back();
            continue;
        }
        bytes.push_back(c);
    }

    // Strip a UTF-8 BOM. Some shells and scripts prepend one when piping, and
    // the same password would otherwise derive a different key depending on
    // how it was supplied.
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

void write_all(int fd, const std::string& text) {
    std::size_t total = 0;
    while (total < text.size()) {
        const ssize_t put = ::write(fd, text.data() + total, text.size() - total);
        if (put <= 0) {
            if (put < 0 && errno == EINTR) continue;
            break;
        }
        total += static_cast<std::size_t>(put);
    }
}

}  // namespace

bool stdout_is_console() { return ::isatty(STDOUT_FILENO) == 1; }

void print(const std::wstring& text) {
    write_all(STDOUT_FILENO, wide_to_utf8(text));
}

void print_error(const std::wstring& text) {
    write_all(STDERR_FILENO, wide_to_utf8(text));
}

SecureBuffer read_password(const wchar_t* prompt, bool confirm) {
    // Not a terminal: take one line and skip the confirmation, since a script
    // supplies the password exactly once.
    if (::isatty(STDIN_FILENO) != 1) {
        return read_line(STDIN_FILENO);
    }

    EchoSuppressor suppress(STDIN_FILENO);

    print(prompt);
    SecureBuffer first = read_line(STDIN_FILENO);
    print(L"\n");

    if (confirm) {
        print(L"Confirm password: ");
        SecureBuffer second = read_line(STDIN_FILENO);
        print(L"\n");

        const bool same = first.size() == second.size() &&
                          constant_time_equal(first.data(), second.data(),
                                              first.size());
        if (!same) {
            throw std::runtime_error("passwords do not match");
        }
    }
    return first;
}

}  // namespace seicure::platform
