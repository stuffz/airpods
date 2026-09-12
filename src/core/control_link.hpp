#pragma once

#include "aap/packets.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

#ifdef _WIN32
#include <stdexcept>
#else
#include "aap/session.hpp"
#include "bt/bluez_devices.hpp"
#include "core/link_retry.hpp"
#include "core/logger.hpp"

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

#include <chrono>
#include <memory>
#include <utility>
#endif

namespace core
{

class ControlLink
{
public:
    using PacketHandler = std::function<void(std::span<const uint8_t>)>;

#ifdef _WIN32
    static constexpr bool kSupported = false;

    void Start(const std::string &) { throw std::logic_error("L2CAP is unavailable on Windows"); }

    void Stop() {}

    void Recheck() {}

    bool IsOpen() const { return false; }

    std::string Name() const { return {}; }

    void OnBattery(PacketHandler) {}

    void OnNoiseMode(PacketHandler) {}

    void OnStateChanged(std::function<void()>) {}

    void SetNoiseMode(aap::NoiseMode)
    {
        throw std::logic_error("Noise control is unavailable on Windows");
    }
#else
    static constexpr bool kSupported = true;

    void Start(const std::string &address)
    {
        requested = address;
        QObject::connect(&tick, &QTimer::timeout, [this] { Poll(); });
        tick.start(kPollMs);
        Open();
    }

    void Stop()
    {
        tick.stop();
        retry.stop();
        notifier.reset();
        session.Close();
    }

    // Answers the top-level audit. Buds that BlueZ does not list as connected
    // are away rather than lost, and reaching for them there would only fail
    // slowly and repeatedly; the scheduled retry already covers their return.
    void Recheck()
    {
        if (session.IsOpen() || !Present())
        {
            return;
        }

        LOG_INFO("AirPods are connected but the control link is not; reopening");
        retry.stop();
        Open();
    }

    bool IsOpen() const { return session.IsOpen(); }

    std::string Name() const { return name; }

    void OnBattery(PacketHandler handler) { session.OnBattery(std::move(handler)); }

    void OnNoiseMode(PacketHandler handler) { session.OnNoiseMode(std::move(handler)); }

    void OnStateChanged(std::function<void()> handler) { changed = std::move(handler); }

    void SetNoiseMode(aap::NoiseMode mode) { session.SetNoiseMode(mode); }

private:
    using Clock = std::chrono::steady_clock;

    static constexpr int kPollMs = 500;

    void Open()
    {
        std::string address = requested;
        if (address.empty())
        {
            const auto devices = discovery.List();
            const auto *pods = bt::BluezDevices::PickAirPods(devices);
            if (pods != nullptr)
            {
                address = pods->address;
                name = pods->name;
            }
        }
        if (address.empty() || !session.Connect(address))
        {
            ScheduleRetry(Retry::Soon);
            return;
        }

        openedAt = Clock::now();
        notifier = std::make_unique<QSocketNotifier>(session.Descriptor(), QSocketNotifier::Read);
        QObject::connect(notifier.get(), &QSocketNotifier::activated, [this] { Poll(); });
        changed();
    }

    void Poll()
    {
        if (!session.IsOpen())
        {
            return;
        }

        const Link alive = session.Poll(0) ? Link::Alive : Link::Lost;
        const Handshake handshake = session.IsReady() ? Handshake::Done : Handshake::Pending;
        const auto sinceOpen =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - openedAt);

        const Retry when = RetryAfterPoll(alive, handshake, sinceOpen);
        if (when == Retry::None)
        {
            return;
        }

        if (when == Retry::Later)
        {
            LOG_WARN("No handshake from the AirPods; dropping the link and backing off");
        }

        Drop(when);
    }

    void Drop(Retry when)
    {
        notifier.reset();
        session.Close();
        changed();
        ScheduleRetry(when);
    }

    void ScheduleRetry(Retry when)
    {
        if (!retry.isSingleShot())
        {
            retry.setSingleShot(true);
            QObject::connect(&retry, &QTimer::timeout, [this] { Open(); });
        }
        const auto delay = RetryDelay(when);
        if (!delay)
        {
            return;
        }

        retry.start(*delay);
    }

    bool Present() const
    {
        const auto devices = discovery.List();

        if (requested.empty())
        {
            return bt::BluezDevices::PickAirPods(devices) != nullptr;
        }

        for (const auto &device : devices)
        {
            if (device.address == requested)
            {
                return device.connected;
            }
        }

        return false;
    }

    bt::BluezDevices discovery;
    aap::Session session;
    QTimer tick;
    QTimer retry;
    std::unique_ptr<QSocketNotifier> notifier;
    std::string requested;
    std::string name;
    std::function<void()> changed;
    Clock::time_point openedAt;
#endif
};

} // namespace core
