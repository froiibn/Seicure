#include "job/job.hpp"

#include <cwctype>
#include <memory>
#include <set>

#include "core/archive.hpp"
#include "core/stream.hpp"
#include "platform/file.hpp"

namespace seicure::job {
namespace {

constexpr const wchar_t* kExtension = L".sef";

std::wstring lowercased(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return out;
}

std::wstring pick_output_path(const Options& options,
                              const std::wstring& reference) {
    const std::wstring dir = options.out_dir.empty()
                                 ? platform::parent_dir(reference)
                                 : options.out_dir;
    // Random name: encrypting the filename inside the container is pointless if
    // the container on disk is still called "layoffs.xlsx.sef".
    for (int attempt = 0; attempt < 8; ++attempt) {
        const std::wstring candidate =
            platform::join_path(dir, platform::random_hex_name() + kExtension);
        if (!platform::file_exists(candidate)) {
            return candidate;
        }
    }
    throw platform::FileError("could not find an unused output name");
}

// Bridges job-level progress (percent + label) onto the engine's byte counts.
stream::ProgressFn make_progress(const ProgressFn& progress,
                                 const std::wstring& label) {
    if (!progress) {
        return {};
    }
    return [&progress, label](std::uint64_t done, std::uint64_t total) {
        const int percent =
            total == 0 ? 100 : static_cast<int>((done * 100) / total);
        return progress(percent, label);
    };
}

// --- packing ----------------------------------------------------------------

class FileEntrySource final : public archive::EntrySource {
public:
    explicit FileEntrySource(const std::vector<std::wstring>& paths) {
        for (const auto& path : paths) {
            platform::FileReader probe(path);  // opens and stats, then closes
            const format::Metadata meta = probe.metadata();
            if (!archive::is_safe_entry_name(meta.original_name)) {
                throw archive::ArchiveError(
                    "cannot archive a file with an unusable name: " +
                    platform::wide_to_utf8(platform::base_name(path)));
            }
            paths_.push_back(path);
            metas_.push_back(meta);
        }
    }

    std::size_t count() const override { return paths_.size(); }

    const format::Metadata& metadata(std::size_t i) const override {
        return metas_[i];
    }

    stream::Reader& open(std::size_t i) override {
        current_ = std::make_unique<platform::FileReader>(paths_[i]);
        return *current_;
    }

private:
    std::vector<std::wstring>             paths_;
    std::vector<format::Metadata>         metas_;
    std::unique_ptr<platform::FileReader> current_;
};

// --- unpacking --------------------------------------------------------------

// Entries are written to temporaries and held there. Nothing is published until
// the whole container has authenticated, so a container that fails partway
// leaves no half-extracted set of files behind.
class FileEntrySink final : public archive::EntrySink {
public:
    FileEntrySink(std::wstring dir, bool force)
        : dir_(std::move(dir)), force_(force) {}

    stream::Writer& begin_entry(const format::Metadata& meta) override {
        const std::wstring name = platform::to_wide(meta.original_name);
        // Windows names are case-insensitive, so two entries differing only in
        // case would collide at publish time and leave a partial extraction.
        if (!seen_.insert(lowercased(name)).second) {
            throw archive::ArchiveError("duplicate entry name in archive");
        }
        pending_.push_back(std::make_unique<platform::AtomicFileWriter>(
            platform::join_path(dir_, name), force_));
        current_meta_ = meta;
        return *pending_.back();
    }

    void end_entry() override { pending_.back()->finalize(current_meta_); }

    // Called only once the container is fully verified.
    std::vector<std::wstring> publish_all() {
        std::vector<std::wstring> paths;
        for (auto& writer : pending_) {
            writer->publish();
            paths.push_back(writer->path());
        }
        return paths;
    }

private:
    std::wstring           dir_;
    bool                   force_;
    std::set<std::wstring> seen_;
    std::vector<std::unique_ptr<platform::AtomicFileWriter>> pending_;
    format::Metadata       current_meta_;
};

// Routes the decrypted payload to either a single file or an archive
// extractor, depending on what the authenticated metadata says.
class ContainerWriter final : public stream::Writer {
public:
    ContainerWriter(std::wstring dir, bool force)
        : dir_(std::move(dir)), force_(force) {}

    void begin(const format::Metadata& container) {
        if (container.content_type == format::ContentType::Archive) {
            sink_     = std::make_unique<FileEntrySink>(dir_, force_);
            unpacker_ = std::make_unique<archive::UnpackWriter>(*sink_);
            return;
        }
        if (!archive::is_safe_entry_name(container.original_name)) {
            throw archive::ArchiveError("container has an unsafe filename");
        }
        single_ = std::make_unique<platform::AtomicFileWriter>(
            platform::join_path(dir_,
                                platform::to_wide(container.original_name)),
            force_);
    }

    void write(const std::uint8_t* data, std::size_t n) override {
        if (unpacker_) {
            unpacker_->write(data, n);
        } else if (single_) {
            single_->write(data, n);
        }
    }

    // Runs only after stream::decrypt returned without throwing, which means
    // every chunk authenticated and no trailing data followed.
    void finish(const format::Metadata& container) {
        if (unpacker_) {
            unpacker_->finish(container.entry_count);
            written_ = sink_->publish_all();
            return;
        }
        single_->commit(container);
        written_.push_back(single_->path());
    }

    [[nodiscard]] const std::vector<std::wstring>& written() const {
        return written_;
    }

private:
    std::wstring                                dir_;
    bool                                        force_;
    std::unique_ptr<platform::AtomicFileWriter> single_;
    std::unique_ptr<FileEntrySink>              sink_;
    std::unique_ptr<archive::UnpackWriter>      unpacker_;
    std::vector<std::wstring>                   written_;
};

void report(const LogFn& log, Event kind, const std::wstring& text) {
    if (log) log(kind, text);
}

}  // namespace

std::wstring format_size(std::uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) {
        value /= 1024.0;
        ++unit;
    }
    // %ls, not %s. In a wide printf the standard reads %s as a *narrow* string,
    // so L"KB" gets walked as bytes and stops at the NUL inside 'K' -- printing
    // "48.8 K". MSVC quietly treats %s as wide; MinGW does not.
    wchar_t buffer[64];
    if (unit == 0) {
        swprintf(buffer, 64, L"%llu %ls",
                 static_cast<unsigned long long>(bytes), units[unit]);
    } else {
        swprintf(buffer, 64, L"%.1f %ls", value, units[unit]);
    }
    return buffer;
}

Result encrypt(const std::vector<std::wstring>& inputs,
               const std::uint8_t* password, std::size_t password_len,
               const Options& options,
               const LogFn& log, const ProgressFn& progress) {
    Result result;

    if (options.archive && !inputs.empty()) {
        try {
            FileEntrySource source(inputs);

            format::Metadata container;
            container.content_type = format::ContentType::Archive;
            container.entry_count  = static_cast<std::uint32_t>(source.count());

            const std::wstring output = pick_output_path(options, inputs.front());
            archive::PackReader pack(source);
            platform::AtomicFileWriter writer(output, options.force);

            stream::encrypt(pack, archive::payload_size(source), container,
                            password, password_len, writer, stream::Options{},
                            make_progress(progress, L"archive"));
            writer.commit();

            report(log, Event::Success,
                   std::to_wstring(source.count()) + L" files  ->  " +
                       platform::base_name(output));
            result.succeeded = 1;
        } catch (const stream::CancelledError&) {
            result.cancelled = true;
        } catch (const std::exception& e) {
            report(log, Event::Error, platform::utf8_to_wide(e.what()));
            result.failed = 1;
        }
        return result;
    }

    for (const auto& input : inputs) {
        try {
            platform::FileReader reader(input);
            const std::wstring output = pick_output_path(options, input);
            const std::wstring label  = platform::base_name(input);

            platform::AtomicFileWriter writer(output, options.force);
            stream::encrypt(reader, reader.size(), reader.metadata(),
                            password, password_len, writer, stream::Options{},
                            make_progress(progress, label));
            writer.commit();

            report(log, Event::Success,
                   label + L"  ->  " + platform::base_name(output));
            ++result.succeeded;
        } catch (const stream::CancelledError&) {
            result.cancelled = true;
            return result;
        } catch (const std::exception& e) {
            report(log, Event::Error, platform::base_name(input) + L": " +
                                          platform::utf8_to_wide(e.what()));
            ++result.failed;
        }
    }
    return result;
}

Result decrypt(const std::vector<std::wstring>& inputs,
               const std::uint8_t* password, std::size_t password_len,
               const Options& options,
               const LogFn& log, const ProgressFn& progress) {
    Result result;

    for (const auto& input : inputs) {
        try {
            platform::FileReader reader(input);
            const std::wstring dir = options.out_dir.empty()
                                         ? platform::parent_dir(input)
                                         : options.out_dir;
            const std::wstring label = platform::base_name(input);

            ContainerWriter out(dir, options.force);
            const format::Metadata container = stream::decrypt(
                reader, password, password_len, out,
                make_progress(progress, label),
                [&out](const format::Metadata& meta) { out.begin(meta); });
            out.finish(container);

            for (const auto& path : out.written()) {
                report(log, Event::Success,
                       label + L"  ->  " + platform::base_name(path));
            }
            ++result.succeeded;
        } catch (const stream::CancelledError&) {
            result.cancelled = true;
            return result;
        } catch (const std::exception& e) {
            report(log, Event::Error, platform::base_name(input) + L": " +
                                          platform::utf8_to_wide(e.what()));
            ++result.failed;
        }
    }
    return result;
}

}  // namespace seicure::job
