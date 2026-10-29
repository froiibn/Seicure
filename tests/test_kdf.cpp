#include <cstring>
#include <string>

#include "core/kdf.hpp"
#include "test_util.hpp"

using namespace seicure;

namespace {

// Cross-implementation vectors. These were produced by the phc-winner-argon2
// reference implementation -- which passes the official Argon2 test suite --
// using the parameter shape libsodium's crypto_pwhash imposes: a 16-byte salt
// and a single lane. Agreement between two independent implementations is a
// stronger check than either one against itself.
struct Vector {
    std::uint32_t iterations;
    std::uint32_t memory_kib;
    const char*   expected_hex;
};

const Vector kVectors[] = {
    {3, 65536,
     "0d1a3c6523c8f06e4e0af9c515aa5b5448cfebd6838f2d52c3d8b6ef8ddc3c2e"},
    {1, 8192,
     "9aeef75313e585f492c33c5e12d82e9ad253b7dd78632d537e899cd4eddd789c"},
    {2, 32768,
     "a3eaeba89d069458afff8b7d965ea12de292418ded91b305cb494aa56d361b50"},
};

const char kPassword[] = "correct horse battery staple";

void fill_salt(std::uint8_t salt[kdf::SALT_SIZE]) {
    for (std::size_t i = 0; i < kdf::SALT_SIZE; ++i) {
        salt[i] = static_cast<std::uint8_t>(i);
    }
}

}  // namespace

TEST(argon2id_matches_reference_implementation) {
    std::uint8_t salt[kdf::SALT_SIZE];
    fill_salt(salt);

    for (const auto& v : kVectors) {
        kdf::Params params{};
        params.memory_kib  = v.memory_kib;
        params.iterations  = v.iterations;
        params.parallelism = 1;

        std::uint8_t key[crypto::KEY_SIZE]{};
        kdf::derive_key(reinterpret_cast<const std::uint8_t*>(kPassword),
                        std::strlen(kPassword), salt, params, key);
        CHECK_EQ_HEX(key, sizeof(key), v.expected_hex);
    }
}

TEST(derive_key_is_deterministic_and_salt_dependent) {
    std::uint8_t salt[kdf::SALT_SIZE];
    fill_salt(salt);

    kdf::Params params{};
    params.memory_kib = 8192;  // keep the suite fast
    params.iterations = 1;

    const std::string password = "hunter2";
    const auto* pw = reinterpret_cast<const std::uint8_t*>(password.data());

    std::uint8_t a[crypto::KEY_SIZE]{};
    std::uint8_t b[crypto::KEY_SIZE]{};
    kdf::derive_key(pw, password.size(), salt, params, a);
    kdf::derive_key(pw, password.size(), salt, params, b);
    CHECK(std::memcmp(a, b, sizeof(a)) == 0);

    // A different salt must give a different key -- that is what stops one
    // rainbow table from covering every file.
    salt[0] ^= 0xFF;
    std::uint8_t c[crypto::KEY_SIZE]{};
    kdf::derive_key(pw, password.size(), salt, params, c);
    CHECK(std::memcmp(a, c, sizeof(a)) != 0);
}

TEST(derive_key_handles_embedded_nul) {
    // Passwords are length-delimited byte strings, not C strings. Treated as a
    // C string, "ab\0cd" and "ab" would collide.
    std::uint8_t salt[kdf::SALT_SIZE]{};
    kdf::Params params{};
    params.memory_kib = 8192;
    params.iterations = 1;

    const std::uint8_t long_pw[]  = {'a', 'b', 0, 'c', 'd'};
    const std::uint8_t short_pw[] = {'a', 'b'};

    std::uint8_t a[crypto::KEY_SIZE]{};
    std::uint8_t b[crypto::KEY_SIZE]{};
    kdf::derive_key(long_pw, sizeof(long_pw), salt, params, a);
    kdf::derive_key(short_pw, sizeof(short_pw), salt, params, b);
    CHECK(std::memcmp(a, b, sizeof(a)) != 0);
}

TEST(kdf_params_reject_out_of_range) {
    kdf::Params p{};
    CHECK(p.valid());          // defaults must be sane
    CHECK(p.parallelism == 1);
    CHECK(p.memory_kib == 256u * 1024u);
    CHECK(p.iterations == 3);

    // A crafted header must not be able to request an unbounded allocation,
    // a cost of zero, or a lane count the implementation cannot honour.
    CHECK(!(kdf::Params{0, 3, 1}).valid());
    CHECK(!(kdf::Params{4u * 1024u * 1024u, 3, 1}).valid());  // 4 GiB
    CHECK(!(kdf::Params{65536, 0, 1}).valid());
    CHECK(!(kdf::Params{65536, 3, 0}).valid());
    CHECK(!(kdf::Params{65536, 3, 4}).valid());  // libsodium is single-lane

    std::uint8_t salt[kdf::SALT_SIZE]{};
    std::uint8_t key[crypto::KEY_SIZE]{};
    const std::uint8_t pw[] = {'x'};
    CHECK_THROWS(kdf::derive_key(pw, sizeof(pw), salt,
                                 kdf::Params{0, 3, 1}, key));
}
