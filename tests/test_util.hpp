#pragma once

// Deliberately dependency-free test harness. Pulling in GoogleTest or Catch2
// would be the only third-party build dependency besides Argon2, and the suite
// does not need more than this.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace tst {

struct TestCase {
    std::string name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

[[noreturn]] inline void fail(const std::string& msg, const char* file, int line) {
    throw std::runtime_error(std::string(file) + ":" + std::to_string(line) +
                             "\n    " + msg);
}

// "a1b2" -> bytes. Throws on malformed input so a typo in a test vector is
// caught rather than silently producing a short buffer.
inline std::vector<std::uint8_t> from_hex(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        throw std::runtime_error("from_hex: odd length");
    }
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("from_hex: bad digit");
    };
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(nibble(hex[i]) * 16 +
                                                nibble(hex[i + 1])));
    }
    return out;
}

inline std::string to_hex(const std::uint8_t* p, std::size_t n) {
    static const char* kDigits = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out.push_back(kDigits[p[i] >> 4]);
        out.push_back(kDigits[p[i] & 0x0F]);
    }
    return out;
}

}  // namespace tst

#define TEST(name)                                          \
    static void name();                                     \
    static ::tst::Registrar tst_reg_##name(#name, &name);   \
    static void name()

#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) ::tst::fail("CHECK failed: " #expr, __FILE__, __LINE__); \
    } while (0)

#define CHECK_EQ_HEX(ptr, len, expected_hex)                                  \
    do {                                                                      \
        std::string actual_ = ::tst::to_hex((ptr), (len));                    \
        std::string want_   = (expected_hex);                                 \
        if (actual_ != want_) {                                               \
            ::tst::fail("mismatch\n      want: " + want_ +                    \
                        "\n      got:  " + actual_, __FILE__, __LINE__);      \
        }                                                                     \
    } while (0)

#define CHECK_THROWS(expr)                                                \
    do {                                                                  \
        bool threw_ = false;                                              \
        try { (void)(expr); } catch (...) { threw_ = true; }              \
        if (!threw_)                                                      \
            ::tst::fail("expected throw: " #expr, __FILE__, __LINE__);    \
    } while (0)
