#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "core/secure_buffer.hpp"

namespace seicure::crypto {

inline constexpr std::size_t KEY_SIZE = 32;  // both ciphers use 256-bit keys
inline constexpr std::size_t TAG_SIZE = 16;  // both produce 128-bit tags

inline constexpr std::size_t AES_NONCE_SIZE      = 12;
inline constexpr std::size_t XCHACHA_NONCE_SIZE  = 24;
inline constexpr std::size_t MAX_NONCE_SIZE      = XCHACHA_NONCE_SIZE;

// The header stores this as a byte, so the values are part of the file format
// and must never be reassigned.
enum class Cipher : std::uint8_t {
    Aes256Gcm         = 1,
    XChaCha20Poly1305 = 2,
};

[[nodiscard]] constexpr std::size_t nonce_size(Cipher cipher) noexcept {
    return cipher == Cipher::Aes256Gcm ? AES_NONCE_SIZE : XCHACHA_NONCE_SIZE;
}

[[nodiscard]] const char* cipher_name(Cipher cipher) noexcept;

// AES-GCM is only offered where the CPU can do it in constant time. A software
// AES would leak the key through cache timing, so refusing is the correct
// answer rather than falling back. XChaCha20 is add/rotate/xor only and is
// therefore always available.
[[nodiscard]] bool cipher_available(Cipher cipher);

class CryptoError : public std::runtime_error {
public:
    explicit CryptoError(const std::string& what) : std::runtime_error(what) {}
};

// Initialises libsodium. Safe to call repeatedly; every entry point below calls
// it, so applications do not have to.
void init();

// Fills `out` from the OS CSPRNG. Throws rather than returning a partial
// result -- a silent RNG failure would be catastrophic.
void random_bytes(std::uint8_t* out, std::size_t n);

// AEAD over one key. Nonce uniqueness is the caller's responsibility; see
// format.hpp for the counter scheme that guarantees it.
class Aead {
public:
    Aead(Cipher cipher, const std::uint8_t key[KEY_SIZE]);

    Aead(const Aead&) = delete;
    Aead& operator=(const Aead&) = delete;

    // `nonce` must be nonce_size(cipher()) bytes. `ct` needs room for `len`
    // bytes and `tag` for TAG_SIZE.
    void encrypt(const std::uint8_t* nonce,
                 const std::uint8_t* aad, std::size_t aad_len,
                 const std::uint8_t* pt, std::size_t len,
                 std::uint8_t* ct, std::uint8_t tag[TAG_SIZE]) const;

    // Returns false when authentication fails; `pt` then holds nothing usable.
    [[nodiscard]] bool decrypt(const std::uint8_t* nonce,
                               const std::uint8_t* aad, std::size_t aad_len,
                               const std::uint8_t* ct, std::size_t len,
                               const std::uint8_t tag[TAG_SIZE],
                               std::uint8_t* pt) const;

    [[nodiscard]] Cipher cipher() const noexcept { return cipher_; }

private:
    Cipher       cipher_;
    SecureBuffer key_;
};

}  // namespace seicure::crypto
