#pragma once

// Runs the tray application: holds the AAP session open inside Qt's event loop
// and pushes every battery report at the tray icon.

#include "aap/battery.hpp"
#include "aap/case_advert.hpp"
#include "aap/encrypted_payload.hpp"
#include "aap/packets.hpp"
#include "aap/proximity.hpp"
#include "bt/advertisement_watch.hpp"
#include "bt/rpa.hpp"
#include "core/battery_store.hpp"
#include "core/control_link.hpp"
#include "core/key_store.hpp"
#include "core/logger.hpp"
#include "core/options.hpp"
#include "core/stop_flag.hpp"
#include "ui/main_window.hpp"
#include "ui/tray_icon.hpp"

#include <QApplication>
#include <QObject>
#include <QTimer>

#include <chrono>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace core
{

class TrayApp
{
public:
    explicit TrayApp(const Options &opts) : options(opts) {}

    int Run()
    {
        if (!ui::TrayIcon::Available())
        {
            LOG_ERROR("This session has no system tray");
            return kFailure;
        }

        tray.Show();
        tray.OnQuit([] { QApplication::quit(); });
        staleness = store.Load(battery);

        Refresh(false);
        tray.OnActivate([this] { window.Toggle(); });
        tray.OnShowDetails([this] { window.Toggle(); });
        tray.OnNoiseMode([this](aap::NoiseMode mode) { link.SetNoiseMode(mode); });
        window.OnModePicked([this](aap::NoiseMode mode) { link.SetNoiseMode(mode); });
        tray.Update(battery, false);
        link.OnNoiseMode(
            [this](std::span<const uint8_t> packet)
            {
                const auto mode = aap::ParseNoiseMode(packet);
                if (mode)
                {
                    tray.ShowNoiseMode(*mode);
                    window.ShowMode(*mode);
                }
            }
        );
        link.OnBattery(
            [this](std::span<const uint8_t> packet)
            {
                battery.Parse(packet);
                staleness.reset();
                Refresh(true);
                store.Save(battery);
            }
        );
        link.OnStateChanged([this] { Refresh(link.IsOpen()); });

        QObject::connect(&tick, &QTimer::timeout, [this] { OnTick(); });
        tick.start(kTickMs);

        if (!StartScanning())
        {
            return kFailure;
        }
        if (ControlLink::kSupported)
        {
            link.Start(options.address);
        }

        const int result = QApplication::exec();
        link.Stop();
        return result;
    }

private:
    static constexpr int kFailure = 1;
    static constexpr int kTickMs = 500;

    bool StartScanning()
    {
        if (!keys.Load(options.keyFile))
        {
            LOG_WARN("No proximity keys; use --key-file with keys fetched on Linux");
            if (!ControlLink::kSupported)
            {
                return false;
            }
        }
        scanner.OnAdvertisement([this](const std::string &address, std::span<const uint8_t> data)
                                { OnAdvertisement(address, data); });
        scanner.OnError([] { QApplication::exit(kFailure); });
        return scanner.Start(aap::kAppleVendorId);
    }

    // Two broadcasts carry battery: the pods' 25-byte proximity message and the
    // case's 17-byte one. They are told apart by length and each has its own
    // identity check, the IRK for the pods and the decrypt for the case.
    void OnAdvertisement(const std::string &address, std::span<const uint8_t> data)
    {
        if (data.empty() || data[0] != aap::kProximityType)
        {
            return;
        }

        LOG_DEBUG("advert " + address + " " + ToHex(data));

        if (aap::IsCaseAdvert(data))
        {
            OnCaseAdvert(address, data);
            return;
        }

        constexpr size_t kPodsAdvertSize = 2 + aap::kPodsAdvertLength;

        if (data.size() != kPodsAdvertSize)
        {
            LOG_WARN("type 07 advert of unexpected length from " + address + ": " + ToHex(data));
            return;
        }

        OnPodsAdvert(address, data);
    }

    void OnCaseAdvert(const std::string &address, std::span<const uint8_t> data)
    {
        if (!keys.HasEncryption())
        {
            LOG_DEBUG("case advert from " + address + " but no encryption key; run --keys");
            return;
        }

        const auto report = aap::ParseCaseAdvert(data, keys.Encryption());
        if (!report)
        {
            LOG_DEBUG("case advert from " + address + " does not decrypt as ours");
            return;
        }

        const std::string anomalies = aap::CaseAdvertAnomalies(*report);
        if (!anomalies.empty())
        {
            LOG_WARN("case advert off the measured layout:" + anomalies + "  " + ToHex(data));
        }

        if (report->transient != 0)
        {
            LOG_DEBUG("case advert transient byte " + std::to_string(report->transient));
        }

        aap::Battery fromAir;
        fromAir.ApplyEncrypted(report->battery);

        const bool opened = report->lid == aap::LidState::Open;
        LOG_DEBUG(
            std::string("case advert: lid ") + (opened ? "open" : "shut") + " count " +
            std::to_string(report->lidCounter) + "  " + Describe(fromAir)
        );

        Absorb(fromAir);
        OnLid(opened, "case");
    }

    void OnPodsAdvert(const std::string &address, std::span<const uint8_t> data)
    {
        const auto report = aap::ParseProximity(data);
        if (!report)
        {
            LOG_WARN("pods advert did not parse from " + address + ": " + ToHex(data));
            return;
        }

        // Without the key any AirPods in range would open the panel; there was
        // a second pair broadcasting during testing.
        if (keys.HasIrk() && !bt::AddressBelongsTo(address, keys.Irk()))
        {
            LOG_DEBUG("pods advert from another pair " + address);
            return;
        }

        aap::Battery fromAir;
        fromAir.ApplyProximity(*report);

        if (keys.HasEncryption())
        {
            const auto exact =
                aap::ParseEncryptedBattery(data, keys.Encryption(), report->primaryLeft);

            if (exact)
            {
                fromAir.ApplyEncrypted(*exact);
            }
            else
            {
                LOG_WARN("pods advert tail did not decrypt from " + address);
            }
        }

        LOG_DEBUG(
            "pods advert: " + Describe(fromAir) +
            (report->lid ? (*report->lid == aap::LidState::Open ? "  lid open" : "  lid shut")
                         : "  no lid")
        );

        Absorb(fromAir);

        // The lid bit only means something from a pod sitting in the case.
        if (report->lid)
        {
            OnLid(*report->lid == aap::LidState::Open, "pods");
        }
    }

    // Every broadcast reading lands here. The link keeps the components it is
    // reporting; the broadcast fills in the rest, and everything once the link
    // is down.
    void Absorb(const aap::Battery &fromAir)
    {
        const bool connected = link.IsOpen();

        battery.Merge(fromAir, connected ? aap::LinkState::Up : aap::LinkState::Down);
        LOG_DEBUG(
            std::string("battery after merge, link ") + (connected ? "up" : "down") + ": " +
            Describe(battery)
        );

        staleness.reset();
        Refresh(connected);
        store.Save(battery);
    }

    // The private address rotates on every lid open, so the state that decides
    // whether this is a transition is kept here, not per address.
    void OnLid(bool opened, const char *source)
    {
        const bool changed = !lidOpen.has_value() || *lidOpen != opened;
        lidOpen = opened;

        if (!changed)
        {
            return;
        }

        LOG_INFO(std::string("lid ") + (opened ? "opened" : "shut") + ", reported by " + source);
    }

    static std::string Describe(const aap::Battery &battery)
    {
        std::string text;

        for (const aap::Component component :
             {aap::Component::Left, aap::Component::Right, aap::Component::Case})
        {
            const auto &state = battery.Get(component);
            text += std::string(aap::Battery::Name(component)) + " ";

            if (!state.known)
            {
                text += "--  ";
                continue;
            }

            text += std::to_string(static_cast<int>(state.level)) + "%" +
                    (state.status == aap::ChargeStatus::Charging ? "+" : "") + " (" +
                    std::string(aap::Battery::SourceName(state.source)) + ")  ";
        }

        return text;
    }

    static std::string ToHex(std::span<const uint8_t> data)
    {
        constexpr char kDigits[] = "0123456789abcdef";

        std::string hex;
        hex.reserve(data.size() * 2);

        for (const uint8_t byte : data)
        {
            hex.push_back(kDigits[byte >> 4]);
            hex.push_back(kDigits[byte & 0x0f]);
        }

        return hex;
    }

    void OnTick()
    {
        if (stopRequested != 0)
        {
            QApplication::quit();
        }
    }

    void Refresh(bool connected)
    {
        tray.Update(battery, connected);
        window.Update(battery, Heading());
        window.ShowLink(connected ? aap::LinkState::Up : aap::LinkState::Down);
        window.ShowAge(staleness ? "Last seen " + Age(*staleness) : std::string{});
    }

    static std::string Age(std::chrono::seconds since)
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

    std::string Heading() const
    {
        const std::string name = link.Name();
        return name.empty() ? "AirPods" : name;
    }

    const Options &options;
    ControlLink link;
    aap::Battery battery;
    ui::TrayIcon tray;
    ui::MainWindow window;
    QTimer tick;
    bt::AdvertisementWatch scanner;
    std::optional<bool> lidOpen;
    KeyStore keys;
    BatteryStore store;
    std::optional<std::chrono::seconds> staleness;
};

} // namespace core
