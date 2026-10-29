#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/archive.hpp"
#include "core/stream.hpp"
#include "test_util.hpp"

using namespace seicure;

namespace {

class MemoryReader final : public stream::Reader {
public:
    explicit MemoryReader(const std::vector<std::uint8_t>& data) : data_(data) {}
    std::size_t read(std::uint8_t* out, std::size_t n) override {
        const std::size_t take = (std::min)(n, data_.size() - pos_);
        if (take > 0) {
            std::memcpy(out, data_.data() + pos_, take);
            pos_ += take;
        }
        return take;
    }

private:
    const std::vector<std::uint8_t>& data_;
    std::size_t pos_ = 0;
};

class MemoryWriter final : public stream::Writer {
public:
    void write(const std::uint8_t* data, std::size_t n) override {
        bytes.insert(bytes.end(), data, data + n);
    }
    std::vector<std::uint8_t> bytes;
};

// Source backed by in-memory blobs.
class VecSource final : public archive::EntrySource {
public:
    void add(const std::u16string& name, std::vector<std::uint8_t> data) {
        format::Metadata m;
        m.original_name   = name;
        m.original_size   = data.size();
        m.creation_time   = 132500000000000000ULL + entries_.size();
        m.last_write_time = 132600000000000000ULL + entries_.size();
        m.file_attributes = 0x20;
        entries_.push_back({m, std::move(data)});
    }

    std::size_t count() const override { return entries_.size(); }

    const format::Metadata& metadata(std::size_t i) const override {
        return entries_[i].meta;
    }

    stream::Reader& open(std::size_t i) override {
        current_ = std::make_unique<MemoryReader>(entries_[i].data);
        return *current_;
    }

private:
    struct Entry {
        format::Metadata          meta;
        std::vector<std::uint8_t> data;
    };
    std::vector<Entry>            entries_;
    std::unique_ptr<MemoryReader> current_;
};

// Sink collecting entries in memory.
class VecSink final : public archive::EntrySink {
public:
    struct Out {
        format::Metadata          meta;
        std::vector<std::uint8_t> data;
    };

    stream::Writer& begin_entry(const format::Metadata& meta) override {
        results.push_back({meta, {}});
        // Index-based, so a reallocation of `results` cannot dangle.
        current_ = std::make_unique<IndexWriter>(results, results.size() - 1);
        return *current_;
    }

    void end_entry() override { current_.reset(); }

    std::vector<Out> results;

private:
    class IndexWriter final : public stream::Writer {
    public:
        IndexWriter(std::vector<Out>& results, std::size_t index)
            : results_(results), index_(index) {}
        void write(const std::uint8_t* data, std::size_t n) override {
            auto& dst = results_[index_].data;
            dst.insert(dst.end(), data, data + n);
        }

    private:
        std::vector<Out>& results_;
        std::size_t       index_;
    };

    std::unique_ptr<IndexWriter> current_;
};

stream::Options fast_options() {
    stream::Options o;
    o.kdf_params = kdf::Params{1024, 1, 1};
    o.chunk_size = 4096;
    return o;
}

const std::string kPassword = "archive-test-password";

const std::uint8_t* pw_bytes(const std::string& s) {
    return reinterpret_cast<const std::uint8_t*>(s.data());
}

std::vector<std::uint8_t> pattern(std::size_t n, std::uint8_t seed) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<std::uint8_t>((i * 31 + seed) & 0xFF);
    }
    return v;
}

// Packs a source, encrypts it, and returns the container bytes.
std::vector<std::uint8_t> seal(VecSource& source) {
    const std::uint64_t payload = archive::payload_size(source);

    format::Metadata container;
    container.content_type = format::ContentType::Archive;
    container.entry_count  = static_cast<std::uint32_t>(source.count());

    archive::PackReader pack(source);
    MemoryWriter out;
    stream::encrypt(pack, payload, container, pw_bytes(kPassword),
                    kPassword.size(), out, fast_options());
    return out.bytes;
}

// Decrypts and unpacks a container into a sink.
format::Metadata unseal(const std::vector<std::uint8_t>& container,
                        VecSink& sink) {
    MemoryReader in(container);
    archive::UnpackWriter unpacker(sink);
    const format::Metadata meta = stream::decrypt(
        in, pw_bytes(kPassword), kPassword.size(), unpacker);
    unpacker.finish(meta.entry_count);
    return meta;
}

}  // namespace

// ---------------------------------------------------------------------------
// Entry-name safety
// ---------------------------------------------------------------------------

TEST(archive_rejects_path_traversal_names) {
    using archive::is_safe_entry_name;

    CHECK(is_safe_entry_name(u"report.txt"));
    CHECK(is_safe_entry_name(u"보고서 (최종).hwp"));
    CHECK(is_safe_entry_name(u"a.b.c.tar.gz"));

    // An archive is attacker-controlled once it leaves the machine. These are
    // exactly the shapes that would write outside the extraction directory.
    CHECK(!is_safe_entry_name(u""));
    CHECK(!is_safe_entry_name(u"."));
    CHECK(!is_safe_entry_name(u".."));
    CHECK(!is_safe_entry_name(u"..\\..\\Windows\\System32\\evil.dll"));
    CHECK(!is_safe_entry_name(u"../../etc/passwd"));
    CHECK(!is_safe_entry_name(u"sub\\dir.txt"));
    CHECK(!is_safe_entry_name(u"C:evil.exe"));
    CHECK(!is_safe_entry_name(u"a*.txt"));
    CHECK(!is_safe_entry_name(u"a?.txt"));
    CHECK(!is_safe_entry_name(u"a|b.txt"));
    CHECK(!is_safe_entry_name(std::u16string(u"nul\0byte.txt", 12)));

    // Win32 strips these silently, which would defeat a name check made
    // anywhere upstream.
    CHECK(!is_safe_entry_name(u"evil.exe "));
    CHECK(!is_safe_entry_name(u"evil.exe."));

    CHECK(!is_safe_entry_name(std::u16string(format::MAX_NAME_UNITS + 1, u'x')));
}

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

TEST(archive_round_trips_multiple_files) {
    VecSource source;
    source.add(u"first.txt", pattern(100, 1));
    source.add(u"보고서.hwp", pattern(9000, 2));
    source.add(u"empty.dat", {});
    source.add(u"large.bin", pattern(40000, 3));

    const auto container = seal(source);

    VecSink sink;
    const format::Metadata meta = unseal(container, sink);

    CHECK(meta.content_type == format::ContentType::Archive);
    CHECK(meta.entry_count == 4);
    CHECK(sink.results.size() == 4);

    CHECK(sink.results[0].meta.original_name == u"first.txt");
    CHECK(sink.results[0].data == pattern(100, 1));
    CHECK(sink.results[1].meta.original_name == u"보고서.hwp");
    CHECK(sink.results[1].data == pattern(9000, 2));
    CHECK(sink.results[2].meta.original_name == u"empty.dat");
    CHECK(sink.results[2].data.empty());
    CHECK(sink.results[3].meta.original_name == u"large.bin");
    CHECK(sink.results[3].data == pattern(40000, 3));

    // Per-entry timestamps and attributes survive too.
    for (std::size_t i = 0; i < sink.results.size(); ++i) {
        CHECK(sink.results[i].meta.creation_time ==
              132500000000000000ULL + i);
        CHECK(sink.results[i].meta.last_write_time ==
              132600000000000000ULL + i);
        CHECK(sink.results[i].meta.file_attributes == 0x20);
    }
}

TEST(archive_round_trips_single_entry) {
    VecSource source;
    source.add(u"only.txt", pattern(5000, 9));

    VecSink sink;
    const auto meta = unseal(seal(source), sink);
    CHECK(meta.entry_count == 1);
    CHECK(sink.results.size() == 1);
    CHECK(sink.results[0].data == pattern(5000, 9));
}

TEST(archive_payload_size_is_exact) {
    VecSource source;
    source.add(u"a", pattern(10, 1));
    source.add(u"b", pattern(20, 2));
    source.add(u"c", {});

    const std::uint64_t expected = 3 * format::METADATA_SIZE + 10 + 20 + 0;
    CHECK(archive::payload_size(source) == expected);

    // The packed stream really does produce exactly that many bytes.
    archive::PackReader pack(source);
    MemoryWriter out;
    std::uint8_t buffer[512];
    while (true) {
        const std::size_t got = pack.read(buffer, sizeof(buffer));
        if (got == 0) break;
        out.write(buffer, got);
    }
    CHECK(out.bytes.size() == expected);
}

TEST(archive_hides_entry_names_and_count) {
    VecSource source;
    source.add(u"TAKEOVER_PLAN.docx", pattern(1000, 1));
    source.add(u"salaries.xlsx", pattern(1000, 2));

    const auto container = seal(source);

    for (const char* name : {"TAKEOVER_PLAN.docx", "salaries.xlsx"}) {
        const std::string ascii = name;
        CHECK(std::search(container.begin(), container.end(), ascii.begin(),
                          ascii.end()) == container.end());

        std::vector<std::uint8_t> utf16;
        for (char c : ascii) {
            utf16.push_back(static_cast<std::uint8_t>(c));
            utf16.push_back(0);
        }
        CHECK(std::search(container.begin(), container.end(), utf16.begin(),
                          utf16.end()) == container.end());
    }
}

TEST(archive_and_single_file_containers_are_indistinguishable) {
    // A three-file archive and a single file of the same payload size must
    // produce byte-identical container sizes -- the entry count lives inside
    // the ciphertext, so an observer cannot tell them apart.
    VecSource source;
    source.add(u"a.txt", pattern(1000, 1));
    source.add(u"b.txt", pattern(1000, 2));
    const std::uint64_t payload = archive::payload_size(source);

    const auto archived = seal(source);

    const auto blob = pattern(static_cast<std::size_t>(payload), 7);
    MemoryReader in(blob);
    MemoryWriter out;
    format::Metadata single;
    single.original_name = u"whatever.bin";
    stream::encrypt(in, blob.size(), single, pw_bytes(kPassword),
                    kPassword.size(), out, fast_options());

    CHECK(archived.size() == out.bytes.size());
}

// ---------------------------------------------------------------------------
// Malformed archives
// ---------------------------------------------------------------------------

TEST(archive_rejects_entry_count_mismatch) {
    VecSource source;
    source.add(u"a.txt", pattern(100, 1));
    source.add(u"b.txt", pattern(100, 2));

    const auto container = seal(source);

    MemoryReader in(container);
    VecSink sink;
    archive::UnpackWriter unpacker(sink);
    const format::Metadata meta = stream::decrypt(
        in, pw_bytes(kPassword), kPassword.size(), unpacker);
    CHECK(meta.entry_count == 2);

    // Claiming a different count than the payload delivered must be caught.
    bool threw = false;
    try {
        unpacker.finish(3);
    } catch (const archive::ArchiveError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(archive_rejects_truncated_entry) {
    // A payload cut mid-entry must not look like a complete archive.
    VecSink sink;
    archive::UnpackWriter unpacker(sink);

    format::Metadata entry;
    entry.original_name = u"a.txt";
    entry.original_size = 100;
    std::uint8_t header[format::METADATA_SIZE];
    entry.serialize(header);

    unpacker.write(header, sizeof(header));
    const auto partial = pattern(40, 1);  // 40 of the promised 100 bytes
    unpacker.write(partial.data(), partial.size());

    bool threw = false;
    try {
        unpacker.finish(1);
    } catch (const archive::ArchiveError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(archive_rejects_unsafe_entry_name_on_extraction) {
    VecSink sink;
    archive::UnpackWriter unpacker(sink);

    format::Metadata evil;
    evil.original_name = u"..\\..\\evil.dll";
    evil.original_size = 0;
    std::uint8_t header[format::METADATA_SIZE];
    evil.serialize(header);

    bool threw = false;
    try {
        unpacker.write(header, sizeof(header));
    } catch (const archive::ArchiveError&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(sink.results.empty());  // nothing was opened for writing
}

TEST(archive_rejects_nested_archive_entry) {
    VecSink sink;
    archive::UnpackWriter unpacker(sink);

    format::Metadata nested;
    nested.original_name = u"inner.sef";
    nested.original_size = 0;
    nested.content_type  = format::ContentType::Archive;
    std::uint8_t header[format::METADATA_SIZE];
    nested.serialize(header);

    bool threw = false;
    try {
        unpacker.write(header, sizeof(header));
    } catch (const archive::ArchiveError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(archive_handles_headers_split_across_writes) {
    // stream::decrypt hands data over in chunk-sized pieces, so an entry header
    // routinely straddles two write() calls.
    VecSink sink;
    archive::UnpackWriter unpacker(sink);

    format::Metadata entry;
    entry.original_name = u"split.bin";
    entry.original_size = 10;
    std::uint8_t header[format::METADATA_SIZE];
    entry.serialize(header);

    const auto body = pattern(10, 5);
    std::vector<std::uint8_t> payload(header, header + sizeof(header));
    payload.insert(payload.end(), body.begin(), body.end());

    // Feed it one byte at a time -- the worst case for the state machine.
    for (std::uint8_t b : payload) {
        unpacker.write(&b, 1);
    }
    unpacker.finish(1);

    CHECK(sink.results.size() == 1);
    CHECK(sink.results[0].meta.original_name == u"split.bin");
    CHECK(sink.results[0].data == body);
}
