#pragma once

#include "bt/ble_scanner.hpp"
#include "core/logger.hpp"

#include <QObject>
#include <QTimer>
#ifndef _WIN32
#include <QSocketNotifier>
#include <memory>
#endif

#include <cstdint>
#include <utility>

namespace bt
{

// Keeps the scanner running inside Qt's event loop. Nothing underneath ends
// the watch, because a tray icon that quietly stopped receiving looks exactly
// like one that is up to date.
class AdvertisementWatch
{
public:
    void OnAdvertisement(BleScanner::Handler handler)
    {
        scanner.OnAdvertisement(std::move(handler));
    }

    // Asks the scanner to prove it is still receiving, then pumps the bus.
    // A blocking sd-bus call reads whatever else arrived into the connection's
    // own queue, and the descriptor is left with nothing to wake the notifier
    // with, so those messages would sit there until the next one turned up.
    void Recheck()
    {
        scanner.Recheck();
        Drain();
    }

    bool IsScanning() const { return scanner.IsScanning(); }

    bool HasAdapter() const { return scanner.HasAdapter(); }

    bool IsPowered() const { return scanner.IsPowered(); }

    // Reports no failure: an autostart that beats dbus to the session has to
    // recover, not leave the tray dead until the next login.
    void Start(uint16_t vendor)
    {
        QObject::connect(&recheck, &QTimer::timeout, [this] { Recheck(); });
        retry.setSingleShot(true);
        QObject::connect(&retry, &QTimer::timeout, [this] { Restart(); });
#ifdef _WIN32
        QObject::connect(&poll, &QTimer::timeout, [this] { Drain(); });
#endif

        if (!scanner.Start(vendor))
        {
            retry.start(kRetryMs);
            return;
        }

        Watch();
        Drain();
    }

private:
    // One property read while discovery is healthy, which is the usual case.
    static constexpr int kRecheckMs = 60000;
    static constexpr int kRetryMs = 5000;
#ifdef _WIN32
    static constexpr int kPollMs = 100;
#endif

    void Watch()
    {
#ifdef _WIN32
        poll.start(kPollMs);
#else
        notifier = std::make_unique<QSocketNotifier>(scanner.Descriptor(), QSocketNotifier::Read);
        QObject::connect(notifier.get(), &QSocketNotifier::activated, [this] { Drain(); });
#endif
        recheck.start(kRecheckMs);
    }

    void Drain()
    {
        if (scanner.Process(0))
        {
            return;
        }

        LOG_WARN("Advertisement scanning failed; reconnecting");
        Unwatch();
        retry.start(kRetryMs);
    }

    void Unwatch()
    {
        recheck.stop();
#ifdef _WIN32
        poll.stop();
#else
        notifier.reset();
#endif
    }

    void Restart()
    {
        if (!scanner.Reopen())
        {
            retry.start(kRetryMs);
            return;
        }

        Watch();
        Drain();
    }

    BleScanner scanner;
    QTimer recheck;
    QTimer retry;
#ifdef _WIN32
    QTimer poll;
#else
    std::unique_ptr<QSocketNotifier> notifier;
#endif
};

} // namespace bt
