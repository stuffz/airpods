#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

// Apple Accessory Protocol, as reverse engineered by the LibrePods project.
// See https://github.com/librepods-org/librepods/blob/main/docs/AAP%20Definitions.md
//
// Every packet after the handshake carries the four-byte data header
// 04 00 04 00 followed by a little-endian 16-bit opcode.
namespace aap
{

inline constexpr uint16_t kPsm = 0x1001;

// The AirPods answer nothing at all until this arrives.
inline constexpr std::array<uint8_t, 16> kHandshake = {0x00, 0x00, 0x04, 0x00, 0x01, 0x00,
                                                       0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                       0x00, 0x00, 0x00, 0x00};

// Declares host capabilities. Unlocks the features Apple gates on macOS.
inline constexpr std::array<uint8_t, 14> kSetSpecificFeatures = {0x04, 0x00, 0x04, 0x00, 0x4d,
                                                                 0x00, 0xd7, 0x00, 0x00, 0x00,
                                                                 0x00, 0x00, 0x00, 0x00};

// Subscribes to the asynchronous reports: battery, ear detection, noise control.
inline constexpr std::array<uint8_t, 11> kRequestNotifications = {0x04, 0x00, 0x04, 0x00,
                                                                  0x0f, 0x00, 0xff, 0xff,
                                                                  0xff, 0xff, 0xff};

// Opcode 0x17 with a protobuf-ish body whose encoding is not understood, so
// the bytes are replayed verbatim. Two pairs are in circulation and which one
// starts the stream depends on the firmware; LibrePods defaults to Alternate.
inline constexpr std::array<uint8_t, 27> kStartHeadTrackingAlt = {
    0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00, 0x10, 0x00, 0x0f, 0x00, 0x08, 0x73,
    0x42, 0x0b, 0x08, 0x10, 0x10, 0x02, 0x1a, 0x05, 0x01, 0x40, 0x9c, 0x00, 0x00
};

inline constexpr std::array<uint8_t, 27> kStopHeadTrackingAlt = {
    0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00, 0x10, 0x00, 0x0f, 0x00, 0x08, 0x75,
    0x42, 0x0b, 0x08, 0x10, 0x10, 0x02, 0x1a, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00
};

// The pair captured from macOS.
inline constexpr std::array<uint8_t, 28> kStartHeadTrackingMacos = {
    0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x08, 0xa1,
    0x02, 0x42, 0x0b, 0x08, 0x0e, 0x10, 0x02, 0x1a, 0x05, 0x01, 0x40, 0x9c, 0x00, 0x00
};

inline constexpr std::array<uint8_t, 29> kStopHeadTrackingMacos = {
    0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00, 0x10, 0x00, 0x11, 0x00, 0x08, 0x7e, 0x10,
    0x02, 0x42, 0x0b, 0x08, 0x4e, 0x10, 0x02, 0x1a, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00
};

// Opcode 0x17 carries its payload length at bytes 10-11, right after the
// ten-byte kHeadTracking prefix. A declared length that overruns the payload
// wedges the buds until they are reset in the case, so every command is
// checked at compile time rather than trusted from transcription.
constexpr bool PayloadLengthMatches(std::span<const uint8_t> packet)
{
    constexpr size_t kLengthOffset = 10;
    constexpr size_t kPayloadOffset = 12;
    constexpr size_t kHighByteShift = 8;

    if (packet.size() < kPayloadOffset)
    {
        return false;
    }

    const size_t declared = static_cast<size_t>(packet[kLengthOffset]) |
                            (static_cast<size_t>(packet[kLengthOffset + 1]) << kHighByteShift);

    return declared == packet.size() - kPayloadOffset;
}

static_assert(PayloadLengthMatches(kStartHeadTrackingAlt), "alt start length");
static_assert(PayloadLengthMatches(kStopHeadTrackingAlt), "alt stop length");
static_assert(PayloadLengthMatches(kStartHeadTrackingMacos), "macos start length");
static_assert(PayloadLengthMatches(kStopHeadTrackingMacos), "macos stop length");

// Noise control. Opcode 09 is shared by several settings, so the sub-command
// byte is part of the prefix - matching on 09 alone would catch conversational
// awareness (28) and the adaptive level (2e) as well.
inline constexpr std::array<uint8_t, 7> kNoiseControl = {0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0d};

enum class NoiseMode : uint8_t
{
    Off = 0x01,
    NoiseCancellation = 0x02,
    Transparency = 0x03,
    Adaptive = 0x04
};

// The buds report the current mode in the same shape they accept it in.
inline constexpr std::array<uint8_t, 11> NoiseControlPacket(NoiseMode mode)
{
    return {0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0d, static_cast<uint8_t>(mode), 0x00, 0x00, 0x00};
}

enum class HeadTrackingVariant
{
    Alternate,
    Macos
};

inline std::span<const uint8_t> StartHeadTrackingPacket(HeadTrackingVariant variant)
{
    return variant == HeadTrackingVariant::Macos ? std::span<const uint8_t>(kStartHeadTrackingMacos)
                                                 : std::span<const uint8_t>(kStartHeadTrackingAlt);
}

inline std::span<const uint8_t> StopHeadTrackingPacket(HeadTrackingVariant variant)
{
    return variant == HeadTrackingVariant::Macos ? std::span<const uint8_t>(kStopHeadTrackingMacos)
                                                 : std::span<const uint8_t>(kStopHeadTrackingAlt);
}

// Asks the buds for the keys that belong to their proximity advertisements:
// an IRK, which resolves the rotating private address to prove a broadcast is
// from these buds, and an encryption key for the payload's encrypted tail.
inline constexpr std::array<uint8_t, 8> kRequestProximityKeys = {0x04, 0x00, 0x04, 0x00,
                                                                 0x30, 0x00, 0x05, 0x00};

inline constexpr std::array<uint8_t, 6> kProximityKeysReply = {0x04, 0x00, 0x04, 0x00, 0x31, 0x00};

// Some firmware answers the handshake and the feature declaration on these
// opcodes instead of the data header, so both are matched as prefixes.
inline constexpr std::array<uint8_t, 4> kHandshakeAck = {0x01, 0x00, 0x04, 0x00};
inline constexpr std::array<uint8_t, 6> kFeaturesAck = {0x04, 0x00, 0x04, 0x00, 0x2b, 0x00};
inline constexpr std::array<uint8_t, 6> kBatteryReport = {0x04, 0x00, 0x04, 0x00, 0x04, 0x00};
inline constexpr std::array<uint8_t, 6> kEarDetection = {0x04, 0x00, 0x04, 0x00, 0x06, 0x00};
inline constexpr std::array<uint8_t, 10> kHeadTracking = {0x04, 0x00, 0x04, 0x00, 0x17,
                                                          0x00, 0x00, 0x00, 0x10, 0x00};

inline bool StartsWith(std::span<const uint8_t> packet, std::span<const uint8_t> prefix);

inline std::optional<NoiseMode> ParseNoiseMode(std::span<const uint8_t> packet)
{
    constexpr size_t kModeOffset = 7;

    if (!StartsWith(packet, kNoiseControl) || packet.size() <= kModeOffset)
    {
        return std::nullopt;
    }

    switch (packet[kModeOffset])
    {
    case static_cast<uint8_t>(NoiseMode::Off):
        return NoiseMode::Off;
    case static_cast<uint8_t>(NoiseMode::NoiseCancellation):
        return NoiseMode::NoiseCancellation;
    case static_cast<uint8_t>(NoiseMode::Transparency):
        return NoiseMode::Transparency;
    case static_cast<uint8_t>(NoiseMode::Adaptive):
        return NoiseMode::Adaptive;
    default:
        return std::nullopt;
    }
}

inline std::string_view NoiseModeName(NoiseMode mode)
{
    switch (mode)
    {
    case NoiseMode::Off:
        return "Off";
    case NoiseMode::NoiseCancellation:
        return "Noise Cancellation";
    case NoiseMode::Transparency:
        return "Transparency";
    case NoiseMode::Adaptive:
        return "Adaptive";
    }

    return "Unknown";
}

inline bool StartsWith(std::span<const uint8_t> packet, std::span<const uint8_t> prefix)
{
    return packet.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), packet.begin());
}

} // namespace aap
