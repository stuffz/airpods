#pragma once

#include "aap/encrypted_payload.hpp"
#include "aap/packets.hpp"
#include "aap/proximity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace aap
{

enum class Component : uint8_t
{
    Headset = 0x01, // AirPods Max reports one component, not two buds
    Right = 0x02,
    Left = 0x04,
    Case = 0x08
};

enum class ChargeStatus : uint8_t
{
    Unknown = 0x00,
    Charging = 0x01,
    Discharging = 0x02,
    Disconnected = 0x04
};

// Where a reading came from. The control link is exact and live, so its
// values outrank a broadcast for as long as it keeps reporting them.
enum class Source : uint8_t
{
    None,
    Stored,
    Air,
    Link
};

enum class LinkState : uint8_t
{
    Down,
    Up
};

struct ComponentBattery
{
    uint8_t level = 0;
    ChargeStatus status = ChargeStatus::Disconnected;
    bool known = false;
    Source source = Source::None;
};

// 04 00 04 00 04 00 [count] then [type 01 level status 01] per component.
// A component reported as Disconnected keeps its last known level: the buds
// send that while one is in the case and overwriting would blank the display.
class Battery
{
public:
    bool Parse(std::span<const uint8_t> packet)
    {
        if (!StartsWith(packet, kBatteryReport))
        {
            return false;
        }

        constexpr size_t kHeaderSize = 7; // header plus the count byte
        constexpr size_t kEntrySize = 5;
        constexpr size_t kMaxEntries = 3;
        constexpr uint8_t kEntryMarker = 0x01;

        const uint8_t count = packet[kHeaderSize - 1];
        if (count > kMaxEntries || packet.size() != kHeaderSize + (kEntrySize * count))
        {
            return false;
        }

        for (uint8_t i = 0; i < count; ++i)
        {
            const std::span<const uint8_t> entry =
                packet.subspan(kHeaderSize + (kEntrySize * i), kEntrySize);

            if (entry[1] != kEntryMarker || entry[4] != kEntryMarker)
            {
                return false;
            }

            ComponentBattery *slot = Slot(static_cast<Component>(entry[0]));
            if (slot == nullptr)
            {
                continue;
            }

            // Disconnected keeps the level on display but the link no longer
            // vouches for it, so a broadcast may take the slot over.
            const auto status = static_cast<ChargeStatus>(entry[3]);
            if (status == ChargeStatus::Disconnected)
            {
                if (slot->source == Source::Link)
                {
                    slot->source = Source::Stored;
                }
                continue;
            }

            *slot = {entry[2], status, true, Source::Link};
        }

        return true;
    }

    // Fills the same slots from a BLE advertisement. Levels arrive in 10% steps
    // there, so this is only worth using when the control link is down.
    void ApplyProximity(const ProximityReport &report)
    {
        Assign(Component::Left, report.leftLevel, report.leftCharging);
        Assign(Component::Right, report.rightLevel, report.rightCharging);
        Assign(Component::Case, report.caseLevel, report.caseCharging);
    }

    // Exact levels from the advertisement's encrypted block. Preferred over
    // ApplyProximity wherever the key is available: same transport, but a byte
    // per component instead of a nibble.
    void ApplyEncrypted(const EncryptedBattery &report)
    {
        Assign(Component::Left, Exact(report.leftLevel), report.leftCharging);
        Assign(Component::Right, Exact(report.rightLevel), report.rightCharging);
        Assign(Component::Case, Exact(report.caseLevel), report.caseCharging);
    }

    // Folds a broadcast reading into this one. While the link is up, a
    // component the link is reporting keeps the link's value; anything the
    // link has stopped reporting, or everything once it is down, takes the
    // broadcast.
    void Merge(const Battery &air, LinkState link)
    {
        for (size_t i = 0; i < states.size(); ++i)
        {
            const ComponentBattery &incoming = air.states[i];
            if (!incoming.known)
            {
                continue;
            }

            ComponentBattery &slot = states[i];
            if (link == LinkState::Up && slot.source == Source::Link)
            {
                continue;
            }

            slot = incoming;
        }
    }

    // Puts a remembered reading back. Separate from the parsing paths so it is
    // obvious at the call site that this is not fresh data.
    void Restore(Component component, uint8_t level, ChargeStatus status)
    {
        ComponentBattery *slot = Slot(component);
        if (slot == nullptr)
        {
            return;
        }

        slot->level = level;
        slot->status = status;
        slot->known = true;
        slot->source = Source::Stored;
    }

    const ComponentBattery &Get(Component component) const
    {
        const size_t index = IndexOf(component);
        return index < states.size() ? states[index] : unknown;
    }

    bool HasReading() const
    {
        for (const auto &state : states)
        {
            if (state.known)
            {
                return true;
            }
        }

        return false;
    }

    static uint8_t Exact(uint8_t level) { return level == kLevelAbsent ? kUnknownLevel : level; }

    void Assign(Component component, uint8_t level, bool charging)
    {
        if (level == kUnknownLevel)
        {
            return;
        }

        ComponentBattery *slot = Slot(component);
        if (slot == nullptr)
        {
            return;
        }

        slot->level = level;
        slot->status = charging ? ChargeStatus::Charging : ChargeStatus::Discharging;
        slot->known = true;
        slot->source = Source::Air;
    }

    static std::string_view Name(Component component)
    {
        switch (component)
        {
        case Component::Headset:
            return "Headset";
        case Component::Right:
            return "Right";
        case Component::Left:
            return "Left";
        case Component::Case:
            return "Case";
        }

        return "Unknown";
    }

    static std::string_view SourceName(Source source)
    {
        switch (source)
        {
        case Source::Link:
            return "link";
        case Source::Air:
            return "air";
        case Source::Stored:
            return "stored";
        case Source::None:
            return "none";
        }

        return "none";
    }

    static std::string_view StatusName(ChargeStatus status)
    {
        switch (status)
        {
        case ChargeStatus::Charging:
            return "Charging";
        case ChargeStatus::Discharging:
            return "Discharging";
        case ChargeStatus::Disconnected:
            return "Disconnected";
        case ChargeStatus::Unknown:
            return "Unknown";
        }

        return "Unknown";
    }

private:
    static constexpr std::array<Component, 4> kComponents = {
        Component::Headset, Component::Right, Component::Left, Component::Case
    };

    static size_t IndexOf(Component component)
    {
        for (size_t i = 0; i < kComponents.size(); ++i)
        {
            if (kComponents[i] == component)
            {
                return i;
            }
        }

        return kComponents.size();
    }

    ComponentBattery *Slot(Component component)
    {
        const size_t index = IndexOf(component);
        return index < states.size() ? &states[index] : nullptr;
    }

    std::array<ComponentBattery, kComponents.size()> states{};
    ComponentBattery unknown{};
};

} // namespace aap
