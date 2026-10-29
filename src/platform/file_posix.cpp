#include "platform/file.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

// POSIX counterpart of file_win32.cpp. Paths cross this boundary as
// std::wstring because everything above the platform layer is written that
// way; they are converted to UTF-8 byte strings here, at the syscall edge.
namespace seicure::platform {
namespace {

// FILETIME epoch (1601-01-01) to Unix epoch (1970-01-01), in 100 ns ticks.
constexpr std::uint64_t kEpochDeltaTicks = 116444736000000000ULL;
constexpr std::uint64_t kTicksPerSecond  = 10000000ULL;

// The two POSIX families spell stat()'s nanosecond timestamps differently:
// BSD (macOS) uses st_mtimespec, glibc uses st_mtim.
const timespec& mtime_of(const struct stat& st) {
#if defined(__APPLE__)
    return st.st_mtimespec;
#else
    return st.st_mtim;
#endif
}

const timespec& atime_of(const struct stat& st) {
#if defined(__APPLE__)
    return st.st_atimespec;
#else
    return st.st_atim;
#endif
}

std::uint64_t to_filetime(const timespec& ts) {
    if (ts.tv_sec == 0 && ts.tv_nsec == 0) {
        return 0;  // treated as "unknown" by the format
    }
    return kEpochDeltaTicks +
           static_cast<std::uint64_t>(ts.tv_sec) * kTicksPerSecond +
           static_cast<std::uint64_t>(ts.tv_nsec) / 100ULL;
}

timespec from_filetime(std::uint64_t ticks) {
    timespec ts{};
    if (ticks < kEpochDeltaTicks) {
        ts.tv_nsec = UTIME_OMIT;  // unknown: leave whatever the OS set
        return ts;
    }
    const std::uint64_t unix_ticks = ticks - kEpochDeltaTicks;
    ts.tv_sec  = static_cast<time_t>(unix_ticks / kTicksPerSecond);
    ts.tv_nsec = static_cast<long>((unix_ticks % kTicksPerSecond) * 100ULL);
    return ts;
}

std::string narrow(const std::wstring& s) { return wide_to_utf8(s); }

[[noreturn]] void throw_errno(const char* what) {
    throw FileError(std::string(what) + ": " + std::strerror(errno));
}

}  // namespace

// ---------------------------------------------------------------------------
// String and path helpers
// ---------------------------------------------------------------------------

std::wstring to_wide(const std::u16string& s) {
    return std::wstring(s.begin(), s.end());
}

std::u16string to_u16(const std::wstring& s) {
    return std::u16string(s.begin(), s.end());
}

// wchar_t is 32-bit on POSIX, so these convert code point by code point rather
// than reinterpreting units. Only the BMP-and-above split differs from Windows.
std::string wide_to_utf8(const std::wstring& s) {
    std::string out;
    out.reserve(s.size());
    for (wchar_t wc : s) {
        auto cp = static_cast<std::uint32_t>(wc);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::wstring utf8_to_wide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = c;
        std::size_t extra = 0;
        if (c >= 0xF0)      { cp = c & 0x07u; extra = 3; }
        else if (c >= 0xE0) { cp = c & 0x0Fu; extra = 2; }
        else if (c >= 0xC0) { cp = c & 0x1Fu; extra = 1; }
        ++i;
        for (std::size_t k = 0; k < extra && i < s.size(); ++k, ++i) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3Fu);
        }
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

std::wstring base_name(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L'/');
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

std::wstring parent_dir(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L'/');
    if (at == std::wstring::npos) return L".";
    if (at == 0) return L"/";
    return path.substr(0, at);
}

std::wstring join_path(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (dir.back() == L'/') return dir + name;
    return dir + L"/" + name;
}

bool file_exists(const std::wstring& path) {
    struct stat st {};
    return ::stat(narrow(path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::uint64_t file_size(const std::wstring& path) {
    struct stat st {};
    if (::stat(narrow(path).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return static_cast<std::uint64_t>(st.st_size);
}

bool directory_exists(const std::wstring& path) {
    struct stat st {};
    return ::stat(narrow(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::wstring random_hex_name(std::size_t random_bytes) {
    std::vector<std::uint8_t> raw(random_bytes);
    crypto::random_bytes(raw.data(), raw.size());

    static const wchar_t* kDigits = L"0123456789abcdef";
    std::wstring out;
    out.reserve(random_bytes * 2);
    for (const std::uint8_t b : raw) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0F]);
    }
    return out;
}

// ---------------------------------------------------------------------------
// FileReader
// ---------------------------------------------------------------------------

FileReader::FileReader(const std::wstring& path) {
    const std::string p = narrow(path);
    const int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw FileError("cannot open input file: " + std::string(std::strerror(errno)));
    }
    handle_ = reinterpret_cast<void*>(static_cast<std::intptr_t>(fd));

    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        handle_ = nullptr;
        throw FileError("cannot stat input file");
    }

    size_ = static_cast<std::uint64_t>(st.st_size);
    metadata_.original_size    = size_;
    metadata_.last_write_time  = to_filetime(mtime_of(st));
    metadata_.last_access_time = to_filetime(atime_of(st));
#if defined(__APPLE__)
    // macOS exposes a real creation time; Linux does not through stat(), so it
    // stays zero and the format reads that as "unknown".
    metadata_.creation_time = to_filetime(st.st_birthtimespec);
#else
    metadata_.creation_time = 0;
#endif
    metadata_.posix_mode    = static_cast<std::uint32_t>(st.st_mode) & 07777u;
    metadata_.meta_flags    = format::META_HAS_POSIX_MODE;
    metadata_.content_type  = format::ContentType::File;
    metadata_.original_name = to_u16(base_name(path));
}

FileReader::~FileReader() {
    if (handle_ != nullptr) {
        ::close(static_cast<int>(reinterpret_cast<std::intptr_t>(handle_)));
    }
}

std::size_t FileReader::read(std::uint8_t* out, std::size_t n) {
    const int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(handle_));
    std::size_t total = 0;
    while (total < n) {
        const ssize_t got = ::read(fd, out + total, n - total);
        if (got < 0) {
            if (errno == EINTR) continue;
            throw_errno("read failed");
        }
        if (got == 0) break;  // end of file
        total += static_cast<std::size_t>(got);
    }
    return total;
}

// ---------------------------------------------------------------------------
// AtomicFileWriter
// ---------------------------------------------------------------------------

AtomicFileWriter::AtomicFileWriter(const std::wstring& final_path,
                                   bool allow_overwrite)
    : final_path_(final_path), allow_overwrite_(allow_overwrite) {
    if (!allow_overwrite_ && file_exists(final_path_)) {
        throw FileError("refusing to overwrite existing file: " +
                        wide_to_utf8(final_path_));
    }

    // The temporary lives beside the destination so rename() stays within one
    // filesystem, which is what makes it atomic.
    temp_path_ = final_path_ + L"." + random_hex_name(8) + L".tmp";

    const std::string p = narrow(temp_path_);
    const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        throw FileError("cannot create output file: " +
                        std::string(std::strerror(errno)));
    }
    handle_ = reinterpret_cast<void*>(static_cast<std::intptr_t>(fd));
}

AtomicFileWriter::~AtomicFileWriter() {
    if (!published_) {
        rollback();
    }
}

void AtomicFileWriter::rollback() noexcept {
    if (handle_ != nullptr) {
        ::close(static_cast<int>(reinterpret_cast<std::intptr_t>(handle_)));
        handle_ = nullptr;
    }
    if (!temp_path_.empty()) {
        ::unlink(narrow(temp_path_).c_str());
    }
}

void AtomicFileWriter::write(const std::uint8_t* data, std::size_t n) {
    const int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(handle_));
    std::size_t total = 0;
    while (total < n) {
        const ssize_t put = ::write(fd, data + total, n - total);
        if (put < 0) {
            if (errno == EINTR) continue;
            throw_errno("write failed");
        }
        if (put == 0) {
            throw FileError("write made no progress (disk full?)");
        }
        total += static_cast<std::size_t>(put);
    }
}

void AtomicFileWriter::finalize() {
    if (finalized_) return;

    const int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(handle_));
    if (::fsync(fd) != 0) {
        throw_errno("flush failed");
    }
    ::close(fd);
    handle_ = nullptr;
    finalized_ = true;
}

void AtomicFileWriter::finalize(const format::Metadata& metadata) {
    if (finalized_) return;

    const int fd = static_cast<int>(reinterpret_cast<std::intptr_t>(handle_));
    if (::fsync(fd) != 0) {
        throw_errno("flush failed");
    }

    // Timestamps have to be set after the last write, since writing is itself
    // what moves them. Access time first, then modification time.
    timespec times[2];
    times[0] = from_filetime(metadata.last_access_time);
    times[1] = from_filetime(metadata.last_write_time);
    if (::futimens(fd, times) != 0) {
        // A filesystem that refuses timestamps should not fail the whole
        // operation; the contents are already safe.
    }

    // Windows attributes have no POSIX equivalent beyond read-only, so a
    // container from Windows still yields a sensible mode.
    if ((metadata.meta_flags & format::META_HAS_POSIX_MODE) != 0) {
        pending_attributes_ = metadata.posix_mode & 07777u;
    } else if ((metadata.meta_flags & format::META_HAS_WIN_ATTRIBUTES) != 0) {
        constexpr std::uint32_t kWinReadOnly = 0x1;
        pending_attributes_ = format::posix_mode_for_readonly(
            (metadata.file_attributes & kWinReadOnly) != 0);
    } else {
        pending_attributes_ = 0;
    }

    ::close(fd);
    handle_ = nullptr;
    finalized_ = true;
}

void AtomicFileWriter::publish() {
    if (published_) return;
    if (!finalized_) {
        finalize();
    }

    const std::string from = narrow(temp_path_);
    const std::string to   = narrow(final_path_);

    // rename() replaces silently, so the no-overwrite promise is enforced here
    // as well as at construction -- the destination may have appeared since.
    if (!allow_overwrite_ && file_exists(final_path_)) {
        throw FileError("refusing to overwrite existing file: " +
                        wide_to_utf8(final_path_));
    }
    if (::rename(from.c_str(), to.c_str()) != 0) {
        throw_errno("cannot move output into place");
    }
    published_ = true;

    // Permissions go on last: an earlier chmod to a read-only mode would stop
    // us writing the contents.
    if (pending_attributes_ != 0) {
        ::chmod(to.c_str(), static_cast<mode_t>(pending_attributes_));
    }
}

}  // namespace seicure::platform
