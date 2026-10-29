#include "core/secure_buffer.hpp"

#include <sodium.h>

#include <new>
#include <utility>

// Backed by libsodium rather than hand-rolled Win32 calls. sodium_malloc gives
// more than the previous malloc + VirtualLock did -- guard pages either side of
// the allocation, a canary to catch overflows, and the pages locked out of the
// page file -- and it is the same on Windows, macOS and Linux.
namespace seicure {
namespace {

// sodium_init() must run before the secure allocator is used.
struct SodiumInit {
    SodiumInit() { sodium_init(); }
};
const SodiumInit g_sodium_init;

}  // namespace

void secure_zero(void* p, std::size_t n) noexcept {
    if (p != nullptr && n != 0) {
        sodium_memzero(p, n);
    }
}

bool constant_time_equal(const void* a, const void* b, std::size_t n) noexcept {
    if (n == 0) {
        return true;
    }
    return sodium_memcmp(a, b, n) == 0;
}

SecureBuffer::SecureBuffer(std::size_t n) {
    resize(n);
}

SecureBuffer::~SecureBuffer() {
    reset();
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
    : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, std::size_t{0});
    }
    return *this;
}

void SecureBuffer::resize(std::size_t n) {
    reset();
    if (n == 0) {
        return;
    }
    auto* p = static_cast<std::uint8_t*>(sodium_malloc(n));
    if (p == nullptr) {
        throw std::bad_alloc();
    }
    data_ = p;
    size_ = n;
    sodium_memzero(data_, size_);
}

void SecureBuffer::reset() noexcept {
    if (data_ != nullptr) {
        // sodium_free wipes before releasing, but be explicit: the intent
        // should not depend on a library detail.
        sodium_memzero(data_, size_);
        sodium_free(data_);
    }
    data_ = nullptr;
    size_ = 0;
}

}  // namespace seicure
