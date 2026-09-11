#pragma once

// A single AES-128 block, which is all the Bluetooth address resolution and the
// advertisement payload need. OpenSSL rather than a hand written cipher: this
// is the one piece of this project where writing it ourselves would be worse
// than taking a dependency.

#include <openssl/evp.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace bt
{

inline constexpr size_t kAesBlockSize = 16;

using AesBlock = std::array<uint8_t, kAesBlockSize>;

namespace detail
{

inline std::optional<AesBlock>
Crypt(std::span<const uint8_t> key, std::span<const uint8_t> block, bool encrypting)
{
    if (key.size() != kAesBlockSize || block.size() != kAesBlockSize)
    {
        return std::nullopt;
    }

    EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
    if (context == nullptr)
    {
        return std::nullopt;
    }

    AesBlock output{};
    int written = 0;
    bool ok = false;

    // ECB with padding disabled: exactly one block in, one block out. The
    // payload is nominally CBC with a zero IV, which for a single block is
    // the same transform.
    const int started =
        encrypting ? EVP_EncryptInit_ex(context, EVP_aes_128_ecb(), nullptr, key.data(), nullptr)
                   : EVP_DecryptInit_ex(context, EVP_aes_128_ecb(), nullptr, key.data(), nullptr);

    if (started == 1)
    {
        EVP_CIPHER_CTX_set_padding(context, 0);

        ok =
            encrypting
                ? EVP_EncryptUpdate(
                      context, output.data(), &written, block.data(), static_cast<int>(block.size())
                  ) == 1
                : EVP_DecryptUpdate(
                      context, output.data(), &written, block.data(), static_cast<int>(block.size())
                  ) == 1;
    }

    EVP_CIPHER_CTX_free(context);

    if (!ok || written != static_cast<int>(kAesBlockSize))
    {
        return std::nullopt;
    }

    return output;
}

} // namespace detail

inline std::optional<AesBlock>
AesEncryptBlock(std::span<const uint8_t> key, std::span<const uint8_t> block)
{
    return detail::Crypt(key, block, true);
}

inline std::optional<AesBlock>
AesDecryptBlock(std::span<const uint8_t> key, std::span<const uint8_t> block)
{
    return detail::Crypt(key, block, false);
}

} // namespace bt
