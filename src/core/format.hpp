#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "core/crypto.hpp"
#include "core/kdf.hpp"

namespace seicure::format {

inline constexpr std::uint8_t MAGIC[6] = {'S', 'E', 'I', 'C', 'U', 'R'};
inline constexpr std::uint8_t FORMAT_VERSION = 1;
inline constexpr std::uint8_t KDF_ARGON2ID   = 1;

// Cipher identifiers are crypto::Cipher values; the enum owns the numbering.
inline constexpr std::uint8_t CIPHER_AES_256_GCM =
    static_cast<std::uint8_t>(crypto::Cipher::Aes256Gcm);
inline constexpr std::uint8_t CIPHER_XCHACHA20_POLY1305 =
    static_cast<std::uint8_t>(crypto::Cipher::XChaCha20Poly1305);

// Default for new containers. AES stays supported for the case where a
// deployment mandates a NIST-approved cipher, but XChaCha20 is the better
// choice everywhere else: constant-time in software on every CPU, and a nonce
// wide enough that the counter scheme has enormous headroom.
inline constexpr crypto::Cipher DEFAULT_CIPHER =
    crypto::Cipher::XChaCha20Poly1305;

inline constexpr std::size_t HEADER_SIZE      = 136;
// Bytes [0, HEADER_AAD_SIZE) are fed as AAD to the DEK unwrap. That is what
// authenticates the header: tampering with the cipher id, the KDF cost, the
// salt, or the chunk size makes the unwrap tag fail, so no separate header MAC
// is needed.
inline constexpr std::size_t HEADER_AAD_SIZE  = 64;

// Sized for the widest nonce. A chunk nonce is base_nonce || counter(4), so the
// base occupies nonce_size(cipher) - 4 bytes and AES simply uses fewer of them.
inline constexpr std::size_t BASE_NONCE_SIZE  = crypto::MAX_NONCE_SIZE - 4;

// The metadata block is a fixed size regardless of how long the filename is.
// A variable-length block would leak the name length through the file size.
inline constexpr std::size_t METADATA_SIZE     = 1024;
inline constexpr std::size_t MAX_NAME_UNITS    = 255;  // NTFS component limit
inline constexpr std::size_t MAX_NAME_BYTES    = MAX_NAME_UNITS * 2;
inline constexpr std::size_t NAME_OFFSET       = 48;

inline constexpr std::uint32_t DEFAULT_CHUNK_SIZE = 1u << 20;  // 1 MiB
inline constexpr std::uint32_t MIN_CHUNK_SIZE     = 4096;
inline constexpr std::uint32_t MAX_CHUNK_SIZE     = 64u << 20;

class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& what) : std::runtime_error(what) {}
};

// ---------------------------------------------------------------------------
// Padmé padding (Nikitin et al., "Reducing Metadata Leakage from Encrypted
// Files", PETS 2019).
//
// Rounds a length up so the on-disk size falls into a coarse bucket, bounding
// the overhead at roughly 12% for small inputs and far less for large ones.
//
// This does NOT hide the file size outright -- nothing short of a fixed-size
// container can. It only reduces how precisely the size can be read off.
// ---------------------------------------------------------------------------
[[nodiscard]] std::uint64_t padme(std::uint64_t length);

// ---------------------------------------------------------------------------
// Cleartext header. Carries only what is needed to attempt decryption; no size
// information and nothing derived from the original file live here.
// ---------------------------------------------------------------------------
struct Header {
    std::uint8_t   format_version = FORMAT_VERSION;
    crypto::Cipher cipher         = DEFAULT_CIPHER;
    std::uint8_t   kdf_id         = KDF_ARGON2ID;
    kdf::Params    kdf_params{};
    std::uint8_t   salt[kdf::SALT_SIZE]{};
    std::uint32_t  chunk_size = DEFAULT_CHUNK_SIZE;
    std::uint8_t   base_nonce[BASE_NONCE_SIZE]{};
    std::uint8_t   dek_wrap_nonce[crypto::MAX_NONCE_SIZE]{};
    std::uint8_t   wrapped_dek[crypto::KEY_SIZE]{};
    std::uint8_t   dek_wrap_tag[crypto::TAG_SIZE]{};

    void serialize(std::uint8_t out[HEADER_SIZE]) const;

    // Parses untrusted bytes -- this runs before anything is authenticated, so
    // it validates every field it can. Throws FormatError on rejection.
    [[nodiscard]] static Header parse(const std::uint8_t in[HEADER_SIZE]);
};

// What the encrypted payload holds. A container is either one file's bytes or
// an archive of several; the distinction is itself encrypted, so an observer
// cannot tell whether a .sef holds one document or forty.
enum class ContentType : std::uint8_t {
    File    = 0,
    Archive = 1,
};

// Which permission representations the writer actually filled in. Windows
// attributes and POSIX mode bits do not map onto each other cleanly, so a
// container records whichever it knows and says so, rather than forcing the
// reader to guess whether a zero means "not set" or "genuinely zero".
enum MetaFlags : std::uint8_t {
    META_HAS_WIN_ATTRIBUTES = 1u << 0,
    META_HAS_POSIX_MODE     = 1u << 1,
};

// Windows' read-only flag is the only attribute with a POSIX counterpart, and
// even that is approximate: read-only means "no write bits for anybody".
[[nodiscard]] bool posix_mode_is_readonly(std::uint32_t mode) noexcept;
[[nodiscard]] std::uint32_t posix_mode_for_readonly(bool readonly) noexcept;

// ---------------------------------------------------------------------------
// Encrypted metadata block. Everything the file system would otherwise reveal.
//
// The same structure describes the container as a whole and each entry inside
// an archive, which keeps one serializer for both.
// ---------------------------------------------------------------------------
// Timestamps are stored as Windows FILETIME ticks (100 ns since 1601-01-01
// UTC) on every platform. That is finer than any filesystem actually records,
// so POSIX ports convert rather than lose anything. Zero means "unknown" --
// Linux, for instance, does not expose a creation time through stat().
struct Metadata {
    std::uint64_t  original_size    = 0;  // true size, before padding
    std::uint64_t  creation_time    = 0;  // FILETIME ticks, 0 = unknown
    std::uint64_t  last_write_time  = 0;
    std::uint64_t  last_access_time = 0;
    std::uint32_t  file_attributes  = 0;  // Windows FILE_ATTRIBUTE_*
    std::uint32_t  posix_mode       = 0;  // POSIX permission bits (& 07777)
    std::uint8_t   meta_flags       = 0;  // MetaFlags: which of the two is set
    std::uint32_t  entry_count      = 0;  // archives only
    ContentType    content_type     = ContentType::File;
    std::u16string original_name;         // base name only, no path

    void serialize(std::uint8_t out[METADATA_SIZE]) const;
    [[nodiscard]] static Metadata parse(const std::uint8_t in[METADATA_SIZE]);
};

// ---------------------------------------------------------------------------
// Nonce and AAD construction.
//
// Nonce = base_nonce(8) || counter(4, big-endian). The DEK is random per file
// and the counter is strictly increasing, so a nonce can never repeat under
// one key.
//
// AAD = block_type(1) || block_index(8, BE) || is_final(1). The index defeats
// chunk reordering, is_final defeats truncation, and block_type keeps the
// metadata block from being swapped in for a data chunk.
// ---------------------------------------------------------------------------
enum class BlockType : std::uint8_t {
    Metadata = 0,
    Data     = 1,
};

inline constexpr std::size_t AAD_SIZE = 10;

// Counter 0 is reserved for the metadata block; data chunk i uses i + 1.
inline constexpr std::uint32_t METADATA_COUNTER = 0;
[[nodiscard]] constexpr std::uint32_t chunk_counter(std::uint64_t index) {
    return static_cast<std::uint32_t>(index + 1);
}

// Writes crypto::nonce_size(cipher) bytes: the leading bytes of `base_nonce`
// followed by a big-endian counter.
void make_nonce(const std::uint8_t base_nonce[BASE_NONCE_SIZE],
                std::uint32_t counter, crypto::Cipher cipher,
                std::uint8_t out[crypto::MAX_NONCE_SIZE]);

void make_aad(BlockType type, std::uint64_t block_index, bool is_final,
              std::uint8_t out[AAD_SIZE]);

// Largest chunk index representable by the 32-bit nonce counter.
[[nodiscard]] std::uint64_t max_chunks() noexcept;

}  // namespace seicure::format
