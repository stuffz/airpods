#pragma once

#include "core/logger.hpp"

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace bt
{

namespace Advertisement = winrt::Windows::Devices::Bluetooth::Advertisement;

class BleScanner
{
public:
    using Handler = std::function<void(const std::string &, std::span<const uint8_t>)>;

    BleScanner() = default;

    ~BleScanner() { Stop(); }

    BleScanner(const BleScanner &) = delete;
    BleScanner &operator=(const BleScanner &) = delete;

    void OnAdvertisement(Handler callback) { handler = std::move(callback); }

    bool Start(uint16_t vendor)
    {
        if (watcher)
        {
            throw std::logic_error("BLE scanner is already started");
        }

        // Set before anything can fail: Reopen starts again from it, and a
        // failed start would otherwise leave it asking for vendor zero.
        wanted = vendor;

        try
        {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            apartment = true;
            state = std::make_shared<State>();
            watcher = Watcher{};
            watcher.ScanningMode(Advertisement::BluetoothLEScanningMode::Passive);
            received = watcher.Received(
                winrt::auto_revoke,
                [queue = state, vendor](
                    const Watcher &,
                    const Advertisement::BluetoothLEAdvertisementReceivedEventArgs &args
                )
                {
                    try
                    {
                        for (const auto &data : args.Advertisement().ManufacturerData())
                        {
                            if (data.CompanyId() != vendor)
                            {
                                continue;
                            }

                            const auto buffer = data.Data();
                            Packet packet{
                                Address(args.BluetoothAddress()),
                                std::vector<uint8_t>(buffer.Length())
                            };
                            winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer)
                                .ReadBytes(packet.data);
                            std::lock_guard lock(queue->mutex);
                            if (queue->packets.size() >= kMaxQueued)
                            {
                                queue->error = "BLE advertisement queue overflow";
                            }
                            else
                            {
                                queue->packets.push_back(std::move(packet));
                            }
                            queue->ready.notify_one();
                        }
                    }
                    catch (const winrt::hresult_error &error)
                    {
                        std::lock_guard lock(queue->mutex);
                        queue->error = winrt::to_string(error.message());
                        queue->ready.notify_one();
                    }
                }
            );
            stopped = watcher.Stopped(
                winrt::auto_revoke,
                [queue = state](
                    const Watcher &,
                    const Advertisement::BluetoothLEAdvertisementWatcherStoppedEventArgs &args
                )
                {
                    std::lock_guard lock(queue->mutex);
                    queue->error = "Windows BLE watcher stopped (Bluetooth error " +
                                   std::to_string(static_cast<int>(args.Error())) +
                                   "); check the adapter and restart the app";
                    queue->ready.notify_one();
                }
            );
            watcher.Start();
            LOG_INFO("Scanning for Bluetooth LE advertisements");
            return true;
        }
        catch (const winrt::hresult_error &error)
        {
            LOG_ERROR(
                "Starting BLE scanner: " + winrt::to_string(error.message()) + " (HRESULT " +
                std::to_string(error.code().value) + ")"
            );
            Stop();
            return false;
        }
    }

    // On Windows the watcher object is the connection the Linux scanner reopens.
    bool Reopen()
    {
        Stop();
        return Start(wanted);
    }

    // Covers a status that changed without the Stopped event arriving.
    void Recheck()
    {
        const auto status = CurrentStatus();

        if (status == WatcherStatus::Started || status == WatcherStatus::Stopping)
        {
            return;
        }

        // Reported rather than restarted here: restarting would leave the watch
        // polling a scanner that a failed Start has already emptied, and
        // Process would throw into the event loop.
        Fail("The BLE watcher is no longer running");
    }

    bool IsScanning() const { return CurrentStatus() == WatcherStatus::Started; }

    // WinRT exposes no adapter object of its own behind the watcher, so this
    // and IsPowered answer the same question here.
    bool HasAdapter() const { return watcher != nullptr; }

    // Windows does not expose the radio itself either. An aborted watcher is
    // the closest signal, and switching the radio off is what produces one.
    bool IsPowered() const { return watcher && CurrentStatus() != WatcherStatus::Aborted; }

    void Stop()
    {
        received.revoke();
        stopped.revoke();
        if (watcher)
        {
            try
            {
                watcher.Stop();
            }
            catch (const winrt::hresult_error &error)
            {
                LOG_ERROR("Stopping BLE scanner: " + winrt::to_string(error.message()));
            }
            watcher = nullptr;
        }
        state.reset();
        if (apartment)
        {
            winrt::uninit_apartment();
            apartment = false;
        }
    }

    bool Process(int timeoutMs)
    {
        if (!state || !handler)
        {
            throw std::logic_error("BLE scanner needs Start and OnAdvertisement before Process");
        }

        std::deque<Packet> packets;
        {
            std::unique_lock lock(state->mutex);
            state->ready.wait_for(
                lock, std::chrono::milliseconds(timeoutMs),
                [this] { return !state->packets.empty() || !state->error.empty(); }
            );
            if (!state->error.empty())
            {
                LOG_ERROR(state->error);
                return false;
            }
            packets.swap(state->packets);
        }
        for (const auto &packet : packets)
        {
            handler(packet.address, packet.data);
        }
        return true;
    }

private:
    using Watcher =
        winrt::Windows::Devices::Bluetooth::Advertisement::BluetoothLEAdvertisementWatcher;
    using WatcherStatus = Advertisement::BluetoothLEAdvertisementWatcherStatus;
    static constexpr size_t kMaxQueued = 4096;

    // So every way of noticing a dead watcher leaves through Process.
    void Fail(const std::string &reason)
    {
        if (!state)
        {
            return;
        }

        std::lock_guard lock(state->mutex);
        state->error = reason;
        state->ready.notify_one();
    }

    WatcherStatus CurrentStatus() const
    {
        if (!watcher)
        {
            return WatcherStatus::Stopped;
        }

        try
        {
            return watcher.Status();
        }
        catch (const winrt::hresult_error &error)
        {
            LOG_ERROR("Reading the BLE watcher status: " + winrt::to_string(error.message()));
            return WatcherStatus::Aborted;
        }
    }

    struct Packet
    {
        std::string address;
        std::vector<uint8_t> data;
    };

    struct State
    {
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<Packet> packets;
        std::string error;
    };

    static std::string Address(uint64_t value)
    {
        constexpr int kAddressBytes = 6;
        constexpr int kBitsPerByte = 8;
        constexpr uint64_t kByteMask = 0xff;
        std::ostringstream text;
        text << std::uppercase << std::hex << std::setfill('0');
        for (int byte = kAddressBytes - 1; byte >= 0; --byte)
        {
            if (byte != kAddressBytes - 1)
            {
                text << ':';
            }
            text << std::setw(2) << ((value >> (byte * kBitsPerByte)) & kByteMask);
        }
        return text.str();
    }

    Handler handler;
    uint16_t wanted = 0;
    std::shared_ptr<State> state;
    Watcher watcher{nullptr};
    Watcher::Received_revoker received;
    Watcher::Stopped_revoker stopped;
    bool apartment = false;
};

} // namespace bt
