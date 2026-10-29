#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "core/stream.hpp"
#include "test_util.hpp"

using namespace seicure;

namespace {

class MemoryReader final : public stream::Reader {
public:
    explicit MemoryReader(const std::vector<std::uint8_t>& data) : data_(data) {}

    std::size_t read(std::uint8_t* out, std::size_t n) override {
        const std::size_t left = data_.size() - pos_;
        const std::size_t take = (std::min)(n, left);
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

// Deliberately cheap so the suite stays fast. Real defaults are 256 MiB / t=3.
stream::Options fast_options(std::uint32_t chunk = 4096) {
    stream::Options o;
    o.kdf_params = kdf::Params{1024, 1, 1};
    o.chunk_size = chunk;
    return o;
}

const std::string kPassword = "hunter2-correct-horse";

const std::uint8_t* pw_bytes(const std::string& s) {
    return reinterpret_cast<const std::uint8_t*>(s.data());
}

std::vector<std::uint8_t> pattern(std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = static_cast<std::uint8_t>((i * 7 + 13) & 0xFF);
    }
    return v;
}

format::Metadata sample_metadata(const std::u16string& name = u"secret.txt") {
    format::Metadata m;
    m.original_name    = name;
    m.creation_time    = 132500000000000000ULL;
    m.last_write_time  = 132600000000000000ULL;
    m.last_access_time = 132700000000000000ULL;
    m.file_attributes  = 0x21;
    return m;
}

std::vector<std::uint8_t> encrypt_to_bytes(
    const std::vector<std::uint8_t>& plaintext,
    const format::Metadata& meta = sample_metadata(),
    const std::string& password = kPassword,
    std::uint32_t chunk = 4096) {
    MemoryReader in(plaintext);
    MemoryWriter out;
    stream::encrypt(in, plaintext.size(), meta, pw_bytes(password),
                    password.size(), out, fast_options(chunk));
    return out.bytes;
}

std::vector<std::uint8_t> decrypt_to_bytes(
    const std::vector<std::uint8_t>& ciphertext,
    format::Metadata* out_meta = nullptr,
    const std::string& password = kPassword) {
    MemoryReader in(ciphertext);
    MemoryWriter out;
    const format::Metadata m = stream::decrypt(in, pw_bytes(password),
                                               password.size(), out);
    if (out_meta != nullptr) *out_meta = m;
    return out.bytes;
}

bool decrypt_fails(const std::vector<std::uint8_t>& ciphertext,
                   const std::string& password = kPassword) {
    MemoryReader in(ciphertext);
    MemoryWriter out;
    try {
        stream::decrypt(in, pw_bytes(password), password.size(), out);
        return false;
    } catch (const stream::AuthError&) {
        return true;
    } catch (const format::FormatError&) {
        return true;
    }
}

// Offset of data chunk `i` in the container, given equal-size chunks.
std::size_t chunk_offset(std::size_t i, std::uint32_t chunk_size) {
    return format::HEADER_SIZE + format::METADATA_SIZE + crypto::TAG_SIZE +
           i * (chunk_size + crypto::TAG_SIZE);
}

}  // namespace

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

TEST(stream_round_trips_all_boundary_sizes) {
    const std::size_t sizes[] = {
        0, 1, 15, 16, 17, 100, 1000,
        4095, 4096, 4097,          // exactly one chunk, either side
        8191, 8192, 8193,          // two chunks
        12345, 40000,              // many chunks
    };

    for (std::size_t n : sizes) {
        const auto plaintext = pattern(n);
        const auto ciphertext = encrypt_to_bytes(plaintext);
        const auto recovered = decrypt_to_bytes(ciphertext);
        CHECK(recovered.size() == plaintext.size());
        CHECK(recovered == plaintext);
    }
}

TEST(stream_restores_metadata_exactly) {
    const auto plaintext = pattern(5000);
    const auto meta = sample_metadata(u"2026 사업계획.hwp");
    const auto ciphertext = encrypt_to_bytes(plaintext, meta);

    format::Metadata recovered;
    const auto out = decrypt_to_bytes(ciphertext, &recovered);

    CHECK(out == plaintext);
    CHECK(recovered.original_name == meta.original_name);
    CHECK(recovered.creation_time == meta.creation_time);
    CHECK(recovered.last_write_time == meta.last_write_time);
    CHECK(recovered.last_access_time == meta.last_access_time);
    CHECK(recovered.file_attributes == meta.file_attributes);
    CHECK(recovered.original_size == plaintext.size());
}

TEST(stream_ciphertext_size_matches_prediction) {
    for (std::size_t n : {std::size_t{0}, std::size_t{100}, std::size_t{5000},
                          std::size_t{40000}}) {
        const auto ciphertext = encrypt_to_bytes(pattern(n));
        CHECK(ciphertext.size() == stream::ciphertext_size(n, 4096));
    }
}

TEST(stream_is_randomized_per_invocation) {
    // Fresh salt, base nonce and DEK every time: encrypting the same input
    // twice must never produce the same bytes.
    const auto plaintext = pattern(3000);
    const auto a = encrypt_to_bytes(plaintext);
    const auto b = encrypt_to_bytes(plaintext);
    CHECK(a.size() == b.size());
    CHECK(a != b);
}

// ---------------------------------------------------------------------------
// Metadata hiding -- the reason this format exists
// ---------------------------------------------------------------------------

TEST(stream_ciphertext_does_not_contain_the_filename) {
    const std::u16string name = u"SECRET_PLAN.docx";
    const auto ciphertext = encrypt_to_bytes(pattern(2000), sample_metadata(name));

    // UTF-16LE form, as it would appear if the metadata block leaked.
    std::vector<std::uint8_t> utf16;
    for (char16_t c : name) {
        utf16.push_back(static_cast<std::uint8_t>(c & 0xFF));
        utf16.push_back(static_cast<std::uint8_t>(c >> 8));
    }
    CHECK(std::search(ciphertext.begin(), ciphertext.end(), utf16.begin(),
                      utf16.end()) == ciphertext.end());

    // And the plain ASCII form, in case of an accidental narrow conversion.
    const std::string ascii = "SECRET_PLAN.docx";
    CHECK(std::search(ciphertext.begin(), ciphertext.end(), ascii.begin(),
                      ascii.end()) == ciphertext.end());
}

TEST(stream_pads_distinct_sizes_to_one_bucket) {
    // 97..104 all land in the same Padmé bucket, so the container size must be
    // byte-identical across them. This is the size-hiding guarantee.
    const std::size_t reference = encrypt_to_bytes(pattern(97)).size();
    for (std::size_t n = 97; n <= 104; ++n) {
        CHECK(encrypt_to_bytes(pattern(n)).size() == reference);
    }

    // A size in a different bucket must not accidentally collide.
    CHECK(encrypt_to_bytes(pattern(200)).size() != reference);
}

TEST(stream_header_leaks_no_plaintext_length) {
    // The cleartext header bytes must be identical in structure regardless of
    // input size; only the random fields differ.
    const auto small = encrypt_to_bytes(pattern(10));
    const auto large = encrypt_to_bytes(pattern(40000));

    // Magic, version, cipher, KDF ids and cost parameters all match.
    CHECK(std::memcmp(small.data(), large.data(), 24) == 0);
    // Chunk size field matches too.
    CHECK(std::memcmp(small.data() + 40, large.data() + 40, 4) == 0);
}

// ---------------------------------------------------------------------------
// Tamper detection
// ---------------------------------------------------------------------------

TEST(stream_rejects_wrong_password) {
    const auto ciphertext = encrypt_to_bytes(pattern(1000));
    CHECK(decrypt_fails(ciphertext, "wrong-password"));
    CHECK(decrypt_fails(ciphertext, ""));
    CHECK(decrypt_fails(ciphertext, kPassword + "x"));
}

TEST(stream_rejects_flipped_bit_anywhere) {
    const auto original = encrypt_to_bytes(pattern(10000));

    const std::size_t targets[] = {
        6,                                    // format version
        12,                                   // KDF memory cost
        24,                                   // salt
        40,                                   // chunk size
        44,                                   // base nonce
        64,                                   // wrapped DEK
        96,                                   // DEK wrap tag
        format::HEADER_SIZE,                  // metadata ciphertext
        format::HEADER_SIZE + format::METADATA_SIZE,  // metadata tag
        chunk_offset(0, 4096),                // first chunk
        chunk_offset(1, 4096) + 100,          // second chunk
        original.size() - 1,                  // final tag
    };

    for (std::size_t at : targets) {
        auto bad = original;
        bad[at] ^= 0x01;
        CHECK(decrypt_fails(bad));
    }
}

TEST(stream_rejects_truncation) {
    const auto original = encrypt_to_bytes(pattern(10000));

    // Losing the trailing chunk entirely.
    auto lost_chunk = original;
    lost_chunk.resize(chunk_offset(2, 4096));
    CHECK(decrypt_fails(lost_chunk));

    // Losing a single trailing byte.
    auto lost_byte = original;
    lost_byte.pop_back();
    CHECK(decrypt_fails(lost_byte));

    // Losing everything after the header.
    auto header_only = original;
    header_only.resize(format::HEADER_SIZE);
    CHECK(decrypt_fails(header_only));
}

TEST(stream_rejects_appended_data) {
    // Without an explicit end-of-stream check, extra bytes would be silently
    // ignored -- which lets an attacker graft data onto a valid file.
    auto extended = encrypt_to_bytes(pattern(5000));
    extended.push_back(0x00);
    CHECK(decrypt_fails(extended));

    auto extended_more = encrypt_to_bytes(pattern(5000));
    extended_more.insert(extended_more.end(), 4112, 0xAA);
    CHECK(decrypt_fails(extended_more));
}

TEST(stream_rejects_reordered_chunks) {
    // 40000 bytes at 4096/chunk gives several equal-sized chunks, so a swap is
    // byte-aligned and would go unnoticed without the index in the AAD.
    const auto original = encrypt_to_bytes(pattern(40000));
    const std::size_t block = 4096 + crypto::TAG_SIZE;

    auto swapped = original;
    const std::size_t a = chunk_offset(0, 4096);
    const std::size_t b = chunk_offset(1, 4096);
    for (std::size_t i = 0; i < block; ++i) {
        std::swap(swapped[a + i], swapped[b + i]);
    }
    CHECK(decrypt_fails(swapped));
}

TEST(stream_rejects_duplicated_chunk) {
    const auto original = encrypt_to_bytes(pattern(40000));
    const std::size_t block = 4096 + crypto::TAG_SIZE;

    auto duplicated = original;
    const std::size_t a = chunk_offset(0, 4096);
    const std::size_t b = chunk_offset(1, 4096);
    std::copy(original.begin() + a, original.begin() + a + block,
              duplicated.begin() + b);
    CHECK(decrypt_fails(duplicated));
}

TEST(stream_rejects_metadata_substituted_for_chunk) {
    // block_type in the AAD is what prevents the metadata block from being
    // replayed into a data-chunk position.
    const auto original = encrypt_to_bytes(pattern(40000));

    auto substituted = original;
    const std::size_t meta_at = format::HEADER_SIZE;
    const std::size_t chunk_at = chunk_offset(0, 4096);
    // Metadata block is 1024 bytes; copy it over the head of chunk 0.
    std::copy(original.begin() + meta_at,
              original.begin() + meta_at + format::METADATA_SIZE,
              substituted.begin() + chunk_at);
    CHECK(decrypt_fails(substituted));
}

TEST(stream_rejects_header_from_a_different_file) {
    // Splicing another file's header onto this body must fail: the header
    // carries the base nonce and the wrapped DEK.
    const auto a = encrypt_to_bytes(pattern(8000));
    const auto b = encrypt_to_bytes(pattern(8000));

    auto spliced = b;
    std::copy(a.begin(), a.begin() + format::HEADER_SIZE, spliced.begin());
    CHECK(decrypt_fails(spliced));
}

// ---------------------------------------------------------------------------
// Progress and cancellation
// ---------------------------------------------------------------------------

TEST(stream_reports_progress_and_honours_cancel) {
    const auto plaintext = pattern(40000);

    std::uint64_t last_done = 0;
    std::uint64_t seen_total = 0;
    {
        MemoryReader in(plaintext);
        MemoryWriter out;
        stream::encrypt(in, plaintext.size(), sample_metadata(),
                        pw_bytes(kPassword), kPassword.size(), out,
                        fast_options(),
                        [&](std::uint64_t done, std::uint64_t total) {
                            CHECK(done >= last_done);  // monotonic
                            CHECK(done <= total);
                            last_done = done;
                            seen_total = total;
                            return true;
                        });
    }
    CHECK(seen_total == plaintext.size());
    CHECK(last_done == plaintext.size());

    // Cancelling partway must surface as CancelledError, not a silent partial
    // write that looks like a valid file.
    bool cancelled = false;
    try {
        MemoryReader in(plaintext);
        MemoryWriter out;
        int calls = 0;
        stream::encrypt(in, plaintext.size(), sample_metadata(),
                        pw_bytes(kPassword), kPassword.size(), out,
                        fast_options(),
                        [&](std::uint64_t, std::uint64_t) {
                            return ++calls < 3;
                        });
    } catch (const stream::CancelledError&) {
        cancelled = true;
    }
    CHECK(cancelled);
}
