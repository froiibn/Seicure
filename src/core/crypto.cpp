#include "core/crypto.hpp"

#include <sodium.h>

#include <cstring>
#include <limits>
#include <mutex>

namespace seicure::crypto {
namespace {

// sodium_init() performs CPU feature detection, which crypto_aead_aes256gcm_*
// depends on. Calling it before anything else is mandatory.
void ensure_initialised() {
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] { ok = sodium_init() >= 0; });
    if (!ok) {
        throw CryptoError("libsodium failed to initialise");
    }
}

}  // namespace

const char* cipher_name(Cipher cipher) noexcept {
    switch (cipher) {
    case Cipher::Aes256Gcm:         return "AES-256-GCM";
    case Cipher::XChaCha20Poly1305: return "XChaCha20-Poly1305";
    }
    return "unknown";
}

bool cipher_available(Cipher cipher) {
    ensure_initialised();
    if (cipher == Cipher::Aes256Gcm) {
        return crypto_aead_aes256gcm_is_available() != 0;
    }
    return true;
}

void init() { ensure_initialised(); }

void random_bytes(std::uint8_t* out, std::size_t n) {
    if (n == 0) {
        return;
    }
    ensure_initialised();
    randombytes_buf(out, n);
}

Aead::Aead(Cipher cipher, const std::uint8_t key[KEY_SIZE])
    : cipher_(cipher), key_(KEY_SIZE) {
    ensure_initialised();
    if (!cipher_available(cipher)) {
        throw CryptoError(std::string(cipher_name(cipher)) +
                          " is not available on this CPU");
    }
    std::memcpy(key_.data(), key, KEY_SIZE);
}

void Aead::encrypt(const std::uint8_t* nonce,
                   const std::uint8_t* aad, std::size_t aad_len,
                   const std::uint8_t* pt, std::size_t len,
                   std::uint8_t* ct, std::uint8_t tag[TAG_SIZE]) const {
    unsigned long long tag_len = 0;
    int rc = -1;

    switch (cipher_) {
    case Cipher::XChaCha20Poly1305:
        rc = crypto_aead_xchacha20poly1305_ietf_encrypt_detached(
            ct, tag, &tag_len, pt, len, aad, aad_len, nullptr, nonce,
            key_.data());
        break;
    case Cipher::Aes256Gcm:
        rc = crypto_aead_aes256gcm_encrypt_detached(
            ct, tag, &tag_len, pt, len, aad, aad_len, nullptr, nonce,
            key_.data());
        break;
    }

    if (rc != 0 || tag_len != TAG_SIZE) {
        throw CryptoError("AEAD encryption failed");
    }
}

bool Aead::decrypt(const std::uint8_t* nonce,
                   const std::uint8_t* aad, std::size_t aad_len,
                   const std::uint8_t* ct, std::size_t len,
                   const std::uint8_t tag[TAG_SIZE],
                   std::uint8_t* pt) const {
    int rc = -1;

    switch (cipher_) {
    case Cipher::XChaCha20Poly1305:
        rc = crypto_aead_xchacha20poly1305_ietf_decrypt_detached(
            pt, nullptr, ct, len, tag, aad, aad_len, nonce, key_.data());
        break;
    case Cipher::Aes256Gcm:
        rc = crypto_aead_aes256gcm_decrypt_detached(
            pt, nullptr, ct, len, tag, aad, aad_len, nonce, key_.data());
        break;
    }

    if (rc != 0) {
        // Expected on tampered or wrong-key input. Wipe anything the library
        // may have written so a caller that ignores the result cannot leak it.
        secure_zero(pt, len);
        return false;
    }
    return true;
}

}  // namespace seicure::crypto
