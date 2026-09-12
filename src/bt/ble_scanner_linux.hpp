#pragma once

// Watches BlueZ for BLE advertisements and hands their manufacturer data up.
//
// Discovery has to be asked for explicitly, and with DuplicateData set: BlueZ
// suppresses repeat advertisements by default, which would collapse exactly
// the stream of changing payloads this exists to read.

#include "core/logger.hpp"

#include <systemd/sd-bus.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <utility>

namespace bt
{

class BleScanner
{
public:
    using Handler = std::function<void(const std::string &, std::span<const uint8_t>)>;

    BleScanner() = default;

    ~BleScanner()
    {
        Stop();
        Discard();
    }

    BleScanner(const BleScanner &) = delete;
    BleScanner &operator=(const BleScanner &) = delete;

    void OnAdvertisement(Handler callback) { handler = std::move(callback); }

    // Begins wanting to scan. Discovery itself starts when the adapter is
    // powered - now, or whenever BlueZ later says it is - and is restarted
    // when BlueZ drops it. False only when the bus is unusable.
    bool Start(uint16_t vendor)
    {
        wanted = vendor;
        active = true;
        return Begin();
    }

    // The descriptor changes with the connection, so whatever was watching the
    // old one has to be pointed at the new one afterwards.
    bool Reopen()
    {
        Discard();
        return Begin();
    }

    // Every other path here is driven by a signal, so a StartDiscovery that
    // failed or a Discovering signal that never arrived leaves nothing to
    // retry on. This is the one check that does not wait to be told.
    //
    // Discovering is the adapter's, not this client's: another client scanning
    // reads as healthy even once our own session is gone. BlueZ offers no
    // per-client answer, so that case is left to the advertisements drying up.
    void Recheck()
    {
        if (!active || bus == nullptr)
        {
            return;
        }

        if (adapter.empty())
        {
            adapter = FindAdapter();
        }

        if (scanning && ReadAdapterFlag("Discovering"))
        {
            return;
        }

        if (scanning)
        {
            LOG_WARN("Discovery is off although it was believed on; restarting");
            scanning = false;
        }

        TryDiscovery();
    }

    bool IsScanning() const { return scanning; }

    bool HasAdapter() const { return !adapter.empty(); }

    // Kept from the signal rather than read back: the tray repaints far more
    // often than an adapter changes its mind.
    bool IsPowered() const { return powered; }

    void Stop()
    {
        active = false;

        if (!scanning)
        {
            return;
        }

        // Cleared first so the Discovering=false that follows is not taken
        // for BlueZ dropping the session.
        scanning = false;
        Call(adapter.c_str(), "StopDiscovery");
    }

    int Descriptor() const { return bus != nullptr ? sd_bus_get_fd(bus) : -1; }

    // Drains everything queued, then waits at most timeoutMs for more.
    bool Process(int timeoutMs)
    {
        if (bus == nullptr)
        {
            return false;
        }

        for (;;)
        {
            const int handled = sd_bus_process(bus, nullptr);
            if (handled < 0)
            {
                LOG_ERROR(std::string("sd_bus_process: ") + std::strerror(-handled));
                return false;
            }

            if (handled == 0)
            {
                break;
            }
        }

        constexpr uint64_t kMicrosecondsPerMs = 1000;
        const int waited = sd_bus_wait(bus, static_cast<uint64_t>(timeoutMs) * kMicrosecondsPerMs);

        if (waited < 0)
        {
            LOG_ERROR(std::string("sd_bus_wait: ") + std::strerror(-waited));
            return false;
        }

        return true;
    }

private:
    // BlueZ answers this when the caller already has what it asked for, which
    // is the wanted state rather than a failure.
    static constexpr const char *kInProgressError = "org.bluez.Error.InProgress";

    // Every call below blocks the thread that makes it, which is the one
    // painting the tray. Measured round trips here are under a millisecond, so
    // this sits far above anything healthy and far below sd-bus's 25s default.
    static constexpr uint64_t kCallTimeoutUs = 2000000;

    bool Begin()
    {
        if (bus == nullptr)
        {
            const int opened = sd_bus_open_system(&bus);
            if (opened < 0)
            {
                bus = nullptr;
                LOG_ERROR(std::string("Cannot reach the system bus: ") + std::strerror(-opened));
                return false;
            }

            sd_bus_set_method_call_timeout(bus, kCallTimeoutUs);
        }

        if (!AddMatches())
        {
            return false;
        }

        adapter = FindAdapter();
        if (adapter.empty())
        {
            LOG_WARN("No Bluetooth adapter published by BlueZ; scanning starts when one appears");
            return true;
        }

        TryDiscovery();
        return true;
    }

    // A private connection rather than the shared default, so a dead one can
    // be thrown away without disturbing anything else in the process that
    // talks to BlueZ over the same bus.
    void Discard()
    {
        if (bus != nullptr)
        {
            sd_bus_flush_close_unref(bus);
            bus = nullptr;
        }

        matched = false;
        scanning = false;
        powered = false;
        adapter.clear();
    }

    void TryDiscovery()
    {
        if (!active || scanning || adapter.empty())
        {
            return;
        }

        powered = ReadAdapterFlag("Powered");
        if (!powered)
        {
            LOG_INFO("Bluetooth is off; scanning starts when it is powered on");
            return;
        }

        if (!SetFilter() || !Call(adapter.c_str(), "StartDiscovery"))
        {
            return;
        }

        scanning = true;
        LOG_INFO("Scanning for advertisements on " + adapter);
    }

    bool ReadAdapterFlag(const char *property) const
    {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        int value = 0;

        const int status = sd_bus_get_property_trivial(
            bus, "org.bluez", adapter.c_str(), "org.bluez.Adapter1", property, &error, 'b', &value
        );

        sd_bus_error_free(&error);
        return status >= 0 && value != 0;
    }

    std::string FindAdapter() const
    {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *reply = nullptr;

        const int called = sd_bus_call_method(
            bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
            &error, &reply, ""
        );
        if (called < 0)
        {
            sd_bus_error_free(&error);
            return {};
        }

        std::string found;
        sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");

        while (sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}") > 0)
        {
            const char *path = nullptr;
            sd_bus_message_read_basic(reply, 'o', static_cast<void *>(&path));

            sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");

            while (sd_bus_message_enter_container(reply, 'e', "sa{sv}") > 0)
            {
                const char *interface = nullptr;
                sd_bus_message_read_basic(reply, 's', static_cast<void *>(&interface));

                if (found.empty() && std::strcmp(interface, "org.bluez.Adapter1") == 0)
                {
                    found = path;
                }

                sd_bus_message_skip(reply, "a{sv}");
                sd_bus_message_exit_container(reply);
            }

            sd_bus_message_exit_container(reply);
            sd_bus_message_exit_container(reply);
        }

        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return found;
    }

    bool SetFilter() const
    {
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *message = nullptr;

        int status = sd_bus_message_new_method_call(
            bus, &message, "org.bluez", adapter.c_str(), "org.bluez.Adapter1", "SetDiscoveryFilter"
        );

        if (status >= 0)
        {
            status = sd_bus_message_open_container(message, 'a', "{sv}");
        }

        if (status >= 0)
        {
            status = sd_bus_message_append(message, "{sv}", "Transport", "s", "le");
        }

        if (status >= 0)
        {
            status = sd_bus_message_append(message, "{sv}", "DuplicateData", "b", 1);
        }

        if (status >= 0)
        {
            status = sd_bus_message_close_container(message);
        }

        if (status >= 0)
        {
            status = sd_bus_call(bus, message, 0, &error, nullptr);
        }

        if (status < 0)
        {
            LOG_ERROR(
                std::string("SetDiscoveryFilter: ") +
                (error.message != nullptr ? error.message : std::strerror(-status))
            );
        }

        sd_bus_message_unref(message);
        sd_bus_error_free(&error);
        return status >= 0;
    }

    bool Call(const char *path, const char *method) const
    {
        sd_bus_error error = SD_BUS_ERROR_NULL;

        const int status = sd_bus_call_method(
            bus, "org.bluez", path, "org.bluez.Adapter1", method, &error, nullptr, ""
        );

        const bool alreadyDone = sd_bus_error_has_name(&error, kInProgressError) != 0;

        if (status < 0 && !alreadyDone)
        {
            LOG_ERROR(
                std::string(method) + ": " +
                (error.message != nullptr ? error.message : std::strerror(-status))
            );
        }

        sd_bus_error_free(&error);
        return status >= 0 || alreadyDone;
    }

    bool AddMatches()
    {
        if (matched)
        {
            return true;
        }

        const int adapterChanged = sd_bus_add_match(
            bus, nullptr,
            "type='signal',sender='org.bluez',"
            "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
            "arg0='org.bluez.Adapter1'",
            OnAdapterChanged, this
        );

        const int changed = sd_bus_add_match(
            bus, nullptr,
            "type='signal',sender='org.bluez',"
            "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
            "arg0='org.bluez.Device1'",
            OnPropertiesChanged, this
        );

        const int added = sd_bus_add_match(
            bus, nullptr,
            "type='signal',sender='org.bluez',"
            "interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded'",
            OnInterfacesAdded, this
        );

        if (adapterChanged < 0 || changed < 0 || added < 0)
        {
            LOG_ERROR("Failed to subscribe to BlueZ signals");
            return false;
        }

        matched = true;
        return true;
    }

    static int OnAdapterChanged(sd_bus_message *message, void *userdata, sd_bus_error *)
    {
        auto *self = static_cast<BleScanner *>(userdata);

        const char *interface = nullptr;
        if (sd_bus_message_read_basic(message, 's', static_cast<void *>(&interface)) < 0)
        {
            return 0;
        }

        if (self->adapter != sd_bus_message_get_path(message))
        {
            return 0;
        }

        self->ReadAdapterProperties(message);
        return 0;
    }

    void ReadAdapterProperties(sd_bus_message *message)
    {
        if (sd_bus_message_enter_container(message, 'a', "{sv}") < 0)
        {
            return;
        }

        while (sd_bus_message_enter_container(message, 'e', "sv") > 0)
        {
            const char *key = nullptr;
            sd_bus_message_read_basic(message, 's', static_cast<void *>(&key));

            int value = 0;
            const bool isFlag = sd_bus_message_enter_container(message, 'v', "b") > 0;

            if (isFlag)
            {
                sd_bus_message_read_basic(message, 'b', static_cast<void *>(&value));
                sd_bus_message_exit_container(message);
            }
            else
            {
                sd_bus_message_skip(message, "v");
            }

            sd_bus_message_exit_container(message);

            if (!isFlag)
            {
                continue;
            }

            if (std::strcmp(key, "Powered") == 0)
            {
                OnPowered(value != 0);
            }
            else if (std::strcmp(key, "Discovering") == 0)
            {
                OnDiscovering(value != 0);
            }
        }

        sd_bus_message_exit_container(message);
    }

    void OnPowered(bool on)
    {
        powered = on;

        if (on)
        {
            LOG_INFO("Bluetooth powered on");
            TryDiscovery();
            return;
        }

        if (scanning)
        {
            LOG_WARN("Bluetooth powered off; scanning paused until it is back");
        }

        scanning = false;
    }

    // BlueZ ends every client's session when the adapter goes down and does
    // not bring it back on its own.
    void OnDiscovering(bool on)
    {
        if (on || !scanning)
        {
            return;
        }

        LOG_WARN("BlueZ stopped discovery; restarting");
        scanning = false;
        TryDiscovery();
    }

    static int OnPropertiesChanged(sd_bus_message *message, void *userdata, sd_bus_error *)
    {
        auto *self = static_cast<BleScanner *>(userdata);

        const char *interface = nullptr;
        if (sd_bus_message_read_basic(message, 's', static_cast<void *>(&interface)) < 0)
        {
            return 0;
        }

        self->ReadProperties(message, sd_bus_message_get_path(message));
        return 0;
    }

    static int OnInterfacesAdded(sd_bus_message *message, void *userdata, sd_bus_error *)
    {
        auto *self = static_cast<BleScanner *>(userdata);

        const char *path = nullptr;
        if (sd_bus_message_read_basic(message, 'o', static_cast<void *>(&path)) < 0)
        {
            return 0;
        }

        if (sd_bus_message_enter_container(message, 'a', "{sa{sv}}") < 0)
        {
            return 0;
        }

        bool adapterAppeared = false;

        while (sd_bus_message_enter_container(message, 'e', "sa{sv}") > 0)
        {
            const char *interface = nullptr;
            sd_bus_message_read_basic(message, 's', static_cast<void *>(&interface));

            if (std::strcmp(interface, "org.bluez.Device1") == 0)
            {
                self->ReadProperties(message, path);
            }
            else
            {
                adapterAppeared =
                    adapterAppeared || std::strcmp(interface, "org.bluez.Adapter1") == 0;
                sd_bus_message_skip(message, "a{sv}");
            }

            sd_bus_message_exit_container(message);
        }

        sd_bus_message_exit_container(message);

        // A dongle plugged in, or BlueZ restarted: the old session is gone
        // with the old object, whatever this side still believes.
        if (adapterAppeared)
        {
            LOG_INFO(std::string("Bluetooth adapter appeared: ") + path);
            self->adapter = path;
            self->scanning = false;
            self->TryDiscovery();
        }

        return 0;
    }

    void ReadProperties(sd_bus_message *message, const char *path)
    {
        if (sd_bus_message_enter_container(message, 'a', "{sv}") < 0)
        {
            return;
        }

        while (sd_bus_message_enter_container(message, 'e', "sv") > 0)
        {
            const char *key = nullptr;
            sd_bus_message_read_basic(message, 's', static_cast<void *>(&key));

            if (std::strcmp(key, "ManufacturerData") == 0)
            {
                ReadManufacturerData(message, path);
            }
            else
            {
                sd_bus_message_skip(message, "v");
            }

            sd_bus_message_exit_container(message);
        }

        sd_bus_message_exit_container(message);
    }

    void ReadManufacturerData(sd_bus_message *message, const char *path)
    {
        char type = 0;
        const char *contents = nullptr;

        if (sd_bus_message_peek_type(message, &type, &contents) < 0 || contents == nullptr)
        {
            return;
        }

        if (sd_bus_message_enter_container(message, 'v', contents) < 0)
        {
            return;
        }

        if (sd_bus_message_enter_container(message, 'a', "{qv}") >= 0)
        {
            while (sd_bus_message_enter_container(message, 'e', "qv") > 0)
            {
                uint16_t vendor = 0;
                sd_bus_message_read_basic(message, 'q', static_cast<void *>(&vendor));

                if (vendor == wanted)
                {
                    Deliver(message, path);
                }
                else
                {
                    sd_bus_message_skip(message, "v");
                }

                sd_bus_message_exit_container(message);
            }

            sd_bus_message_exit_container(message);
        }

        sd_bus_message_exit_container(message);
    }

    void Deliver(sd_bus_message *message, const char *path)
    {
        if (sd_bus_message_enter_container(message, 'v', "ay") < 0)
        {
            return;
        }

        const void *bytes = nullptr;
        size_t size = 0;

        if (sd_bus_message_read_array(message, 'y', &bytes, &size) >= 0 && handler)
        {
            handler(
                AddressFromPath(path),
                std::span<const uint8_t>(static_cast<const uint8_t *>(bytes), size)
            );
        }

        sd_bus_message_exit_container(message);
    }

    // /org/bluez/hci0/dev_00_11_22_33_44_55 -> 00:11:22:33:44:55. Taken from
    // the path rather than fetched, because the device may already be gone.
    static std::string AddressFromPath(const char *path)
    {
        std::string text(path == nullptr ? "" : path);
        const size_t marker = text.rfind("/dev_");

        if (marker == std::string::npos)
        {
            return text;
        }

        std::string address = text.substr(marker + 5);
        for (char &character : address)
        {
            if (character == '_')
            {
                character = ':';
            }
        }

        return address;
    }

    sd_bus *bus = nullptr;
    std::string adapter;
    Handler handler;
    uint16_t wanted = 0;
    bool active = false;
    bool matched = false;
    bool scanning = false;
    bool powered = false;
};

} // namespace bt
