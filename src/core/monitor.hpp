#pragma once

#include "core/local_time.hpp"

#include "aap/battery.hpp"
#include "aap/gestures.hpp"
#include "aap/head_tracking.hpp"
#include "aap/session.hpp"
#include "bt/bluez_devices.hpp"
#include "core/logger.hpp"
#include "core/options.hpp"
#include "core/stop_flag.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace core
{

// Owns the run: find the AirPods, hold the control link open, and print what
// arrives on it. Battery reports are pushed by the AirPods rather than polled,
// so the interval governs printing, not fetching.
class Monitor
{
public:
    explicit Monitor(const Options &opts) : options(opts) {}

    int Run()
    {
        if (options.list)
        {
            return ListDevices();
        }

        const std::string address = ResolveAddress();
        if (address.empty())
        {
            return kFailure;
        }

        LOG_INFO("Connecting to " + address);
        if (!session.Connect(address))
        {
            return kFailure;
        }

        InstallHandlers();

        const int status = Loop();

        if (options.gestures)
        {
            session.StopHeadTracking(options.headTracking);
        }

        session.Close();
        return status;
    }

private:
    using Clock = std::chrono::steady_clock;

    static constexpr int kSuccess = 0;
    static constexpr int kFailure = 1;
    static constexpr int kMinPollMs = 1;

    int ListDevices() const
    {
        const auto devices = discovery.List();

        if (devices.empty())
        {
            std::cerr << "No Bluetooth devices known to BlueZ\n";
            return kFailure;
        }

        for (const auto &device : devices)
        {
            std::cout << device.address << "  "
                      << (device.connected ? "connected   " : "offline     ") << device.name
                      << "\n";
        }

        return kSuccess;
    }

    std::string ResolveAddress() const
    {
        if (!options.address.empty())
        {
            return options.address;
        }

        const auto devices = discovery.List();
        const auto *airpods = bt::BluezDevices::PickAirPods(devices);

        if (airpods == nullptr)
        {
            std::cerr << "No connected AirPods found. Connect them, or pass --address.\n";
            return {};
        }

        LOG_INFO("Found " + airpods->name + " at " + airpods->address);
        return airpods->address;
    }

    void InstallHandlers()
    {
        session.OnBattery([this](std::span<const uint8_t> packet) { battery.Parse(packet); });

        session.OnReady(
            [this]
            {
                if (!options.gestures)
                {
                    return;
                }

                LOG_INFO("Starting head tracking");
                session.StartHeadTracking(options.headTracking);
            }
        );

        if (!options.gestures)
        {
            return;
        }

        session.OnHeadTracking(
            [this](std::span<const uint8_t> packet)
            {
                const auto sample = aap::ParseHeadSample(packet);
                if (!sample)
                {
                    return;
                }

                const auto gesture = detector.Feed(*sample);
                if (gesture)
                {
                    std::cout << Timestamp() << "  gesture " << aap::GestureName(*gesture) << "\n"
                              << std::flush;
                }
            }
        );
    }

    int Loop()
    {
        auto nextPrint = Clock::now();

        while (stopRequested == 0)
        {
            if (!session.Poll(PollTimeout(nextPrint)))
            {
                return kFailure;
            }

            if (Clock::now() < nextPrint)
            {
                continue;
            }

            nextPrint += std::chrono::milliseconds(options.intervalMs);

            if (!battery.HasReading())
            {
                continue;
            }

            PrintBattery();

            if (options.once)
            {
                return kSuccess;
            }
        }

        return kSuccess;
    }

    int PollTimeout(Clock::time_point nextPrint) const
    {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(nextPrint - Clock::now());

        return std::clamp(static_cast<int>(remaining.count()), kMinPollMs, options.intervalMs);
    }

    void PrintBattery() const
    {
        constexpr aap::Component kOrder[] = {
            aap::Component::Headset, aap::Component::Left, aap::Component::Right,
            aap::Component::Case
        };

        std::cout << Timestamp();

        for (const aap::Component component : kOrder)
        {
            const auto &state = battery.Get(component);
            if (!state.known)
            {
                continue;
            }

            std::cout << "  " << aap::Battery::Name(component) << " " << std::setw(3)
                      << static_cast<int>(state.level) << "% "
                      << aap::Battery::StatusName(state.status);
        }

        std::cout << "\n" << std::flush;
    }

    static std::string Timestamp()
    {
        const auto now = std::chrono::system_clock::now();
        const auto timeT = std::chrono::system_clock::to_time_t(now);

        const std::tm tm = core::LocalTime(timeT);

        std::array<char, 16> text{};
        if (std::strftime(text.data(), text.size(), "%H:%M:%S", &tm) == 0)
        {
            return {};
        }

        return text.data();
    }

    const Options &options;
    bt::BluezDevices discovery;
    aap::Session session;
    aap::Battery battery;
    aap::GestureDetector detector;
};

} // namespace core
