#include "core/stream.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/crypto.hpp"
#include "core/secure_buffer.hpp"

namespace seicure::stream {
namespace {

std::uint64_t chunk_count(std::uint64_t padded_size, std::uint32_t chunk_size) {
    // padme() never returns 0, so there is always at least one chunk.
    return (padded_size + chunk_size - 1) / chunk_size;
}

void read_exact(Reader& in, std::uint8_t* out, std::size_t n,
                const char* what) {
    std::size_t total = 0;
    while (total < n) {
        const std::size_t got = in.read(out + total, n - total);
        if (got == 0) {
            throw AuthError(std::string("truncated file (reading ") + what + ")");
        }
        total += got;
    }
}

void report(const ProgressFn& progress, std::uint64_t done, std::uint64_t total) {
    if (progress && !progress(done, total)) {
        throw CancelledError();
    }
}

// Derives the KEK and unwraps (or wraps) the DEK. The header's first
// HEADER_AAD_SIZE bytes go in as AAD, which is what authenticates the KDF
// parameters, salt and chunk size without a separate MAC.
void wrap_dek(const std::uint8_t* password, std::size_t password_len,
              format::Header& header, const std::uint8_t* header_bytes,
              const std::uint8_t dek[crypto::KEY_SIZE]) {
    SecureBuffer kek(crypto::KEY_SIZE);
    kdf::derive_key(password, password_len, header.salt, header.kdf_params,
                    kek.data());
    const crypto::Aead wrapper(header.cipher, kek.data());
    wrapper.encrypt(header.dek_wrap_nonce, header_bytes, format::HEADER_AAD_SIZE,
                    dek, crypto::KEY_SIZE, header.wrapped_dek,
                    header.dek_wrap_tag);
}

[[nodiscard]] bool unwrap_dek(const std::uint8_t* password,
                              std::size_t password_len,
                              const format::Header& header,
                              const std::uint8_t* header_bytes,
                              std::uint8_t out_dek[crypto::KEY_SIZE]) {
    SecureBuffer kek(crypto::KEY_SIZE);
    kdf::derive_key(password, password_len, header.salt, header.kdf_params,
                    kek.data());
    const crypto::Aead wrapper(header.cipher, kek.data());
    return wrapper.decrypt(header.dek_wrap_nonce, header_bytes,
                           format::HEADER_AAD_SIZE, header.wrapped_dek,
                           crypto::KEY_SIZE, header.dek_wrap_tag, out_dek);
}

}  // namespace

std::uint64_t ciphertext_size(std::uint64_t plaintext_size,
                              std::uint32_t chunk_size) {
    const std::uint64_t padded = format::padme(plaintext_size);
    const std::uint64_t chunks = chunk_count(padded, chunk_size);
    return format::HEADER_SIZE
         + format::METADATA_SIZE + crypto::TAG_SIZE
         + padded + chunks * crypto::TAG_SIZE;
}

void encrypt(Reader& in, std::uint64_t input_size,
             const format::Metadata& metadata,
             const std::uint8_t* password, std::size_t password_len,
             Writer& out, const Options& options, const ProgressFn& progress) {
    if (options.chunk_size < format::MIN_CHUNK_SIZE ||
        options.chunk_size > format::MAX_CHUNK_SIZE) {
        throw format::FormatError("chunk size out of range");
    }

    const std::uint64_t padded = format::padme(input_size);
    const std::uint64_t chunks = chunk_count(padded, options.chunk_size);
    if (chunks > format::max_chunks()) {
        throw format::FormatError("input too large for the nonce counter");
    }

    // --- header ------------------------------------------------------------
    format::Header header;
    header.cipher     = options.cipher;
    header.kdf_params = options.kdf_params;
    header.chunk_size = options.chunk_size;
    crypto::random_bytes(header.salt, kdf::SALT_SIZE);
    crypto::random_bytes(header.base_nonce, format::BASE_NONCE_SIZE);
    crypto::random_bytes(header.dek_wrap_nonce, crypto::MAX_NONCE_SIZE);

    SecureBuffer dek(crypto::KEY_SIZE);
    crypto::random_bytes(dek.data(), crypto::KEY_SIZE);

    std::uint8_t header_bytes[format::HEADER_SIZE];
    header.serialize(header_bytes);  // wrap fields still zero; AAD covers 0..64
    wrap_dek(password, password_len, header, header_bytes, dek.data());
    header.serialize(header_bytes);  // re-emit, now with the wrapped DEK
    out.write(header_bytes, format::HEADER_SIZE);

    const crypto::Aead aead(header.cipher, dek.data());

    // --- metadata block ----------------------------------------------------
    {
        format::Metadata meta = metadata;
        meta.original_size = input_size;  // authoritative; caller cannot desync it

        SecureBuffer plain(format::METADATA_SIZE);
        meta.serialize(plain.data());

        std::uint8_t nonce[crypto::MAX_NONCE_SIZE];
        std::uint8_t aad[format::AAD_SIZE];
        format::make_nonce(header.base_nonce, format::METADATA_COUNTER, header.cipher, nonce);
        format::make_aad(format::BlockType::Metadata, 0, true, aad);

        std::vector<std::uint8_t> ct(format::METADATA_SIZE);
        std::uint8_t tag[crypto::TAG_SIZE];
        aead.encrypt(nonce, aad, sizeof(aad), plain.data(),
                       format::METADATA_SIZE, ct.data(), tag);
        out.write(ct.data(), ct.size());
        out.write(tag, sizeof(tag));
    }

    // --- data chunks -------------------------------------------------------
    SecureBuffer plain(options.chunk_size);
    std::vector<std::uint8_t> ct(options.chunk_size);
    std::uint64_t written = 0;

    report(progress, 0, input_size);

    for (std::uint64_t i = 0; i < chunks; ++i) {
        const std::uint64_t remaining_padded = padded - written;
        const std::size_t this_chunk = static_cast<std::size_t>(
            (std::min)(static_cast<std::uint64_t>(options.chunk_size),
                       remaining_padded));

        // Fill from the input, then zero-pad the tail. The padding is inside
        // the AEAD, so it is indistinguishable from real data on disk.
        std::size_t filled = 0;
        while (filled < this_chunk) {
            const std::size_t got =
                in.read(plain.data() + filled, this_chunk - filled);
            if (got == 0) break;
            filled += got;
        }
        if (filled < this_chunk) {
            std::memset(plain.data() + filled, 0, this_chunk - filled);
        }

        std::uint8_t nonce[crypto::MAX_NONCE_SIZE];
        std::uint8_t aad[format::AAD_SIZE];
        format::make_nonce(header.base_nonce, format::chunk_counter(i), header.cipher, nonce);
        format::make_aad(format::BlockType::Data, i, i + 1 == chunks, aad);

        std::uint8_t tag[crypto::TAG_SIZE];
        aead.encrypt(nonce, aad, sizeof(aad), plain.data(), this_chunk,
                       ct.data(), tag);
        out.write(ct.data(), this_chunk);
        out.write(tag, sizeof(tag));

        written += this_chunk;
        report(progress, (std::min)(written, input_size), input_size);
    }
}

format::Metadata decrypt(Reader& in,
                         const std::uint8_t* password, std::size_t password_len,
                         Writer& out, const ProgressFn& progress,
                         const MetadataFn& on_metadata) {
    // --- header ------------------------------------------------------------
    std::uint8_t header_bytes[format::HEADER_SIZE];
    read_exact(in, header_bytes, format::HEADER_SIZE, "header");
    const format::Header header = format::Header::parse(header_bytes);

    SecureBuffer dek(crypto::KEY_SIZE);
    if (!unwrap_dek(password, password_len, header, header_bytes, dek.data())) {
        // Wrong password and a tampered header are intentionally the same
        // error: distinguishing them hands an attacker a free oracle.
        throw AuthError("wrong password or corrupted file");
    }

    const crypto::Aead aead(header.cipher, dek.data());

    // --- metadata block ----------------------------------------------------
    format::Metadata metadata;
    {
        std::vector<std::uint8_t> ct(format::METADATA_SIZE);
        std::uint8_t tag[crypto::TAG_SIZE];
        read_exact(in, ct.data(), ct.size(), "metadata");
        read_exact(in, tag, sizeof(tag), "metadata tag");

        std::uint8_t nonce[crypto::MAX_NONCE_SIZE];
        std::uint8_t aad[format::AAD_SIZE];
        format::make_nonce(header.base_nonce, format::METADATA_COUNTER, header.cipher, nonce);
        format::make_aad(format::BlockType::Metadata, 0, true, aad);

        SecureBuffer plain(format::METADATA_SIZE);
        if (!aead.decrypt(nonce, aad, sizeof(aad), ct.data(), ct.size(), tag,
                            plain.data())) {
            throw AuthError("metadata block failed authentication");
        }
        metadata = format::Metadata::parse(plain.data());
    }

    // Authenticated at this point, so the caller can safely act on it.
    if (on_metadata) {
        on_metadata(metadata);
    }

    // Chunk layout is derived entirely from the authenticated original_size,
    // never from the length of the file on disk. That is what makes appending
    // or truncating detectable rather than silently accepted.
    const std::uint64_t padded = format::padme(metadata.original_size);
    const std::uint64_t chunks = chunk_count(padded, header.chunk_size);

    SecureBuffer plain(header.chunk_size);
    std::vector<std::uint8_t> ct(header.chunk_size);
    std::uint64_t consumed = 0;   // padded bytes decrypted so far
    std::uint64_t emitted  = 0;   // real bytes handed to the writer

    report(progress, 0, metadata.original_size);

    for (std::uint64_t i = 0; i < chunks; ++i) {
        const std::size_t this_chunk = static_cast<std::size_t>(
            (std::min)(static_cast<std::uint64_t>(header.chunk_size),
                       padded - consumed));

        std::uint8_t tag[crypto::TAG_SIZE];
        read_exact(in, ct.data(), this_chunk, "chunk");
        read_exact(in, tag, sizeof(tag), "chunk tag");

        std::uint8_t nonce[crypto::MAX_NONCE_SIZE];
        std::uint8_t aad[format::AAD_SIZE];
        format::make_nonce(header.base_nonce, format::chunk_counter(i), header.cipher, nonce);
        format::make_aad(format::BlockType::Data, i, i + 1 == chunks, aad);

        if (!aead.decrypt(nonce, aad, sizeof(aad), ct.data(), this_chunk, tag,
                            plain.data())) {
            throw AuthError("chunk failed authentication");
        }

        // Emit only real bytes; the Padmé padding is dropped here.
        const std::uint64_t real_left = metadata.original_size - emitted;
        const std::size_t to_emit = static_cast<std::size_t>(
            (std::min)(static_cast<std::uint64_t>(this_chunk), real_left));
        if (to_emit > 0) {
            out.write(plain.data(), to_emit);
            emitted += to_emit;
        }

        consumed += this_chunk;
        report(progress, emitted, metadata.original_size);
    }

    // Anything left over means bytes were appended after the last chunk.
    std::uint8_t extra = 0;
    if (in.read(&extra, 1) != 0) {
        throw AuthError("trailing data after final chunk");
    }

    return metadata;
}

}  // namespace seicure::stream
