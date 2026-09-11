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

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

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

    bool IsOpen() const { return session.IsOpen(); }

    std::string Name() const { return name; }

    void OnBattery(PacketHandler handler) { session.OnBattery(std::move(handler)); }

    void OnNoiseMode(PacketHandler handler) { session.OnNoiseMode(std::move(handler)); }

    void OnStateChanged(std::function<void()> handler) { changed = std::move(handler); }

    void SetNoiseMode(aap::NoiseMode mode) { session.SetNoiseMode(mode); }

private:
    static constexpr int kPollMs = 500;
    static constexpr int kRetryMs = 5000;

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
            ScheduleRetry();
            return;
        }

        notifier = std::make_unique<QSocketNotifier>(session.Descriptor(), QSocketNotifier::Read);
        QObject::connect(notifier.get(), &QSocketNotifier::activated, [this] { Poll(); });
        changed();
    }

    void Poll()
    {
        if (!session.IsOpen() || session.Poll(0))
        {
            return;
        }
        notifier.reset();
        session.Close();
        changed();
        ScheduleRetry();
    }

    void ScheduleRetry()
    {
        if (!retry.isSingleShot())
        {
            retry.setSingleShot(true);
            QObject::connect(&retry, &QTimer::timeout, [this] { Open(); });
        }
        retry.start(kRetryMs);
    }

    bt::BluezDevices discovery;
    aap::Session session;
    QTimer tick;
    QTimer retry;
    std::unique_ptr<QSocketNotifier> notifier;
    std::string requested;
    std::string name;
    std::function<void()> changed;
#endif
};

} // namespace core
