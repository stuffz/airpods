#pragma once

#include "bt/ble_scanner.hpp"

#include <QObject>
#include <QTimer>
#ifndef _WIN32
#include <QSocketNotifier>
#include <memory>
#endif

#include <cstdint>
#include <functional>
#include <utility>

namespace bt
{

class AdvertisementWatch
{
public:
    void OnAdvertisement(BleScanner::Handler handler)
    {
        scanner.OnAdvertisement(std::move(handler));
    }

    void OnError(std::function<void()> handler) { failed = std::move(handler); }

    bool Start(uint16_t vendor)
    {
        if (!scanner.Start(vendor))
        {
            return false;
        }
#ifdef _WIN32
        QObject::connect(&timer, &QTimer::timeout, [this] { Drain(); });
        timer.start(kPollMs);
#else
        notifier = std::make_unique<QSocketNotifier>(scanner.Descriptor(), QSocketNotifier::Read);
        QObject::connect(notifier.get(), &QSocketNotifier::activated, [this] { Drain(); });
#endif
        return true;
    }

private:
    void Drain()
    {
        if (scanner.Process(0))
        {
            return;
        }
#ifdef _WIN32
        timer.stop();
#else
        notifier->setEnabled(false);
#endif
        scanner.Stop();
        failed();
    }

    BleScanner scanner;
    std::function<void()> failed;
#ifdef _WIN32
    static constexpr int kPollMs = 100;
    QTimer timer;
#else
    std::unique_ptr<QSocketNotifier> notifier;
#endif
};

} // namespace bt
