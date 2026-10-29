#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "core/crypto.hpp"
#include "core/secure_buffer.hpp"
#include "test_util.hpp"

using namespace seicure;
using crypto::Cipher;

namespace {

const std::uint8_t* ptr_or_null(const std::vector<std::uint8_t>& v) {
    return v.empty() ? nullptr : v.data();
}

std::uint8_t* mut_or_null(std::vector<std::uint8_t>& v) {
    return v.empty() ? nullptr : v.data();
}

// Ciphers this machine can actually run. AES-GCM needs hardware support, so a
// build on a CPU without it still exercises everything else.
std::vector<Cipher> usable_ciphers() {
    std::vector<Cipher> out{Cipher::XChaCha20Poly1305};
    if (crypto::cipher_available(Cipher::Aes256Gcm)) {
        out.push_back(Cipher::Aes256Gcm);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Known-answer vectors
// ---------------------------------------------------------------------------

TEST(xchacha20poly1305_known_answer_vector) {
    // From libsodium's own test suite (test/default/aead_xchacha20poly1305).
    // The message is the RFC 8439 "Ladies and Gentlemen" plaintext carried over
    // to the 24-byte-nonce XChaCha20 construction.
    auto key = tst::from_hex(
        "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f");
    auto nonce = tst::from_hex("07000000404142434445464748494a4b4c4d4e4f50515253");
    auto aad   = tst::from_hex("50515253c0c1c2c3c4c5c6c7");

    const std::string message =
        "Ladies and Gentlemen of the class of '99: If I could offer you only "
        "one tip for the future, sunscreen would be it.";
    CHECK(message.size() == 114);

    const std::string expected =
        "f8ebea4875044066fc162a0604e171feecfb3d20425248563bcfd5a155dcc47b"
        "bda70b86e5ab9b55002bd1274c02db35321acd7af8b2e2d25015e136b7679458"
        "e9f43243bf719d639badb5feac03f80a19a96ef10cb1d15333a837b90946ba38"
        "54ee74da3f2585efc7e1e170e17e15e563e77601f4f85cafa8e5877614e143e6"
        "8420";

    CHECK(key.size() == crypto::KEY_SIZE);
    CHECK(nonce.size() == crypto::XCHACHA_NONCE_SIZE);

    const crypto::Aead aead(Cipher::XChaCha20Poly1305, key.data());
    std::vector<std::uint8_t> ct(message.size());
    std::uint8_t tag[crypto::TAG_SIZE];
    aead.encrypt(nonce.data(), aad.data(), aad.size(),
                 reinterpret_cast<const std::uint8_t*>(message.data()),
                 message.size(), ct.data(), tag);

    // The vector publishes ciphertext and tag concatenated.
    const std::string actual =
        tst::to_hex(ct.data(), ct.size()) + tst::to_hex(tag, crypto::TAG_SIZE);
    if (actual != expected) {
        tst::fail("XChaCha20 mismatch\n      want: " + expected +
                      "\n      got:  " + actual,
                  __FILE__, __LINE__);
    }

    std::vector<std::uint8_t> back(ct.size());
    CHECK(aead.decrypt(nonce.data(), aad.data(), aad.size(), ct.data(),
                       ct.size(), tag, back.data()));
    CHECK(std::memcmp(back.data(), message.data(), message.size()) == 0);
}

TEST(aes_gcm_known_answer_vectors) {
    if (!crypto::cipher_available(Cipher::Aes256Gcm)) {
        return;  // no hardware AES on this CPU; nothing to assert
    }
    // AES-256 cases from the original GCM specification (McGrew & Viega).
    struct Vector {
        const char* key;
        const char* nonce;
        const char* aad;
        const char* plaintext;
        const char* ciphertext;
        const char* tag;
    };
    const Vector vectors[] = {
        {"0000000000000000000000000000000000000000000000000000000000000000",
         "000000000000000000000000", "", "", "",
         "530f8afbc74536b9a963b4f1c4cb738b"},
        {"0000000000000000000000000000000000000000000000000000000000000000",
         "000000000000000000000000", "",
         "00000000000000000000000000000000",
         "cea7403d4d606b6e074ec5d3baf39d18",
         "d0d1c8a799996bf0265b98b5d48ab919"},
        {"feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308",
         "cafebabefacedbaddecaf888",
         "feedfacedeadbeeffeedfacedeadbeefabaddad2",
         "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721"
         "c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39",
         "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
         "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662",
         "76fc6ece0f4e1768cddf8853bb2d551b"},
    };

    for (const auto& v : vectors) {
        auto key   = tst::from_hex(v.key);
        auto nonce = tst::from_hex(v.nonce);
        auto aad   = tst::from_hex(v.aad);
        auto pt    = tst::from_hex(v.plaintext);
        auto tag   = tst::from_hex(v.tag);

        CHECK(nonce.size() == crypto::AES_NONCE_SIZE);
        const crypto::Aead aead(Cipher::Aes256Gcm, key.data());

        std::vector<std::uint8_t> ct(pt.size());
        std::uint8_t out_tag[crypto::TAG_SIZE];
        aead.encrypt(nonce.data(), ptr_or_null(aad), aad.size(),
                     ptr_or_null(pt), pt.size(), mut_or_null(ct), out_tag);

        if (!ct.empty()) {
            CHECK_EQ_HEX(ct.data(), ct.size(), v.ciphertext);
        }
        CHECK_EQ_HEX(out_tag, crypto::TAG_SIZE, v.tag);

        std::vector<std::uint8_t> back(ct.size());
        CHECK(aead.decrypt(nonce.data(), ptr_or_null(aad), aad.size(),
                           ptr_or_null(ct), ct.size(), tag.data(),
                           mut_or_null(back)));
        if (!pt.empty()) {
            CHECK_EQ_HEX(back.data(), back.size(), v.plaintext);
        }
    }
}

// ---------------------------------------------------------------------------
// Properties that must hold for every cipher
// ---------------------------------------------------------------------------

TEST(aead_round_trips_for_every_cipher) {
    for (Cipher cipher : usable_ciphers()) {
        std::uint8_t key[crypto::KEY_SIZE];
        std::uint8_t nonce[crypto::MAX_NONCE_SIZE];
        crypto::random_bytes(key, sizeof(key));
        crypto::random_bytes(nonce, crypto::nonce_size(cipher));

        const crypto::Aead aead(cipher, key);
        for (std::size_t n : {std::size_t{0}, std::size_t{1}, std::size_t{4096}}) {
            std::vector<std::uint8_t> pt(n, 0x5A);
            std::vector<std::uint8_t> ct(n);
            std::uint8_t tag[crypto::TAG_SIZE];
            aead.encrypt(nonce, nullptr, 0, ptr_or_null(pt), n, mut_or_null(ct),
                         tag);

            std::vector<std::uint8_t> back(n);
            CHECK(aead.decrypt(nonce, nullptr, 0, ptr_or_null(ct), n, tag,
                               mut_or_null(back)));
            CHECK(back == pt);
        }
    }
}

TEST(aead_rejects_tampering_for_every_cipher) {
    for (Cipher cipher : usable_ciphers()) {
        std::uint8_t key[crypto::KEY_SIZE];
        std::uint8_t nonce[crypto::MAX_NONCE_SIZE];
        crypto::random_bytes(key, sizeof(key));
        crypto::random_bytes(nonce, crypto::nonce_size(cipher));

        const crypto::Aead aead(cipher, key);
        std::vector<std::uint8_t> pt(1000, 0xAB);
        std::vector<std::uint8_t> ct(pt.size());
        std::uint8_t aad[] = {1, 2, 3, 4, 5, 6, 7, 8};
        std::uint8_t tag[crypto::TAG_SIZE];
        aead.encrypt(nonce, aad, sizeof(aad), pt.data(), pt.size(), ct.data(),
                     tag);

        std::vector<std::uint8_t> out(pt.size());

        // Ciphertext bit flips.
        for (std::size_t at : {std::size_t{0}, pt.size() / 2, pt.size() - 1}) {
            auto bad = ct;
            bad[at] ^= 0x01;
            CHECK(!aead.decrypt(nonce, aad, sizeof(aad), bad.data(), bad.size(),
                                tag, out.data()));
        }

        // Tag.
        std::uint8_t bad_tag[crypto::TAG_SIZE];
        std::memcpy(bad_tag, tag, sizeof(tag));
        bad_tag[crypto::TAG_SIZE - 1] ^= 0x80;
        CHECK(!aead.decrypt(nonce, aad, sizeof(aad), ct.data(), ct.size(),
                            bad_tag, out.data()));

        // AAD -- this is what protects the header and the chunk index.
        aad[3] ^= 0xFF;
        CHECK(!aead.decrypt(nonce, aad, sizeof(aad), ct.data(), ct.size(), tag,
                            out.data()));
        aad[3] ^= 0xFF;

        // Wrong key.
        std::uint8_t other_key[crypto::KEY_SIZE];
        std::memcpy(other_key, key, sizeof(key));
        other_key[0] ^= 0x01;
        const crypto::Aead other(cipher, other_key);
        CHECK(!other.decrypt(nonce, aad, sizeof(aad), ct.data(), ct.size(), tag,
                             out.data()));

        // Wrong nonce.
        std::uint8_t other_nonce[crypto::MAX_NONCE_SIZE];
        std::memcpy(other_nonce, nonce, crypto::MAX_NONCE_SIZE);
        other_nonce[0] ^= 0x01;
        CHECK(!aead.decrypt(other_nonce, aad, sizeof(aad), ct.data(), ct.size(),
                            tag, out.data()));
    }
}

TEST(cipher_metadata_is_consistent) {
    CHECK(crypto::nonce_size(Cipher::Aes256Gcm) == 12);
    CHECK(crypto::nonce_size(Cipher::XChaCha20Poly1305) == 24);
    CHECK(crypto::MAX_NONCE_SIZE == 24);
    // XChaCha20 is add/rotate/xor only, so it is available everywhere.
    CHECK(crypto::cipher_available(Cipher::XChaCha20Poly1305));
    CHECK(std::string(crypto::cipher_name(Cipher::XChaCha20Poly1305)) ==
          "XChaCha20-Poly1305");
}

TEST(random_bytes_are_distinct) {
    // Not a randomness quality test -- it catches a stubbed or failing RNG,
    // which would be silent and fatal.
    std::set<std::string> seen;
    for (int i = 0; i < 64; ++i) {
        std::uint8_t buf[32];
        crypto::random_bytes(buf, sizeof(buf));
        CHECK(seen.insert(tst::to_hex(buf, sizeof(buf))).second);
    }

    std::uint8_t zero[32]{};
    std::uint8_t buf[32]{};
    crypto::random_bytes(buf, sizeof(buf));
    CHECK(std::memcmp(buf, zero, sizeof(buf)) != 0);
}

TEST(secure_buffer_wipes_and_moves) {
    SecureBuffer a(64);
    CHECK(a.size() == 64);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i] == 0);
    }
    a[0] = 0xFF;

    SecureBuffer b = std::move(a);
    CHECK(b.size() == 64);
    CHECK(b[0] == 0xFF);
    CHECK(a.size() == 0);
    CHECK(a.data() == nullptr);
}

TEST(constant_time_equal_matches_memcmp) {
    std::uint8_t x[32], y[32];
    crypto::random_bytes(x, sizeof(x));
    std::memcpy(y, x, sizeof(x));
    CHECK(constant_time_equal(x, y, sizeof(x)));

    for (std::size_t i : {std::size_t{0}, std::size_t{15}, std::size_t{31}}) {
        y[i] ^= 0x01;
        CHECK(!constant_time_equal(x, y, sizeof(x)));
        y[i] ^= 0x01;
    }
}
