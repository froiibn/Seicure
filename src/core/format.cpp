#include "core/format.hpp"

#include <bit>
#include <cstring>
#include <limits>

namespace seicure::format {
namespace {

void put_u16_le(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}

void put_u32_le(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

void put_u64_le(std::uint8_t* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>(v >> (8 * i));
    }
}

void put_u32_be(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<std::uint8_t>(v >> (8 * (3 - i)));
    }
}

void put_u64_be(std::uint8_t* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>(v >> (8 * (7 - i)));
    }
}

std::uint16_t get_u16_le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t get_u32_le(const std::uint8_t* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    }
    return v;
}

std::uint64_t get_u64_le(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return v;
}

}  // namespace

std::uint64_t padme(std::uint64_t length) {
    // Below this the bucketing math degenerates and the absolute overhead is
    // negligible anyway, so clamp to a fixed floor.
    if (length <= 16) {
        return 16;
    }
    const unsigned E = static_cast<unsigned>(std::bit_width(length)) - 1;
    const unsigned S = static_cast<unsigned>(std::bit_width(E));
    const unsigned z = E - S;

    const std::uint64_t mask = (std::uint64_t{1} << z) - 1;
    if (length > (std::numeric_limits<std::uint64_t>::max)() - mask) {
        return length;  // pathological; refuse to wrap
    }
    return (length + mask) & ~mask;
}

// ---------------------------------------------------------------------------
// Header
// ---------------------------------------------------------------------------

void Header::serialize(std::uint8_t out[HEADER_SIZE]) const {
    std::memset(out, 0, HEADER_SIZE);
    std::memcpy(out + 0, MAGIC, sizeof(MAGIC));
    out[6] = format_version;
    out[7] = static_cast<std::uint8_t>(cipher);
    out[8] = kdf_id;
    // out[9..11] reserved, already zero
    put_u32_le(out + 12, kdf_params.memory_kib);
    put_u32_le(out + 16, kdf_params.iterations);
    put_u32_le(out + 20, kdf_params.parallelism);
    std::memcpy(out + 24, salt, kdf::SALT_SIZE);
    put_u32_le(out + 40, chunk_size);
    std::memcpy(out + 44, base_nonce, BASE_NONCE_SIZE);
    std::memcpy(out + 64, dek_wrap_nonce, crypto::MAX_NONCE_SIZE);
    std::memcpy(out + 88, wrapped_dek, crypto::KEY_SIZE);
    std::memcpy(out + 120, dek_wrap_tag, crypto::TAG_SIZE);
}

Header Header::parse(const std::uint8_t in[HEADER_SIZE]) {
    if (std::memcmp(in, MAGIC, sizeof(MAGIC)) != 0) {
        throw FormatError("not a Seicure file (bad magic)");
    }

    Header h;
    h.format_version = in[6];
    h.kdf_id         = in[8];

    if (h.format_version != FORMAT_VERSION) {
        throw FormatError("unsupported format version");
    }
    if (in[7] != CIPHER_AES_256_GCM && in[7] != CIPHER_XCHACHA20_POLY1305) {
        throw FormatError("unsupported cipher");
    }
    h.cipher = static_cast<crypto::Cipher>(in[7]);
    if (h.kdf_id != KDF_ARGON2ID) {
        throw FormatError("unsupported KDF");
    }

    h.kdf_params.memory_kib  = get_u32_le(in + 12);
    h.kdf_params.iterations  = get_u32_le(in + 16);
    h.kdf_params.parallelism = get_u32_le(in + 20);
    // Bounds-check before the KDF runs: these values come from an attacker in
    // the worst case, and an unchecked memory cost is a trivial DoS.
    if (!h.kdf_params.valid()) {
        throw FormatError("KDF cost parameters out of range");
    }

    std::memcpy(h.salt, in + 24, kdf::SALT_SIZE);

    h.chunk_size = get_u32_le(in + 40);
    if (h.chunk_size < MIN_CHUNK_SIZE || h.chunk_size > MAX_CHUNK_SIZE) {
        throw FormatError("chunk size out of range");
    }

    std::memcpy(h.base_nonce,     in + 44, BASE_NONCE_SIZE);
    std::memcpy(h.dek_wrap_nonce, in + 64, crypto::MAX_NONCE_SIZE);
    std::memcpy(h.wrapped_dek,    in + 88, crypto::KEY_SIZE);
    std::memcpy(h.dek_wrap_tag,   in + 120, crypto::TAG_SIZE);
    return h;
}

// ---------------------------------------------------------------------------
// Metadata
// ---------------------------------------------------------------------------

void Metadata::serialize(std::uint8_t out[METADATA_SIZE]) const {
    if (original_name.size() > MAX_NAME_UNITS) {
        throw FormatError("original filename too long");
    }

    std::memset(out, 0, METADATA_SIZE);
    put_u64_le(out + 0,  original_size);
    put_u64_le(out + 8,  creation_time);
    put_u64_le(out + 16, last_write_time);
    put_u64_le(out + 24, last_access_time);
    put_u32_le(out + 32, file_attributes);
    put_u32_le(out + 36, entry_count);
    put_u16_le(out + 40, static_cast<std::uint16_t>(original_name.size()));
    out[42] = static_cast<std::uint8_t>(content_type);
    out[43] = meta_flags;
    put_u32_le(out + 44, posix_mode);

    // UTF-16LE, explicitly serialized rather than memcpy'd so the on-disk
    // layout does not depend on host endianness.
    for (std::size_t i = 0; i < original_name.size(); ++i) {
        put_u16_le(out + NAME_OFFSET + i * 2,
                   static_cast<std::uint16_t>(original_name[i]));
    }
    // Remainder stays zero: fixed size, so the name length does not leak.
}

Metadata Metadata::parse(const std::uint8_t in[METADATA_SIZE]) {
    Metadata m;
    m.original_size    = get_u64_le(in + 0);
    m.creation_time    = get_u64_le(in + 8);
    m.last_write_time  = get_u64_le(in + 16);
    m.last_access_time = get_u64_le(in + 24);
    m.file_attributes  = get_u32_le(in + 32);
    m.entry_count      = get_u32_le(in + 36);
    m.meta_flags       = in[43];
    // Only the permission bits are meaningful; the file-type bits in st_mode
    // describe the original object, not the file we are about to create.
    m.posix_mode       = get_u32_le(in + 44) & 07777u;

    const std::uint16_t name_units = get_u16_le(in + 40);
    if (name_units > MAX_NAME_UNITS) {
        throw FormatError("metadata: filename length out of range");
    }

    const std::uint8_t type = in[42];
    if (type > static_cast<std::uint8_t>(ContentType::Archive)) {
        throw FormatError("metadata: unknown content type");
    }
    m.content_type = static_cast<ContentType>(type);

    m.original_name.resize(name_units);
    for (std::size_t i = 0; i < name_units; ++i) {
        m.original_name[i] =
            static_cast<char16_t>(get_u16_le(in + NAME_OFFSET + i * 2));
    }
    return m;
}

bool posix_mode_is_readonly(std::uint32_t mode) noexcept {
    // Read-only in the Windows sense means nobody may write.
    return (mode & 0222u) == 0;
}

std::uint32_t posix_mode_for_readonly(bool readonly) noexcept {
    // 0644 / 0444: readable by all, writable by the owner unless read-only.
    // Execute bits are deliberately not invented -- Windows never knew them,
    // and guessing one on would be a security decision we have no basis for.
    return readonly ? 0444u : 0644u;
}

// ---------------------------------------------------------------------------
// Nonce / AAD
// ---------------------------------------------------------------------------

void make_nonce(const std::uint8_t base_nonce[BASE_NONCE_SIZE],
                std::uint32_t counter, crypto::Cipher cipher,
                std::uint8_t out[crypto::MAX_NONCE_SIZE]) {
    const std::size_t total = crypto::nonce_size(cipher);
    const std::size_t prefix = total - 4;
    std::memcpy(out, base_nonce, prefix);
    put_u32_be(out + prefix, counter);
}

void make_aad(BlockType type, std::uint64_t block_index, bool is_final,
              std::uint8_t out[AAD_SIZE]) {
    out[0] = static_cast<std::uint8_t>(type);
    put_u64_be(out + 1, block_index);
    out[9] = is_final ? 1 : 0;
}

std::uint64_t max_chunks() noexcept {
    // Counter 0 is taken by the metadata block, so data chunks get 1..2^32-1.
    return (std::uint64_t{1} << 32) - 1;
}

}  // namespace seicure::format
