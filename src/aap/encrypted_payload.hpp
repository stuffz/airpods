#pragma once

// The encrypted tail of a proximity advertisement.
//
// The cleartext carries battery in nibbles, so 10% steps. This block carries a
// byte per component - charging in the top bit, level in the low seven - which
// is where the exact figures a phone shows come from. It needs the encryption
// key the buds hand out over the control link.

#include "bt/aes.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace aap
{

inline constexpr uint8_t kLevelAbsent = 0x7f;
inline constexpr uint8_t kByteAbsent = 0xff;

struct DecodedLevel
{
    bool charging = false;
    uint8_t level = kLevelAbsent;
};

inline DecodedLevel DecodeLevel(uint8_t value);

struct EncryptedBattery
{
    uint8_t leftLevel = kLevelAbsent;
    uint8_t rightLevel = kLevelAbsent;
    uint8_t caseLevel = kLevelAbsent;
    bool leftCharging = false;
    bool rightCharging = false;
    bool caseCharging = false;
};

// The advertisement's last 16 bytes are the block; anything shorter has none.
inline std::optional<EncryptedBattery> ParseEncryptedBattery(
    std::span<const uint8_t> advertisement, std::span<const uint8_t> key, bool leftIsPrimary
)
{
    if (advertisement.size() < bt::kAesBlockSize)
    {
        return std::nullopt;
    }

    const auto block = bt::AesDecryptBlock(key, advertisement.last(bt::kAesBlockSize));
    if (!block)
    {
        return std::nullopt;
    }

    constexpr size_t kPrimaryOffset = 1;
    constexpr size_t kSecondaryOffset = 2;
    constexpr size_t kCaseOffset = 3;

    const auto left = DecodeLevel((*block)[leftIsPrimary ? kPrimaryOffset : kSecondaryOffset]);
    const auto right = DecodeLevel((*block)[leftIsPrimary ? kSecondaryOffset : kPrimaryOffset]);
    const auto caseLevel = DecodeLevel((*block)[kCaseOffset]);

    return EncryptedBattery{left.level,    right.level,    caseLevel.level,
                            left.charging, right.charging, caseLevel.charging};
}

// A component that is not there reads 0xff, all bits set, which read naively
// is "absent and charging". The whole byte is checked before the bit is.
inline DecodedLevel DecodeLevel(uint8_t value)
{
    constexpr uint8_t kChargingBit = 0x80;
    constexpr uint8_t kLevelMask = 0x7f;

    if (value == kByteAbsent)
    {
        return {false, kLevelAbsent};
    }

    return {(value & kChargingBit) != 0, static_cast<uint8_t>(value & kLevelMask)};
}

} // namespace aap
