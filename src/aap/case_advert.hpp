#pragma once

// The case's own advertisement: manufacturer id 0x004c, type 0x07, length 0x11.
//
// Sent about once a second whether the lid is open or shut, from a random
// static address the IRK cannot resolve. After the flag byte there is nothing
// but one AES-128 block under the encryption key the buds hand out, so the
// only way to know the broadcast is ours is to decrypt it and find the fixed
// bytes in place. Measured layout, see docs/ble.md:
//
//   29 20 | lid | case | left | right | 51 0b | 00 | ?? | 00 00 | counter x4
//
// Levels use the same byte encoding as the pods' encrypted tail. The lid byte
// is the pods' cleartext lid indicator: bits 0-2 count opens, bit 3 is shut.
// The last four bytes are a little-endian seconds counter; byte 14 looked
// constant for a day and then carried, which cost a day of "not ours".

#include "aap/encrypted_payload.hpp"
#include "aap/proximity.hpp"
#include "bt/aes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace aap
{

inline constexpr uint8_t kCaseAdvertLength = 0x11;
inline constexpr uint8_t kCaseAdvertFlag = 0x06;
inline constexpr uint16_t kCaseKnownHeader = 0x2920;

struct CaseReport
{
    EncryptedBattery battery;
    LidState lid = LidState::Closed;
    uint8_t lidCounter = 0;
    uint16_t header = 0;
    uint8_t flag = 0;
    uint8_t transient = 0;
};

// Type and length only: this says the payload has the case's shape, not that
// it is ours. The two size bytes in front are the type and the length itself.
inline bool IsCaseAdvert(std::span<const uint8_t> data)
{
    constexpr size_t kPrefixSize = 2;

    return data.size() == kPrefixSize + kCaseAdvertLength && data[0] == kProximityType &&
           data[1] == kCaseAdvertLength;
}

inline bool BlockIsOurs(const bt::AesBlock &block);

// Nothing unless the block decrypts into the fixed bytes: another pair, or a
// wrong key, produces noise and is rejected here.
inline std::optional<CaseReport>
ParseCaseAdvert(std::span<const uint8_t> data, std::span<const uint8_t> key)
{
    constexpr size_t kFlagOffset = 2;
    constexpr size_t kLidOffset = 2;
    constexpr size_t kCaseOffset = 3;
    constexpr size_t kLeftOffset = 4;
    constexpr size_t kRightOffset = 5;
    constexpr size_t kTransientOffset = 9;
    constexpr uint8_t kLidCounterMask = 0x07;
    constexpr uint8_t kLidShutBit = 0x08;
    constexpr int kByteShift = 8;

    if (!IsCaseAdvert(data))
    {
        return std::nullopt;
    }

    const auto block = bt::AesDecryptBlock(key, data.last(bt::kAesBlockSize));
    if (!block || !BlockIsOurs(*block))
    {
        return std::nullopt;
    }

    const auto caseLevel = DecodeLevel((*block)[kCaseOffset]);
    const auto left = DecodeLevel((*block)[kLeftOffset]);
    const auto right = DecodeLevel((*block)[kRightOffset]);

    CaseReport report;
    report.battery = EncryptedBattery{left.level,    right.level,    caseLevel.level,
                                      left.charging, right.charging, caseLevel.charging};
    report.lid = ((*block)[kLidOffset] & kLidShutBit) != 0 ? LidState::Closed : LidState::Open;
    report.lidCounter = (*block)[kLidOffset] & kLidCounterMask;
    report.header = static_cast<uint16_t>(((*block)[0] << kByteShift) | (*block)[1]);
    report.flag = data[kFlagOffset];
    report.transient = (*block)[kTransientOffset];

    return report;
}

// Bytes that were fixed in every capture and are not the header, which may
// well be a model id and so is checked separately by the caller. The counter
// in bytes 12-15 is deliberately left out.
inline bool BlockIsOurs(const bt::AesBlock &block)
{
    constexpr std::array<std::pair<size_t, uint8_t>, 5> kFixed = {{
        {6, 0x51},
        {7, 0x0b},
        {8, 0x00},
        {10, 0x00},
        {11, 0x00},
    }};

    for (const auto &[offset, value] : kFixed)
    {
        if (block[offset] != value)
        {
            return false;
        }
    }

    return true;
}

// Everything in a parsed report that differs from what was measured. Empty
// when the packet looks exactly like the ones this was written against.
inline std::string CaseAdvertAnomalies(const CaseReport &report)
{
    constexpr uint8_t kMaxLevel = 100;

    std::string notes;

    if (report.header != kCaseKnownHeader)
    {
        notes += " header " + std::to_string(report.header);
    }

    if (report.flag != kCaseAdvertFlag)
    {
        notes += " flag " + std::to_string(report.flag);
    }

    const auto tooHigh = [](uint8_t level) { return level != kLevelAbsent && level > kMaxLevel; };

    if (tooHigh(report.battery.leftLevel) || tooHigh(report.battery.rightLevel) ||
        tooHigh(report.battery.caseLevel))
    {
        notes += " level above 100";
    }

    return notes;
}

} // namespace aap
