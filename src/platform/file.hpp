#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "core/format.hpp"
#include "core/stream.hpp"

namespace seicure::platform {

class FileError : public std::runtime_error {
public:
    explicit FileError(const std::string& what) : std::runtime_error(what) {}
};

// wchar_t and char16_t are both UTF-16 on Windows, but they are distinct types.
std::wstring   to_wide(const std::u16string& s);
std::u16string to_u16(const std::wstring& s);
std::string    wide_to_utf8(const std::wstring& s);
std::wstring   utf8_to_wide(const std::string& s);

std::wstring base_name(const std::wstring& path);
std::wstring parent_dir(const std::wstring& path);
std::wstring join_path(const std::wstring& dir, const std::wstring& name);
bool         file_exists(const std::wstring& path);
// Zero when the path is missing or is not a regular file. Callers that need to
// tell "empty" from "absent" should ask file_exists as well.
std::uint64_t file_size(const std::wstring& path);
bool         directory_exists(const std::wstring& path);

// Output names are random so the container itself reveals nothing. Encrypting
// the filename inside is pointless if the file on disk is still called
// "layoffs.xlsx.sef".
std::wstring random_hex_name(std::size_t random_bytes = 16);

class FileReader final : public stream::Reader {
public:
    explicit FileReader(const std::wstring& path);
    ~FileReader() override;

    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    std::size_t read(std::uint8_t* out, std::size_t n) override;

    [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
    // Name, timestamps and attributes captured at open time.
    [[nodiscard]] const format::Metadata& metadata() const noexcept {
        return metadata_;
    }

private:
    void*            handle_ = nullptr;
    std::uint64_t    size_ = 0;
    format::Metadata metadata_;
};

// Writes through a temporary file in the destination directory and only swaps
// it into place on commit(). An error, a cancel, or a crash leaves the
// destination untouched -- never half a file where the original used to be.
class AtomicFileWriter final : public stream::Writer {
public:
    AtomicFileWriter(const std::wstring& final_path, bool allow_overwrite);
    ~AtomicFileWriter() override;  // rolls back when commit() was not called

    AtomicFileWriter(const AtomicFileWriter&) = delete;
    AtomicFileWriter& operator=(const AtomicFileWriter&) = delete;

    void write(const std::uint8_t* data, std::size_t n) override;

    // Finalizing and publishing are separate so a caller extracting several
    // files can withhold every one of them until the whole container has
    // authenticated. Committing as each entry completes would leave a partial
    // extraction on disk when a later chunk turns out to be corrupt.
    //
    // finalize() flushes and closes the handle; the data still lives only in
    // the temporary file.
    void finalize();
    void finalize(const format::Metadata& metadata);  // also restores timestamps

    // Moves the temporary into place and applies saved attributes.
    void publish();

    void commit() { finalize(); publish(); }
    void commit(const format::Metadata& metadata) {
        finalize(metadata);
        publish();
    }

    [[nodiscard]] const std::wstring& path() const noexcept { return final_path_; }

private:
    void rollback() noexcept;

    void*         handle_ = nullptr;
    std::wstring  final_path_;
    std::wstring  temp_path_;
    bool          allow_overwrite_ = false;
    bool          finalized_ = false;
    bool          published_ = false;
    std::uint32_t pending_attributes_ = 0;
};

}  // namespace seicure::platform
