#include "core/kdf.hpp"

#include <sodium.h>

#include <string>

namespace seicure::kdf {
namespace {

// Upper bounds blunt a denial-of-service via crafted headers: the KDF cost is
// read from an untrusted file before anything can be authenticated, so without
// a cap an attacker could request a 64 GiB allocation.
constexpr std::uint32_t kMinMemoryKib  = 8;
constexpr std::uint32_t kMaxMemoryKib  = 2u * 1024u * 1024u;  // 2 GiB
constexpr std::uint32_t kMaxIterations = 1u << 20;

static_assert(SALT_SIZE == crypto_pwhash_SALTBYTES,
              "header salt width must match what crypto_pwhash expects");
static_assert(crypto::KEY_SIZE >= crypto_pwhash_BYTES_MIN,
              "derived key is shorter than crypto_pwhash allows");

}  // namespace

bool Params::valid() const noexcept {
    if (memory_kib < kMinMemoryKib || memory_kib > kMaxMemoryKib) return false;
    if (iterations < 1 || iterations > kMaxIterations)            return false;
    // libsodium's crypto_pwhash always uses a single lane; anything else in a
    // header would silently produce a different key than it claims.
    if (parallelism != 1)                                         return false;
    return true;
}

void derive_key(const std::uint8_t* password, std::size_t password_len,
                const std::uint8_t salt[SALT_SIZE], const Params& params,
                std::uint8_t out_key[crypto::KEY_SIZE]) {
    if (!params.valid()) {
        throw crypto::CryptoError("Argon2id: cost parameters out of range");
    }
    crypto::init();

    const int rc = crypto_pwhash(
        out_key, crypto::KEY_SIZE,
        reinterpret_cast<const char*>(password), password_len,
        salt,
        params.iterations,
        static_cast<std::size_t>(params.memory_kib) * 1024u,
        crypto_pwhash_ALG_ARGON2ID13);

    if (rc != 0) {
        // The only documented failure is running out of memory for the
        // requested cost.
        throw crypto::CryptoError(
            "Argon2id failed: could not allocate " +
            std::to_string(params.memory_kib / 1024) + " MiB");
    }
}

}  // namespace seicure::kdf
