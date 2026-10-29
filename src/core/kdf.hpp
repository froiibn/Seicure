#pragma once

#include <cstddef>
#include <cstdint>

#include "core/crypto.hpp"
#include "core/secure_buffer.hpp"

namespace seicure::kdf {

inline constexpr std::size_t SALT_SIZE = 16;

// Argon2id cost parameters, stored in the file header so a container encrypted
// with today's defaults still opens after the defaults are raised.
//
// The defaults match libsodium's "moderate" profile. Parallelism is fixed at 1
// because libsodium's crypto_pwhash does not expose lanes; that costs wall
// clock on a multi-core machine but takes nothing away from an attacker, who
// parallelises across guesses rather than within one.
struct Params {
    std::uint32_t memory_kib  = 256u * 1024u;  // 256 MiB
    std::uint32_t iterations  = 3;
    std::uint32_t parallelism = 1;

    [[nodiscard]] bool valid() const noexcept;
};

// Derives a KEK from a password. `password` is raw bytes (UTF-8), not a
// null-terminated string, so embedded NULs are handled correctly.
//
// Throws CryptoError on failure -- notably when `memory_kib` cannot be
// allocated, which is reachable if a crafted header requests an absurd cost.
void derive_key(const std::uint8_t* password, std::size_t password_len,
                const std::uint8_t salt[SALT_SIZE], const Params& params,
                std::uint8_t out_key[crypto::KEY_SIZE]);

}  // namespace seicure::kdf
