#pragma once

// Apple's proximity pairing advertisement: manufacturer id 0x004c, type 0x07.
//
// This is a different transport from the L2CAP control link - it is broadcast,
// so it arrives while the buds are asleep or connected to another host, which
// is what makes lid detection possible at all. Levels are nibble encoded, so
// they land in 10% steps rather than the 1% the control link reports.
//
// The trailing 16 bytes are AES encrypted; nothing here needs them.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace aap
{

enum class LidState : uint8_t
{
    Open = 0x00,
    Closed = 0x01
};

inline constexpr uint16_t kAppleVendorId = 0x004c;
inline constexpr uint8_t kProximityType = 0x07;
inline constexpr uint8_t kPodsAdvertLength = 0x19;
inline constexpr uint8_t kUnknownLevel = 0xff;
inline constexpr uint8_t kModelSuffix = 0x20;

struct ProximityReport
{
    uint16_t model = 0;
    uint8_t leftLevel = kUnknownLevel;
    uint8_t rightLevel = kUnknownLevel;
    uint8_t caseLevel = kUnknownLevel;
    bool leftCharging = false;
    bool rightCharging = false;
    bool caseCharging = false;
    bool leftInEar = false;
    bool rightInEar = false;
    bool bothPodsInCase = false;
    bool onePodInCase = false;
    bool primaryLeft = true;
    uint8_t lidCounter = 0;
    std::optional<LidState> lid;
};

// Names the models LibrePods has identified. A model this does not know is
// still an AirPods model - the id suffix says so - just an unnamed one.
inline std::string_view ModelName(uint16_t model)
{
    switch (model)
    {
    case 0x0220:
        return "AirPods";
    case 0x0f20:
        return "AirPods 2";
    case 0x1320:
        return "AirPods 3";
    case 0x1920:
        return "AirPods 4";
    case 0x1b20:
        return "AirPods 4 ANC";
    case 0x0a20:
        return "AirPods Max";
    case 0x1f20:
        return "AirPods Max USB-C";
    case 0x0e20:
        return "AirPods Pro";
    case 0x1420:
        return "AirPods Pro 2";
    case 0x2420:
        return "AirPods Pro 2 USB-C";
    default:
        return "AirPods";
    }
}

// Returns nothing for anything that is not a paired-mode proximity broadcast:
// a pairing-mode payload lays its fields out differently and must not be read
// with these offsets.
inline std::optional<ProximityReport> ParseProximity(std::span<const uint8_t> data)
{
    constexpr size_t kMinSize = 11;
    constexpr uint8_t kPairingMode = 0x00;
    constexpr uint8_t kMaxLevelNibble = 10; // 0-10 is 0-100%; 15 means absent
    constexpr uint8_t kLevelStep = 10;
    constexpr uint8_t kNibbleShift = 4;
    constexpr uint8_t kNibbleMask = 0x0f;

    // Only the pairing-mode payload is rejected. Byte 2 carries other values in
    // normal operation and none of them change the layout that follows.
    if (data.size() < kMinSize || data[0] != kProximityType || data[2] == kPairingMode)
    {
        return std::nullopt;
    }

    // Every known AirPods model id ends in 0x20 - 0x0e20 Pro, 0x1420 Pro 2,
    // 0x2420 Pro 2 USB-C, 0x2720 Pro 3. Other Apple hardware broadcasts type 07
    // with a payload that rotates every advertisement, and without this those
    // random bytes decode into plausible looking battery levels.
    if (data[4] != kModelSuffix)
    {
        return std::nullopt;
    }

    constexpr uint8_t kPrimaryLeftBit = 0x20;
    constexpr uint8_t kThisPodInCaseBit = 0x40;
    constexpr uint8_t kOnePodInCaseBit = 0x10;
    constexpr uint8_t kBothPodsInCaseBit = 0x04;
    constexpr uint8_t kInEarLowBit = 0x02;
    constexpr uint8_t kInEarHighBit = 0x08;

    const uint8_t status = data[5];
    const bool primaryLeft = (status & kPrimaryLeftBit) != 0;
    const bool thisPodInCase = (status & kThisPodInCaseBit) != 0;

    // Which pod a nibble describes depends on which one is primary, so every
    // paired field is read through that flip.
    const bool flipped = !primaryLeft;

    // Anything above 10 is not a level. 15 is the documented absent marker, but
    // other out-of-range nibbles turn up from Apple devices that are not buds.
    const auto level = [](uint8_t nibble)
    {
        return nibble > kMaxLevelNibble ? kUnknownLevel : static_cast<uint8_t>(nibble * kLevelStep);
    };

    const uint8_t pods = data[6];
    const uint8_t high = (pods >> kNibbleShift) & kNibbleMask;
    const uint8_t low = pods & kNibbleMask;

    const uint8_t caseAndFlags = data[7];
    const uint8_t flags = (caseAndFlags >> kNibbleShift) & kNibbleMask;

    constexpr uint8_t kChargeBitA = 0x01;
    constexpr uint8_t kChargeBitB = 0x02;
    constexpr uint8_t kCaseChargeBit = 0x04;

    ProximityReport report;
    report.model = static_cast<uint16_t>((static_cast<uint16_t>(data[3]) << 8) | data[4]);
    report.leftLevel = level(flipped ? high : low);
    report.rightLevel = level(flipped ? low : high);
    report.caseLevel = level(caseAndFlags & kNibbleMask);
    report.leftCharging = (flags & (flipped ? kChargeBitB : kChargeBitA)) != 0;
    report.rightCharging = (flags & (flipped ? kChargeBitA : kChargeBitB)) != 0;
    report.caseCharging = (flags & kCaseChargeBit) != 0;
    report.primaryLeft = primaryLeft;
    report.onePodInCase = (status & kOnePodInCaseBit) != 0;
    report.bothPodsInCase = (status & kBothPodsInCaseBit) != 0;

    const bool earFlip = flipped != thisPodInCase;
    report.leftInEar = (status & (earFlip ? kInEarHighBit : kInEarLowBit)) != 0;
    report.rightInEar = (status & (earFlip ? kInEarLowBit : kInEarHighBit)) != 0;

    constexpr uint8_t kLidCounterMask = 0x07;
    constexpr uint8_t kLidStateShift = 3;
    constexpr uint8_t kLidStateMask = 0x01;

    const uint8_t lidIndicator = data[8];
    report.lidCounter = lidIndicator & kLidCounterMask;

    // The lid bit only means anything from a pod that is sitting in the case.
    if (thisPodInCase)
    {
        report.lid = static_cast<LidState>((lidIndicator >> kLidStateShift) & kLidStateMask);
    }

    return report;
}

} // namespace aap
