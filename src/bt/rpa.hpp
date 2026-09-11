#pragma once

// Resolvable private address checking, per the Bluetooth core specification.
//
// AirPods rotate their advertising address on every lid open, so an address is
// no use for recognising them. The identity resolving key the buds hand out
// turns that around: the top half of the address is a hash of the bottom half
// under the key, so only a holder of the key can tell the broadcasts apart
// from any other AirPods in range.

#include "bt/aes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace bt
{

namespace detail
{

inline constexpr size_t kAddressBytes = 6;
inline constexpr size_t kHashBytes = 3;

// The specification's e() works on little endian words, so the key and the
// block are reversed going in and the result is reversed coming out.
inline std::optional<AesBlock> SpecEncrypt(std::span<const uint8_t> key, const AesBlock &block)
{
    AesBlock reversedKey{};
    AesBlock reversedBlock = block;

    if (key.size() != kAesBlockSize)
    {
        return std::nullopt;
    }

    std::reverse_copy(key.begin(), key.end(), reversedKey.begin());
    std::reverse(reversedBlock.begin(), reversedBlock.end());

    auto encrypted = AesEncryptBlock(reversedKey, reversedBlock);
    if (!encrypted)
    {
        return std::nullopt;
    }

    std::reverse(encrypted->begin(), encrypted->end());
    return encrypted;
}

// ah(k, r): r occupies the low three bytes of an otherwise zero block.
inline std::optional<std::array<uint8_t, kHashBytes>>
AddressHash(std::span<const uint8_t> key, std::span<const uint8_t> random)
{
    if (random.size() < kHashBytes)
    {
        return std::nullopt;
    }

    AesBlock padded{};
    std::copy_n(random.begin(), kHashBytes, padded.begin());

    const auto encrypted = SpecEncrypt(key, padded);
    if (!encrypted)
    {
        return std::nullopt;
    }

    std::array<uint8_t, kHashBytes> hash{};
    std::copy_n(encrypted->begin(), kHashBytes, hash.begin());
    return hash;
}

inline std::optional<std::array<uint8_t, kAddressBytes>> ParseAddress(const std::string &address)
{
    constexpr size_t kTextLength = (kAddressBytes * 3) - 1;
    constexpr int kHexBase = 16;

    if (address.size() != kTextLength)
    {
        return std::nullopt;
    }

    std::array<uint8_t, kAddressBytes> bytes{};

    for (size_t i = 0; i < kAddressBytes; ++i)
    {
        const size_t offset = i * 3;

        if (i > 0 && address[offset - 1] != ':')
        {
            return std::nullopt;
        }

        const std::string pair = address.substr(offset, 2);
        size_t consumed = 0;
        const unsigned long value = std::stoul(pair, &consumed, kHexBase);

        if (consumed != 2)
        {
            return std::nullopt;
        }

        // Stored little endian, so the written order is reversed.
        bytes[kAddressBytes - 1 - i] = static_cast<uint8_t>(value);
    }

    return bytes;
}

} // namespace detail

// True when this address was generated from this key.
inline bool AddressBelongsTo(const std::string &address, std::span<const uint8_t> irk)
{
    const auto bytes = detail::ParseAddress(address);
    if (!bytes)
    {
        return false;
    }

    const std::span<const uint8_t> stored(*bytes);
    const std::span<const uint8_t> hash = stored.first(detail::kHashBytes);
    const std::span<const uint8_t> random = stored.subspan(detail::kHashBytes);

    const auto expected = detail::AddressHash(irk, random);
    if (!expected)
    {
        return false;
    }

    return std::equal(expected->begin(), expected->end(), hash.begin());
}

} // namespace bt
