#include <cstring>
#include <string>
#include <vector>

#include "core/format.hpp"
#include "test_util.hpp"

using namespace seicure;
using namespace seicure::format;

namespace {

Header make_populated_header() {
    Header h;
    h.kdf_params = kdf::Params{65536, 4, 1};
    h.chunk_size = 4096;
    for (std::size_t i = 0; i < kdf::SALT_SIZE; ++i) {
        h.salt[i] = static_cast<std::uint8_t>(0x10 + i);
    }
    for (std::size_t i = 0; i < BASE_NONCE_SIZE; ++i) {
        h.base_nonce[i] = static_cast<std::uint8_t>(0x40 + i);
    }
    for (std::size_t i = 0; i < crypto::MAX_NONCE_SIZE; ++i) {
        h.dek_wrap_nonce[i] = static_cast<std::uint8_t>(0x50 + i);
    }
    for (std::size_t i = 0; i < crypto::KEY_SIZE; ++i) {
        h.wrapped_dek[i] = static_cast<std::uint8_t>(0x60 + i);
    }
    for (std::size_t i = 0; i < crypto::TAG_SIZE; ++i) {
        h.dek_wrap_tag[i] = static_cast<std::uint8_t>(0x90 + i);
    }
    return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// Padmé
// ---------------------------------------------------------------------------

TEST(padme_never_shrinks_and_is_monotonic) {
    std::uint64_t previous = 0;
    for (std::uint64_t len = 0; len <= 4096; ++len) {
        const std::uint64_t padded = padme(len);
        CHECK(padded >= len);
        CHECK(padded >= previous);  // monotonic: buckets never go backwards
        previous = padded;
    }
}

TEST(padme_overhead_stays_within_twelve_percent) {
    // The published bound is ~12%; the empirical worst case sits just under
    // 11.8% (lengths just above 2^7). If this ever regresses, the padding math
    // is wrong, not the bound.
    for (std::uint64_t len = 16; len <= 200000; ++len) {
        const std::uint64_t padded = padme(len);
        const std::uint64_t overhead = padded - len;
        CHECK(overhead * 100 <= len * 12);
    }
}

TEST(padme_known_values) {
    // Small inputs clamp to a fixed floor rather than revealing themselves.
    CHECK(padme(0) == 16);
    CHECK(padme(1) == 16);
    CHECK(padme(15) == 16);
    CHECK(padme(16) == 16);

    // Powers of two are already bucket boundaries.
    CHECK(padme(1024) == 1024);
    CHECK(padme(65536) == 65536);

    // Worked examples: E=6,S=3,z=3 -> multiple of 8; E=19,S=5,z=14 -> 16 KiB.
    CHECK(padme(100) == 104);
    CHECK(padme(1000000) == 1015808);
}

TEST(padme_collapses_distinct_sizes_into_buckets) {
    // The entire point: many different true sizes must map to one on-disk
    // size, otherwise the padding hides nothing.
    CHECK(padme(1000001) == padme(1010000));
    CHECK(padme(500000) == padme(504000));
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

TEST(header_round_trips) {
    const Header original = make_populated_header();
    std::uint8_t bytes[HEADER_SIZE];
    original.serialize(bytes);

    const Header parsed = Header::parse(bytes);
    CHECK(parsed.format_version == original.format_version);
    CHECK(parsed.cipher == original.cipher);
    CHECK(parsed.kdf_id == original.kdf_id);
    CHECK(parsed.kdf_params.memory_kib == original.kdf_params.memory_kib);
    CHECK(parsed.kdf_params.iterations == original.kdf_params.iterations);
    CHECK(parsed.kdf_params.parallelism == original.kdf_params.parallelism);
    CHECK(parsed.chunk_size == original.chunk_size);
    CHECK(std::memcmp(parsed.salt, original.salt, kdf::SALT_SIZE) == 0);
    CHECK(std::memcmp(parsed.base_nonce, original.base_nonce,
                      BASE_NONCE_SIZE) == 0);
    CHECK(std::memcmp(parsed.dek_wrap_nonce, original.dek_wrap_nonce,
                      crypto::MAX_NONCE_SIZE) == 0);
    CHECK(std::memcmp(parsed.wrapped_dek, original.wrapped_dek,
                      crypto::KEY_SIZE) == 0);
    CHECK(std::memcmp(parsed.dek_wrap_tag, original.dek_wrap_tag,
                      crypto::TAG_SIZE) == 0);
}

TEST(header_carries_no_size_information) {
    // Two files of very different sizes must produce byte-identical headers
    // when every other input matches. The header has no size field at all, so
    // this is really a guard against one being added later by accident.
    std::uint8_t bytes[HEADER_SIZE];
    make_populated_header().serialize(bytes);

    // Offsets 0..63 are the AAD region; everything after is DEK wrap material.
    // The base nonce is sized for the widest cipher, so the wrap fields sit
    // further along than they did when only AES-GCM existed.
    CHECK(HEADER_AAD_SIZE == 64);
    CHECK(HEADER_SIZE == 136);
    CHECK(bytes[64] == 0x50);   // dek_wrap_nonce starts here
    CHECK(bytes[88] == 0x60);   // wrapped_dek
    CHECK(bytes[120] == 0x90);  // dek_wrap_tag
}

TEST(header_aad_region_excludes_wrap_fields) {
    // The unwrap AAD must cover the parameters but not the tag it protects.
    Header a = make_populated_header();
    Header b = a;
    b.wrapped_dek[0] ^= 0xFF;
    b.dek_wrap_tag[0] ^= 0xFF;

    std::uint8_t ba[HEADER_SIZE], bb[HEADER_SIZE];
    a.serialize(ba);
    b.serialize(bb);
    CHECK(std::memcmp(ba, bb, HEADER_AAD_SIZE) == 0);
    CHECK(std::memcmp(ba, bb, HEADER_SIZE) != 0);
}

TEST(header_rejects_bad_magic) {
    std::uint8_t bytes[HEADER_SIZE];
    make_populated_header().serialize(bytes);
    bytes[0] = 'X';
    CHECK_THROWS(Header::parse(bytes));
}

TEST(header_rejects_unsupported_identifiers) {
    std::uint8_t bytes[HEADER_SIZE];

    make_populated_header().serialize(bytes);
    bytes[6] = 99;  // format version
    CHECK_THROWS(Header::parse(bytes));

    make_populated_header().serialize(bytes);
    bytes[7] = 99;  // cipher
    CHECK_THROWS(Header::parse(bytes));

    make_populated_header().serialize(bytes);
    bytes[8] = 99;  // kdf
    CHECK_THROWS(Header::parse(bytes));
}

TEST(header_rejects_hostile_kdf_parameters) {
    // A crafted header could otherwise ask us to allocate gigabytes, or drop
    // the cost to nothing, before a single byte has been authenticated.
    std::uint8_t bytes[HEADER_SIZE];

    Header huge = make_populated_header();
    huge.kdf_params.memory_kib = 4u * 1024u * 1024u;  // 4 GiB
    huge.serialize(bytes);
    CHECK_THROWS(Header::parse(bytes));

    Header zero_time = make_populated_header();
    zero_time.kdf_params.iterations = 0;
    zero_time.serialize(bytes);
    CHECK_THROWS(Header::parse(bytes));

    Header zero_lanes = make_populated_header();
    zero_lanes.kdf_params.parallelism = 0;
    zero_lanes.serialize(bytes);
    CHECK_THROWS(Header::parse(bytes));
}

TEST(header_rejects_bad_chunk_size) {
    std::uint8_t bytes[HEADER_SIZE];

    Header small = make_populated_header();
    small.chunk_size = 16;
    small.serialize(bytes);
    CHECK_THROWS(Header::parse(bytes));

    Header big = make_populated_header();
    big.chunk_size = 512u << 20;
    big.serialize(bytes);
    CHECK_THROWS(Header::parse(bytes));
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

TEST(metadata_round_trips_unicode_names) {
    const std::u16string names[] = {
        u"report.txt",
        u"보고서.hwp",                 // 보고서.hwp
        u"중요 문서 (最終).xlsx",
        {char16_t(0xD83D), char16_t(0xDD12), u'.', u't', u'x', u't'},  // surrogate pair
        u"",
    };

    for (const auto& name : names) {
        Metadata m;
        m.original_size    = 1234567890123ULL;
        m.creation_time    = 132000000000000000ULL;
        m.last_write_time  = 132111111111111111ULL;
        m.last_access_time = 132222222222222222ULL;
        m.file_attributes  = 0x21;  // READONLY | ARCHIVE
        m.original_name    = name;

        std::uint8_t bytes[METADATA_SIZE];
        m.serialize(bytes);
        const Metadata parsed = Metadata::parse(bytes);

        CHECK(parsed.original_size == m.original_size);
        CHECK(parsed.creation_time == m.creation_time);
        CHECK(parsed.last_write_time == m.last_write_time);
        CHECK(parsed.last_access_time == m.last_access_time);
        CHECK(parsed.file_attributes == m.file_attributes);
        CHECK(parsed.original_name == name);
    }
}

TEST(metadata_block_is_fixed_size_regardless_of_name) {
    // If the block were variable length, the ciphertext size would leak how
    // long the original filename was.
    Metadata shortish;
    shortish.original_name = u"a";

    Metadata longish;
    longish.original_name = std::u16string(MAX_NAME_UNITS, u'x');

    std::uint8_t a[METADATA_SIZE], b[METADATA_SIZE];
    shortish.serialize(a);
    longish.serialize(b);

    // Same length by construction; verify the tail of the short one is zeroed
    // rather than carrying stack residue.
    for (std::size_t i = NAME_OFFSET + 2; i < METADATA_SIZE; ++i) {
        CHECK(a[i] == 0);
    }
}

TEST(metadata_round_trips_content_type_and_entry_count) {
    Metadata archive;
    archive.content_type  = ContentType::Archive;
    archive.entry_count   = 42;
    archive.original_size = 999;

    std::uint8_t bytes[METADATA_SIZE];
    archive.serialize(bytes);
    const Metadata parsed = Metadata::parse(bytes);
    CHECK(parsed.content_type == ContentType::Archive);
    CHECK(parsed.entry_count == 42);
    CHECK(parsed.original_size == 999);

    // Default is a plain file.
    Metadata single;
    single.serialize(bytes);
    CHECK(Metadata::parse(bytes).content_type == ContentType::File);
}

TEST(metadata_round_trips_permissions_from_both_worlds) {
    // A container written on Windows carries attributes; one written on POSIX
    // carries a mode. Both survive the round trip, and the flags say which the
    // writer actually knew.
    std::uint8_t bytes[METADATA_SIZE];

    Metadata from_windows;
    from_windows.file_attributes = 0x21;  // READONLY | ARCHIVE
    from_windows.posix_mode      = 0444;
    from_windows.meta_flags = META_HAS_WIN_ATTRIBUTES | META_HAS_POSIX_MODE;
    from_windows.serialize(bytes);
    Metadata parsed = Metadata::parse(bytes);
    CHECK(parsed.file_attributes == 0x21);
    CHECK(parsed.posix_mode == 0444);
    CHECK((parsed.meta_flags & META_HAS_WIN_ATTRIBUTES) != 0);
    CHECK((parsed.meta_flags & META_HAS_POSIX_MODE) != 0);

    Metadata from_posix;
    from_posix.posix_mode = 0754;
    from_posix.meta_flags = META_HAS_POSIX_MODE;
    from_posix.serialize(bytes);
    parsed = Metadata::parse(bytes);
    CHECK(parsed.posix_mode == 0754);
    CHECK((parsed.meta_flags & META_HAS_POSIX_MODE) != 0);
    CHECK((parsed.meta_flags & META_HAS_WIN_ATTRIBUTES) == 0);

    // A container from an older writer says nothing about permissions.
    Metadata bare;
    bare.serialize(bytes);
    CHECK(Metadata::parse(bytes).meta_flags == 0);
}

TEST(metadata_strips_non_permission_mode_bits) {
    // st_mode carries file-type bits above the permissions. Those describe the
    // original object, not the file being created, so they must not survive.
    Metadata m;
    m.posix_mode = 0100644;  // S_IFREG | 0644
    m.meta_flags = META_HAS_POSIX_MODE;
    std::uint8_t bytes[METADATA_SIZE];
    m.serialize(bytes);
    CHECK(Metadata::parse(bytes).posix_mode == 0644);
}

TEST(readonly_maps_between_windows_and_posix) {
    CHECK(posix_mode_is_readonly(0444));
    CHECK(posix_mode_is_readonly(0555));
    CHECK(!posix_mode_is_readonly(0644));
    CHECK(!posix_mode_is_readonly(0200));  // write for owner only still counts

    CHECK(posix_mode_for_readonly(true) == 0444);
    CHECK(posix_mode_for_readonly(false) == 0644);

    // Round trip: read-only survives a hop through either representation.
    CHECK(posix_mode_is_readonly(posix_mode_for_readonly(true)));
    CHECK(!posix_mode_is_readonly(posix_mode_for_readonly(false)));

    // Execute bits are never invented -- that would be a security decision
    // with no basis in the source metadata.
    CHECK((posix_mode_for_readonly(false) & 0111) == 0);
    CHECK((posix_mode_for_readonly(true) & 0111) == 0);
}

TEST(metadata_rejects_unknown_content_type) {
    Metadata m;
    m.original_name = u"x.txt";
    std::uint8_t bytes[METADATA_SIZE];
    m.serialize(bytes);
    bytes[42] = 7;
    CHECK_THROWS(Metadata::parse(bytes));
}

TEST(metadata_rejects_overlong_name) {
    Metadata m;
    m.original_name = std::u16string(MAX_NAME_UNITS + 1, u'x');
    std::uint8_t bytes[METADATA_SIZE];
    CHECK_THROWS(m.serialize(bytes));

    // And a crafted length field must be rejected on the way back in.
    Metadata ok;
    ok.original_name = u"fine.txt";
    ok.serialize(bytes);
    bytes[40] = 0xFF;
    bytes[41] = 0xFF;
    CHECK_THROWS(Metadata::parse(bytes));
}

// ---------------------------------------------------------------------------
// Nonce / AAD
// ---------------------------------------------------------------------------

TEST(nonce_is_base_plus_big_endian_counter) {
    // The base nonce is stored at full width; each cipher consumes as much of
    // it as its nonce size allows, then appends the counter.
    std::uint8_t base[BASE_NONCE_SIZE];
    for (std::size_t i = 0; i < BASE_NONCE_SIZE; ++i) {
        base[i] = static_cast<std::uint8_t>(i + 1);
    }
    std::uint8_t nonce[crypto::MAX_NONCE_SIZE];

    // AES-GCM: 8 bytes of base + 4 byte counter = 12.
    make_nonce(base, 0, crypto::Cipher::Aes256Gcm, nonce);
    CHECK_EQ_HEX(nonce, crypto::AES_NONCE_SIZE, "010203040506070800000000");
    make_nonce(base, 0x01020304u, crypto::Cipher::Aes256Gcm, nonce);
    CHECK_EQ_HEX(nonce, crypto::AES_NONCE_SIZE, "010203040506070801020304");

    // XChaCha20: 20 bytes of base + 4 byte counter = 24.
    make_nonce(base, 1, crypto::Cipher::XChaCha20Poly1305, nonce);
    CHECK_EQ_HEX(nonce, crypto::XCHACHA_NONCE_SIZE,
                 "0102030405060708090a0b0c0d0e0f1011121314"
                 "00000001");
}

TEST(header_defaults_to_xchacha20) {
    Header h;
    CHECK(h.cipher == crypto::Cipher::XChaCha20Poly1305);
    CHECK(DEFAULT_CIPHER == crypto::Cipher::XChaCha20Poly1305);
    // Both identifiers must round-trip; AES stays supported for deployments
    // that mandate a NIST-approved cipher.
    for (crypto::Cipher c : {crypto::Cipher::Aes256Gcm,
                             crypto::Cipher::XChaCha20Poly1305}) {
        Header a = make_populated_header();
        a.cipher = c;
        std::uint8_t bytes[HEADER_SIZE];
        a.serialize(bytes);
        CHECK(Header::parse(bytes).cipher == c);
    }
}

TEST(header_rejects_unknown_cipher_id) {
    std::uint8_t bytes[HEADER_SIZE];
    make_populated_header().serialize(bytes);
    bytes[7] = 3;  // neither AES-256-GCM nor XChaCha20-Poly1305
    CHECK_THROWS(Header::parse(bytes));
}

TEST(nonces_are_unique_across_blocks) {
    // The metadata block and every data chunk must land on distinct counters.
    CHECK(METADATA_COUNTER == 0);
    CHECK(chunk_counter(0) == 1);
    CHECK(chunk_counter(1) == 2);
    CHECK(chunk_counter(100) == 101);
}

TEST(aad_binds_type_index_and_finality) {
    std::uint8_t aad[AAD_SIZE];

    make_aad(BlockType::Metadata, 0, true, aad);
    CHECK_EQ_HEX(aad, AAD_SIZE, "00000000000000000001");

    make_aad(BlockType::Data, 0, false, aad);
    CHECK_EQ_HEX(aad, AAD_SIZE, "01000000000000000000");

    make_aad(BlockType::Data, 5, true, aad);
    CHECK_EQ_HEX(aad, AAD_SIZE, "01000000000000000501");

    // A metadata block and a data chunk at the same index must not share AAD,
    // or one could be substituted for the other.
    std::uint8_t meta_aad[AAD_SIZE], data_aad[AAD_SIZE];
    make_aad(BlockType::Metadata, 0, true, meta_aad);
    make_aad(BlockType::Data, 0, true, data_aad);
    CHECK(std::memcmp(meta_aad, data_aad, AAD_SIZE) != 0);
}
