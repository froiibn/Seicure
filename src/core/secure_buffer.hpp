#pragma once

#include <cstddef>
#include <cstdint>

namespace seicure {

// Zeroes memory such that the optimizer cannot elide the write. Plain memset on
// a buffer that is never read again is dead-store-eliminated by every modern
// compiler, which is exactly how key material survives in freed memory.
void secure_zero(void* p, std::size_t n) noexcept;

// Constant-time comparison. Use for tags and any other secret-dependent
// equality test; a short-circuiting compare leaks a byte-position oracle.
[[nodiscard]] bool constant_time_equal(const void* a, const void* b,
                                       std::size_t n) noexcept;

// Owning byte buffer allocated with guard pages and a canary, locked out of the
// page file, and wiped on release. Backed by libsodium's secure allocator, so
// the guarantees are identical on every platform.
class SecureBuffer {
public:
    SecureBuffer() noexcept = default;
    explicit SecureBuffer(std::size_t n);
    ~SecureBuffer();

    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;

    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;

    void resize(std::size_t n);
    void reset() noexcept;

    [[nodiscard]] std::uint8_t*       data()       noexcept { return data_; }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t         size() const noexcept { return size_; }
    [[nodiscard]] bool                empty() const noexcept { return size_ == 0; }

    [[nodiscard]] std::uint8_t& operator[](std::size_t i) noexcept { return data_[i]; }
    [[nodiscard]] const std::uint8_t& operator[](std::size_t i) const noexcept { return data_[i]; }

private:
    std::uint8_t* data_ = nullptr;
    std::size_t   size_ = 0;
};

}  // namespace seicure
