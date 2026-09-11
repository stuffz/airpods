#pragma once

// The reply to a proximity key request: a count, then one length-prefixed key
// each. The IRK resolves the rotating advertisement address; the encryption
// key belongs to the payload's encrypted tail, whose contents are not known.

#include "aap/packets.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace aap
{

enum class KeyType : uint8_t
{
    Irk = 0x01,
    Encryption = 0x04
};

struct ProximityKey
{
    KeyType type = KeyType::Irk;
    std::vector<uint8_t> material;
};

inline std::string_view KeyTypeName(KeyType type)
{
    switch (type)
    {
    case KeyType::Irk:
        return "IRK";
    case KeyType::Encryption:
        return "ENC_KEY";
    }

    return "unknown";
}

inline std::vector<ProximityKey> ParseProximityKeys(std::span<const uint8_t> packet)
{
    constexpr size_t kCountOffset = 6;
    constexpr size_t kFirstEntry = 7;
    constexpr size_t kLengthOffset = 2;
    constexpr size_t kEntryHeader = 4;

    std::vector<ProximityKey> keys;

    if (!StartsWith(packet, kProximityKeysReply) || packet.size() <= kCountOffset)
    {
        return keys;
    }

    const uint8_t count = packet[kCountOffset];
    size_t offset = kFirstEntry;

    for (uint8_t i = 0; i < count; ++i)
    {
        if (offset + kEntryHeader > packet.size())
        {
            break;
        }

        const auto type = static_cast<KeyType>(packet[offset]);
        const size_t length = packet[offset + kLengthOffset];

        offset += kEntryHeader;

        if (offset + length > packet.size())
        {
            break;
        }

        keys.push_back(
            ProximityKey{
                type, std::vector<uint8_t>(
                          packet.begin() + static_cast<long>(offset),
                          packet.begin() + static_cast<long>(offset + length)
                      )
            }
        );

        offset += length;
    }

    return keys;
}

} // namespace aap
