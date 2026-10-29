#include "platform/file.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "core/crypto.hpp"

namespace seicure::platform {
namespace {

std::string last_error_message(const char* what, DWORD code) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), " (error %lu)",
                  static_cast<unsigned long>(code));
    return std::string(what) + buffer;
}

[[noreturn]] void throw_last_error(const char* what) {
    throw FileError(last_error_message(what, GetLastError()));
}

std::uint64_t to_u64(const FILETIME& ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) |
           ft.dwLowDateTime;
}

FILETIME from_u64(std::uint64_t v) {
    FILETIME ft;
    ft.dwLowDateTime  = static_cast<DWORD>(v & 0xFFFFFFFFu);
    ft.dwHighDateTime = static_cast<DWORD>(v >> 32);
    return ft;
}

// Win32 caps ordinary paths at MAX_PATH; the \\?\ prefix lifts that. It only
// works on absolute paths and disables forward-slash translation.
std::wstring extended(const std::wstring& path) {
    if (path.size() < MAX_PATH) {
        return path;
    }
    if (path.rfind(LR"(\\?\)", 0) == 0) {
        return path;
    }
    if (path.size() > 2 && path[1] == L':') {
        return LR"(\\?\)" + path;
    }
    return path;
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

std::string wide_to_utf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int needed = WideCharToMultiByte(CP_UTF8, 0, s.data(),
                                           static_cast<int>(s.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) throw FileError("WideCharToMultiByte failed");
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                           static_cast<int>(s.size()),
                                           nullptr, 0);
    if (needed <= 0) throw FileError("MultiByteToWideChar failed");
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), needed);
    return out;
}

std::wstring base_name(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

std::wstring parent_dir(const std::wstring& path) {
    const std::size_t at = path.find_last_of(L"\\/");
    if (at == std::wstring::npos) return L".";
    if (at == 0) return L"\\";
    return path.substr(0, at);
}

std::wstring join_path(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    const wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') return dir + name;
    return dir + L"\\" + name;
}

bool file_exists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(extended(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::uint64_t file_size(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(extended(path).c_str(), GetFileExInfoStandard,
                              &info)) {
        return 0;
    }
    return (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) |
           info.nFileSizeLow;
}

bool directory_exists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(extended(path).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
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
    handle_ = CreateFileW(extended(path).c_str(), GENERIC_READ, FILE_SHARE_READ,
                          nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                          nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        handle_ = nullptr;
        throw FileError(last_error_message("cannot open input file",
                                           GetLastError()));
    }

    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle_, &info)) {
        const DWORD code = GetLastError();
        CloseHandle(handle_);
        handle_ = nullptr;
        throw FileError(last_error_message("cannot stat input file", code));
    }

    size_ = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) |
            info.nFileSizeLow;

    metadata_.original_size    = size_;
    metadata_.creation_time    = to_u64(info.ftCreationTime);
    metadata_.last_write_time  = to_u64(info.ftLastWriteTime);
    metadata_.last_access_time = to_u64(info.ftLastAccessTime);
    metadata_.file_attributes  = info.dwFileAttributes;
    metadata_.content_type     = format::ContentType::File;
    metadata_.original_name    = to_u16(base_name(path));

    // Record a POSIX equivalent as well, so a container written here restores
    // with sensible permissions when it is opened on macOS or Linux.
    metadata_.meta_flags = format::META_HAS_WIN_ATTRIBUTES |
                           format::META_HAS_POSIX_MODE;
    metadata_.posix_mode = format::posix_mode_for_readonly(
        (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0);
}

FileReader::~FileReader() {
    if (handle_ != nullptr) {
        CloseHandle(handle_);
    }
}

std::size_t FileReader::read(std::uint8_t* out, std::size_t n) {
    std::size_t total = 0;
    while (total < n) {
        const DWORD want = static_cast<DWORD>(
            (std::min)(n - total, static_cast<std::size_t>(1u << 20)));
        DWORD got = 0;
        if (!ReadFile(handle_, out + total, want, &got, nullptr)) {
            throw_last_error("read failed");
        }
        if (got == 0) break;  // end of file
        total += got;
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

    // The temporary lives beside the destination so the final move stays on one
    // volume, which is what makes it atomic.
    temp_path_ = final_path_ + L"." + random_hex_name(8) + L".tmp";

    handle_ = CreateFileW(extended(temp_path_).c_str(), GENERIC_WRITE, 0,
                          nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        handle_ = nullptr;
        throw FileError(last_error_message("cannot create output file",
                                           GetLastError()));
    }
}

AtomicFileWriter::~AtomicFileWriter() {
    if (!published_) {
        rollback();
    }
}

void AtomicFileWriter::rollback() noexcept {
    if (handle_ != nullptr) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    if (!temp_path_.empty()) {
        DeleteFileW(extended(temp_path_).c_str());
    }
}

void AtomicFileWriter::write(const std::uint8_t* data, std::size_t n) {
    std::size_t total = 0;
    while (total < n) {
        const DWORD want = static_cast<DWORD>(
            (std::min)(n - total, static_cast<std::size_t>(1u << 20)));
        DWORD put = 0;
        if (!WriteFile(handle_, data + total, want, &put, nullptr)) {
            throw_last_error("write failed");
        }
        if (put == 0) {
            throw FileError("write made no progress (disk full?)");
        }
        total += put;
    }
}

void AtomicFileWriter::finalize() {
    if (finalized_) return;

    if (!FlushFileBuffers(handle_)) {
        throw_last_error("flush failed");
    }
    CloseHandle(handle_);
    handle_ = nullptr;
    finalized_ = true;
}

void AtomicFileWriter::finalize(const format::Metadata& metadata) {
    if (finalized_) return;

    if (!FlushFileBuffers(handle_)) {
        throw_last_error("flush failed");
    }

    // Timestamps have to be set while the handle is still open, and after all
    // content is written -- writing is itself what moves the access and write
    // times, so doing this any earlier would be undone.
    if (metadata.creation_time != 0 || metadata.last_write_time != 0 ||
        metadata.last_access_time != 0) {
        const FILETIME creation = from_u64(metadata.creation_time);
        const FILETIME access   = from_u64(metadata.last_access_time);
        const FILETIME write    = from_u64(metadata.last_write_time);
        if (!SetFileTime(handle_,
                         metadata.creation_time ? &creation : nullptr,
                         metadata.last_access_time ? &access : nullptr,
                         metadata.last_write_time ? &write : nullptr)) {
            throw_last_error("cannot restore timestamps");
        }
    }

    // Directory and reparse bits describe the original object, not this freshly
    // written file; carrying them over would be wrong. Read-only in particular
    // can only go on after the move, or the move itself fails.
    if ((metadata.meta_flags & format::META_HAS_WIN_ATTRIBUTES) != 0) {
        pending_attributes_ = metadata.file_attributes &
                              ~static_cast<std::uint32_t>(
                                  FILE_ATTRIBUTE_DIRECTORY |
                                  FILE_ATTRIBUTE_REPARSE_POINT |
                                  FILE_ATTRIBUTE_SPARSE_FILE |
                                  FILE_ATTRIBUTE_COMPRESSED |
                                  FILE_ATTRIBUTE_ENCRYPTED);
    } else if ((metadata.meta_flags & format::META_HAS_POSIX_MODE) != 0) {
        // Written on a POSIX system: the only attribute we can honestly
        // reconstruct is read-only.
        pending_attributes_ =
            format::posix_mode_is_readonly(metadata.posix_mode)
                ? FILE_ATTRIBUTE_READONLY
                : 0;
    } else {
        pending_attributes_ = 0;
    }

    CloseHandle(handle_);
    handle_ = nullptr;
    finalized_ = true;
}

void AtomicFileWriter::publish() {
    if (published_) return;
    if (!finalized_) {
        finalize();
    }

    const DWORD flags = allow_overwrite_ ? MOVEFILE_REPLACE_EXISTING : 0;
    if (!MoveFileExW(extended(temp_path_).c_str(),
                     extended(final_path_).c_str(), flags)) {
        throw_last_error("cannot move output into place");
    }
    published_ = true;

    if (pending_attributes_ != 0) {
        SetFileAttributesW(extended(final_path_).c_str(),
                           static_cast<DWORD>(pending_attributes_));
    }
}

}  // namespace seicure::platform
