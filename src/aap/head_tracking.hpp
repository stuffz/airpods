#pragma once

#include "aap/packets.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace aap
{

// Fields the AirPods stream once head tracking is started. Orientation is the
// absolute pose; acceleration is what a nod or a shake actually moves.
struct HeadSample
{
    int16_t orientation1 = 0;
    int16_t orientation2 = 0;
    int16_t orientation3 = 0;
    int16_t horizontalAccel = 0;
    int16_t verticalAccel = 0;
};

// Offsets into the 0x17 sensor packet, established by LibrePods:
//
//   0        10       11          43  45  47   49  51  53
//   |header  |kind    |           |o1 |o2 |o3  |   |h  |v
//
// Byte 10 distinguishes a sensor frame (0x44/0x45) from the acknowledgements
// the same opcode carries.
inline std::optional<HeadSample> ParseHeadSample(std::span<const uint8_t> packet)
{
    constexpr size_t kMinSize = 55;
    constexpr size_t kKindOffset = 10;
    constexpr size_t kReservedOffset = 11;
    constexpr size_t kOrientationOffset = 43;
    constexpr size_t kAccelOffset = 51;
    constexpr uint8_t kSensorKindA = 0x44;
    constexpr uint8_t kSensorKindB = 0x45;

    if (!StartsWith(packet, kHeadTracking) || packet.size() < kMinSize)
    {
        return std::nullopt;
    }

    if (packet[kKindOffset] != kSensorKindA && packet[kKindOffset] != kSensorKindB)
    {
        return std::nullopt;
    }

    if (packet[kReservedOffset] != 0x00)
    {
        return std::nullopt;
    }

    const auto read = [packet](size_t offset)
    {
        return static_cast<int16_t>(
            static_cast<uint16_t>(packet[offset]) | (static_cast<uint16_t>(packet[offset + 1]) << 8)
        );
    };

    return HeadSample{
        read(kOrientationOffset), read(kOrientationOffset + 2), read(kOrientationOffset + 4),
        read(kAccelOffset), read(kAccelOffset + 2)
    };
}

} // namespace aap
