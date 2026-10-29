#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>

#include "core/format.hpp"
#include "core/kdf.hpp"

namespace seicure::stream {

// Raised when authentication fails: wrong password, tampered file, truncated
// file, reordered chunks. Deliberately does not distinguish between them --
// telling an attacker *why* it failed is an oracle.
class AuthError : public std::runtime_error {
public:
    explicit AuthError(const std::string& what) : std::runtime_error(what) {}
};

class CancelledError : public std::runtime_error {
public:
    CancelledError() : std::runtime_error("operation cancelled") {}
};

// Abstract I/O so the engine can be exercised entirely in memory. The Win32
// file implementations live in platform/.
class Reader {
public:
    virtual ~Reader() = default;
    // Returns bytes actually read; a short read means end of input.
    virtual std::size_t read(std::uint8_t* out, std::size_t n) = 0;
};

class Writer {
public:
    virtual ~Writer() = default;
    virtual void write(const std::uint8_t* data, std::size_t n) = 0;
};

struct Options {
    kdf::Params    kdf_params{};
    std::uint32_t  chunk_size = format::DEFAULT_CHUNK_SIZE;
    // Decryption always follows the header, so this only affects new files.
    // Switch to Aes256Gcm where a deployment mandates a NIST-approved cipher.
    crypto::Cipher cipher = format::DEFAULT_CIPHER;
};

// Called with (bytes processed, bytes total). Return false to cancel, which
// surfaces as CancelledError.
using ProgressFn = std::function<bool(std::uint64_t, std::uint64_t)>;

// Invoked during decrypt as soon as the metadata block authenticates and
// before any payload byte is emitted. This is how a caller learns the original
// name -- and whether the container holds one file or an archive -- in time to
// decide where the output should go.
using MetadataFn = std::function<void(const format::Metadata&)>;

// Encrypts `input_size` bytes from `in` into `out`.
//
// `metadata.original_size` is ignored and overwritten with `input_size`; the
// caller supplies the name, timestamps and attributes.
void encrypt(Reader& in, std::uint64_t input_size,
             const format::Metadata& metadata,
             const std::uint8_t* password, std::size_t password_len,
             Writer& out, const Options& options = {},
             const ProgressFn& progress = {});

// Decrypts `in` into `out` and returns the recovered metadata so the caller
// can restore the original name, timestamps and attributes.
//
// Throws AuthError if anything fails to authenticate.
format::Metadata decrypt(Reader& in,
                         const std::uint8_t* password, std::size_t password_len,
                         Writer& out, const ProgressFn& progress = {},
                         const MetadataFn& on_metadata = {});

// Exact on-disk size for a given plaintext size and chunk size. Useful for
// preflight checks and for reasoning about what the size actually leaks.
[[nodiscard]] std::uint64_t ciphertext_size(std::uint64_t plaintext_size,
                                            std::uint32_t chunk_size);

}  // namespace seicure::stream
