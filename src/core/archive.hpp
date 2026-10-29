#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "core/format.hpp"
#include "core/stream.hpp"

// Archive layer: packs several files into the single byte stream that the
// encryption engine consumes, so a multi-file container looks exactly like a
// single-file one from the outside. Entry count, names and individual sizes
// all live inside the ciphertext.
//
// Payload layout:  [Metadata 1024][data] [Metadata 1024][data] ...
//
// Each entry header reuses format::Metadata, so one serializer covers both the
// container and its entries.
namespace seicure::archive {

class ArchiveError : public std::runtime_error {
public:
    explicit ArchiveError(const std::string& what) : std::runtime_error(what) {}
};

// Refuses anything that could escape the extraction directory or is invalid on
// Windows. An archive is attacker-controlled input once it leaves your machine,
// and "..\..\Windows\System32\x.dll" as an entry name is the classic attack.
[[nodiscard]] bool is_safe_entry_name(const std::u16string& name);

// Hard ceiling so a corrupt or hostile entry_count cannot drive an unbounded
// loop before anything is written.
inline constexpr std::uint32_t MAX_ENTRIES = 1'000'000;

// ---------------------------------------------------------------------------
// Packing
// ---------------------------------------------------------------------------

class EntrySource {
public:
    virtual ~EntrySource() = default;
    [[nodiscard]] virtual std::size_t count() const = 0;
    [[nodiscard]] virtual const format::Metadata& metadata(std::size_t i) const = 0;
    // Opens entry `i` for reading. The returned reference stays valid until the
    // next call to open().
    [[nodiscard]] virtual stream::Reader& open(std::size_t i) = 0;
};

// Total payload bytes the packed stream will produce.
[[nodiscard]] std::uint64_t payload_size(const EntrySource& source);

class PackReader final : public stream::Reader {
public:
    explicit PackReader(EntrySource& source);
    std::size_t read(std::uint8_t* out, std::size_t n) override;

private:
    enum class State { NeedHeader, EmitHeader, EmitData, Done };

    EntrySource&   source_;
    State          state_ = State::NeedHeader;
    std::size_t    entry_ = 0;
    std::uint8_t   header_[format::METADATA_SIZE]{};
    std::size_t    header_pos_ = 0;
    std::uint64_t  data_left_ = 0;
    stream::Reader* reader_ = nullptr;
};

// ---------------------------------------------------------------------------
// Unpacking
// ---------------------------------------------------------------------------

class EntrySink {
public:
    virtual ~EntrySink() = default;
    // Called once per entry, before any of its bytes arrive.
    [[nodiscard]] virtual stream::Writer& begin_entry(const format::Metadata& meta) = 0;
    // Called once the entry's declared byte count has been delivered.
    virtual void end_entry() = 0;
};

class UnpackWriter final : public stream::Writer {
public:
    explicit UnpackWriter(EntrySink& sink);
    void write(const std::uint8_t* data, std::size_t n) override;

    // Confirms the payload ended on an entry boundary and produced exactly the
    // number of entries the container's authenticated metadata declared.
    // A payload that stops mid-entry would otherwise pass unnoticed.
    void finish(std::uint32_t expected_entries);

    [[nodiscard]] std::uint32_t entries_seen() const noexcept {
        return entries_done_;
    }

private:
    enum class State { NeedHeader, WriteData };
    void complete_entry();

    EntrySink&      sink_;
    std::uint32_t   entries_done_ = 0;
    State           state_ = State::NeedHeader;
    std::uint8_t    header_[format::METADATA_SIZE]{};
    std::size_t     header_pos_ = 0;
    std::uint64_t   data_left_ = 0;
    stream::Writer* writer_ = nullptr;
};

}  // namespace seicure::archive
