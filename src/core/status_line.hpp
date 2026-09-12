#pragma once

// The one line the window keeps for "how are we doing".
//
// Pure decisions, separated from the tray so they can be exercised without an
// event loop, a radio or a ten minute wait: everything they need arrives as
// arguments, the clock included.

#include <chrono>
#include <optional>
#include <string>

namespace core
{

// A battery reading older than this is what the tray calls quiet, and what
// prompts every subsystem to check itself.
inline constexpr auto kQuietTimeout = std::chrono::minutes(10);

// What the scanner and the key store can say about themselves.
struct RadioHealth
{
    bool hasAdapter = false;
    bool powered = false;
    bool scanning = false;
    bool hasKeys = false;
};

enum class LinkHealth
{
    Closed,
    Open
};

struct StatusInputs
{
    RadioHealth radio;
    LinkHealth link = LinkHealth::Closed;
    // Since the last battery reading of any kind.
    std::chrono::seconds sinceArrival{0};
    // This run's own time, which a reading loaded from the cache has aged by.
    std::chrono::seconds sinceStart{0};
    // How old the cached reading was when it was loaded, if there was one.
    std::optional<std::chrono::seconds> storedAge;
};

inline std::string Age(std::chrono::seconds since);

// Ordered so the most actionable wins: a scanner that is not running hides
// everything underneath it. Derived rather than remembered, so it can neither
// outlive its cause nor miss one appearing between audits.
inline std::string Trouble(const RadioHealth &radio)
{
    if (!radio.hasAdapter)
    {
        return "Error: no Bluetooth adapter";
    }

    if (!radio.powered)
    {
        return "Error: Bluetooth is off";
    }

    if (!radio.scanning)
    {
        return "Error: Bluetooth scanning is not running";
    }

    if (!radio.hasKeys)
    {
        return "Error: no proximity keys, run --keys";
    }

    return {};
}

// A fault wins over the age: how old a reading is only matters while
// everything that could refresh it works.
inline std::string StatusLine(const StatusInputs &state)
{
    std::string fault = Trouble(state.radio);
    if (!fault.empty())
    {
        return fault;
    }

    const bool quiet = state.sinceArrival >= kQuietTimeout;

    if (quiet && state.link == LinkHealth::Open)
    {
        return "Error: connected but silent";
    }

    if (state.storedAge)
    {
        // Plus this run's own time: a stored reading does not get any fresher
        // while the app sits here without a new one.
        return "Last seen " + Age(*state.storedAge + state.sinceStart);
    }

    return quiet ? "No AirPods in range, Bluetooth is on" : std::string{};
}

inline std::string Age(std::chrono::seconds since)
{
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(since).count();

    if (minutes < 1)
    {
        return "just now";
    }

    constexpr long kMinutesPerHour = 60;
    constexpr long kHoursPerDay = 24;

    if (minutes < kMinutesPerHour)
    {
        return std::to_string(minutes) + " min ago";
    }

    const long hours = minutes / kMinutesPerHour;

    if (hours < kHoursPerDay)
    {
        return std::to_string(hours) + (hours == 1 ? " hour ago" : " hours ago");
    }

    const long days = hours / kHoursPerDay;
    return std::to_string(days) + (days == 1 ? " day ago" : " days ago");
}

} // namespace core
