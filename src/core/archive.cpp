#include "core/archive.hpp"

#include <algorithm>
#include <cstring>

namespace seicure::archive {

bool is_safe_entry_name(const std::u16string& name) {
    if (name.empty() || name.size() > format::MAX_NAME_UNITS) {
        return false;
    }
    if (name == u"." || name == u"..") {
        return false;
    }
    for (const char16_t c : name) {
        // Path separators and the drive-letter colon are what make traversal
        // possible; the rest are simply invalid in an NTFS name.
        if (c == u'\\' || c == u'/' || c == u':' || c == u'*' || c == u'?' ||
            c == u'"' || c == u'<' || c == u'>' || c == u'|') {
            return false;
        }
        if (c < 0x20) {
            return false;
        }
    }
    // Trailing dots and spaces are silently stripped by Win32, which would let
    // "evil.exe " resolve to "evil.exe" after passing a name check elsewhere.
    if (name.back() == u'.' || name.back() == u' ') {
        return false;
    }
    return true;
}

std::uint64_t payload_size(const EntrySource& source) {
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < source.count(); ++i) {
        total += format::METADATA_SIZE + source.metadata(i).original_size;
    }
    return total;
}

// ---------------------------------------------------------------------------
// PackReader
// ---------------------------------------------------------------------------

PackReader::PackReader(EntrySource& source) : source_(source) {
    if (source.count() > MAX_ENTRIES) {
        throw ArchiveError("too many entries");
    }
}

std::size_t PackReader::read(std::uint8_t* out, std::size_t n) {
    std::size_t produced = 0;

    while (produced < n && state_ != State::Done) {
        switch (state_) {
        case State::NeedHeader: {
            if (entry_ >= source_.count()) {
                state_ = State::Done;
                break;
            }
            format::Metadata meta = source_.metadata(entry_);
            meta.content_type = format::ContentType::File;  // no nested archives
            meta.serialize(header_);
            data_left_  = meta.original_size;
            reader_     = &source_.open(entry_);
            header_pos_ = 0;
            state_      = State::EmitHeader;
            break;
        }

        case State::EmitHeader: {
            const std::size_t take =
                (std::min)(n - produced, format::METADATA_SIZE - header_pos_);
            std::memcpy(out + produced, header_ + header_pos_, take);
            header_pos_ += take;
            produced    += take;
            if (header_pos_ == format::METADATA_SIZE) {
                state_ = (data_left_ == 0) ? State::NeedHeader : State::EmitData;
                if (state_ == State::NeedHeader) ++entry_;
            }
            break;
        }

        case State::EmitData: {
            const std::size_t want = static_cast<std::size_t>(
                (std::min)(static_cast<std::uint64_t>(n - produced), data_left_));
            const std::size_t got = reader_->read(out + produced, want);
            if (got == 0) {
                // The declared size came from a stat() that no longer matches
                // the file. Zero-filling here would silently corrupt the entry.
                throw ArchiveError("entry shrank while being read");
            }
            produced   += got;
            data_left_ -= got;
            if (data_left_ == 0) {
                ++entry_;
                state_ = State::NeedHeader;
            }
            break;
        }

        case State::Done:
            break;
        }
    }

    return produced;
}

// ---------------------------------------------------------------------------
// UnpackWriter
// ---------------------------------------------------------------------------

UnpackWriter::UnpackWriter(EntrySink& sink) : sink_(sink) {}

void UnpackWriter::complete_entry() {
    sink_.end_entry();
    writer_ = nullptr;
    ++entries_done_;
    header_pos_ = 0;
    state_ = State::NeedHeader;
}

void UnpackWriter::write(const std::uint8_t* data, std::size_t n) {
    std::size_t offset = 0;

    while (offset < n) {
        if (entries_done_ >= MAX_ENTRIES) {
            throw ArchiveError("too many entries in archive");
        }

        if (state_ == State::NeedHeader) {
            const std::size_t take =
                (std::min)(n - offset, format::METADATA_SIZE - header_pos_);
            std::memcpy(header_ + header_pos_, data + offset, take);
            header_pos_ += take;
            offset      += take;

            if (header_pos_ < format::METADATA_SIZE) {
                continue;  // header spans another write
            }

            const format::Metadata meta = format::Metadata::parse(header_);
            if (meta.content_type != format::ContentType::File) {
                throw ArchiveError("nested archives are not supported");
            }
            if (!is_safe_entry_name(meta.original_name)) {
                throw ArchiveError("unsafe entry name in archive");
            }

            writer_    = &sink_.begin_entry(meta);
            data_left_ = meta.original_size;
            if (data_left_ == 0) {
                complete_entry();
            } else {
                state_ = State::WriteData;
            }
            continue;
        }

        // State::WriteData
        const std::size_t take = static_cast<std::size_t>(
            (std::min)(static_cast<std::uint64_t>(n - offset), data_left_));
        writer_->write(data + offset, take);
        offset     += take;
        data_left_ -= take;
        if (data_left_ == 0) {
            complete_entry();
        }
    }
}

void UnpackWriter::finish(std::uint32_t expected_entries) {
    // A payload that stops mid-header or mid-entry leaves state behind.
    if (state_ != State::NeedHeader || header_pos_ != 0 || data_left_ != 0) {
        throw ArchiveError("archive ended mid-entry");
    }
    if (entries_done_ != expected_entries) {
        throw ArchiveError("archive entry count does not match its metadata");
    }
}

}  // namespace seicure::archive
