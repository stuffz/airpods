#pragma once

// When to reach for the control link again after a poll.
//
// The two delays are far apart and the reasons for them are not obvious from
// the call site, so the choice is made here, away from the socket and the
// timers that act on it.

#include <chrono>
#include <optional>

namespace core
{

enum class Link
{
    Alive,
    Lost
};

enum class Handshake
{
    Pending,
    Done
};

enum class Retry
{
    None,
    Soon,
    Later
};

// The buds answer the handshake in well under a second when they are listening
// at all, so ten seconds of silence is not slowness.
inline constexpr auto kHandshakeTimeout = std::chrono::seconds(10);

inline constexpr auto kRetrySoon = std::chrono::seconds(5);

// A link that opens but never answers is the buds wedged on their side, which
// no reconnect clears. Retrying that at kRetrySoon would seize and drop the
// single-holder link every five seconds, for nothing.
inline constexpr auto kRetryLater = std::chrono::seconds(60);

// A lost socket is the ordinary case and comes back quickly. A socket that is
// still there but never handshook is the wedge, and waits.
inline constexpr Retry
RetryAfterPoll(Link link, Handshake handshake, std::chrono::milliseconds sinceOpen)
{
    if (link == Link::Lost)
    {
        return Retry::Soon;
    }

    if (handshake == Handshake::Done || sinceOpen < kHandshakeTimeout)
    {
        return Retry::None;
    }

    return Retry::Later;
}

// Nothing for Retry::None, which is not a wait but the absence of one.
inline constexpr std::optional<std::chrono::milliseconds> RetryDelay(Retry when)
{
    switch (when)
    {
    case Retry::Soon:
        return kRetrySoon;
    case Retry::Later:
        return kRetryLater;
    case Retry::None:
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace core
