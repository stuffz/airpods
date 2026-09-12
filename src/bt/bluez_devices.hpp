#pragma once

#include "core/logger.hpp"

#include <systemd/sd-bus.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace bt
{

struct BluezDevice
{
    std::string address;
    std::string name;
    bool connected = false;
    bool appleVendor = false;
    bool audioIcon = false;
};

// BlueZ publishes every known device on D-Bus; there is no sysfs or ioctl path
// to the same data that works unprivileged, so ObjectManager is the way in.
class BluezDevices
{
public:
    BluezDevices()
    {
        if (sd_bus_open_system(&bus) >= 0)
        {
            sd_bus_set_method_call_timeout(bus, kCallTimeoutUs);
        }
    }

    ~BluezDevices()
    {
        if (bus != nullptr)
        {
            sd_bus_flush_close_unref(bus);
        }
    }

    BluezDevices(const BluezDevices &) = delete;
    BluezDevices &operator=(const BluezDevices &) = delete;

    bool IsAvailable() const { return bus != nullptr; }

    std::vector<BluezDevice> List() const
    {
        std::vector<BluezDevice> devices;

        if (bus == nullptr)
        {
            LOG_ERROR("No connection to the system bus; is dbus running?");
            return devices;
        }

        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *reply = nullptr;

        const int called = sd_bus_call_method(
            bus, "org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
            &error, &reply, ""
        );
        if (called < 0)
        {
            LOG_ERROR(
                std::string("GetManagedObjects: ") +
                (error.message != nullptr ? error.message : std::strerror(-called))
            );
            sd_bus_error_free(&error);
            return devices;
        }

        ReadObjects(reply, devices);

        sd_bus_message_unref(reply);
        sd_bus_error_free(&error);
        return devices;
    }

    // AirPods identify themselves by Apple's vendor id in Modalias; the audio
    // icon then separates the buds from a Mac or an iPhone on the same adapter.
    static const BluezDevice *PickAirPods(const std::vector<BluezDevice> &devices)
    {
        const BluezDevice *fallback = nullptr;

        for (const auto &device : devices)
        {
            if (!device.connected)
            {
                continue;
            }

            if (device.name.find("AirPods") != std::string::npos)
            {
                return &device;
            }

            if (device.appleVendor && device.audioIcon && fallback == nullptr)
            {
                fallback = &device;
            }
        }

        return fallback;
    }

private:
    static constexpr uint64_t kCallTimeoutUs = 2000000;
    static constexpr const char *kDeviceInterface = "org.bluez.Device1";
    static constexpr const char *kAppleModaliasPrefix = "bluetooth:v004C";

    static void ReadObjects(sd_bus_message *reply, std::vector<BluezDevice> &devices)
    {
        if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{oa{sa{sv}}}") <= 0)
        {
            return;
        }

        while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "oa{sa{sv}}") > 0)
        {
            // The object path is not needed, but it has to be consumed
            // before the interface dictionary that follows it.
            ReadBasicString(reply, SD_BUS_TYPE_OBJECT_PATH);
            ReadInterfaces(reply, devices);
            sd_bus_message_exit_container(reply);
        }

        sd_bus_message_exit_container(reply);
    }

    static void ReadInterfaces(sd_bus_message *reply, std::vector<BluezDevice> &devices)
    {
        if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sa{sv}}") <= 0)
        {
            return;
        }

        while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}") > 0)
        {
            const char *interface = ReadBasicString(reply, SD_BUS_TYPE_STRING);

            BluezDevice device;
            const bool isDevice =
                interface != nullptr && std::strcmp(interface, kDeviceInterface) == 0;

            ReadProperties(reply, device);

            if (isDevice && !device.address.empty())
            {
                devices.push_back(std::move(device));
            }

            sd_bus_message_exit_container(reply);
        }

        sd_bus_message_exit_container(reply);
    }

    static void ReadProperties(sd_bus_message *reply, BluezDevice &device)
    {
        if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_ARRAY, "{sv}") <= 0)
        {
            return;
        }

        while (sd_bus_message_enter_container(reply, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0)
        {
            const char *key = ReadBasicString(reply, SD_BUS_TYPE_STRING);
            if (key != nullptr)
            {
                ReadProperty(reply, key, device);
            }
            sd_bus_message_exit_container(reply);
        }

        sd_bus_message_exit_container(reply);
    }

    static void ReadProperty(sd_bus_message *reply, const char *key, BluezDevice &device)
    {
        char type = '\0';
        const char *contents = nullptr;
        if (sd_bus_message_peek_type(reply, &type, &contents) <= 0 || contents == nullptr)
        {
            return;
        }

        if (std::strcmp(contents, "s") == 0)
        {
            const std::string value = ReadStringVariant(reply);

            // Alias is the name the user set, so it wins over Name whichever
            // order the two properties arrive in.
            const bool isAlias = std::strcmp(key, "Alias") == 0;
            const bool isUnclaimedName = std::strcmp(key, "Name") == 0 && device.name.empty();

            if (std::strcmp(key, "Address") == 0)
            {
                device.address = value;
            }
            else if (isAlias || isUnclaimedName)
            {
                device.name = value;
            }
            else if (std::strcmp(key, "Modalias") == 0)
            {
                device.appleVendor = value.rfind(kAppleModaliasPrefix, 0) == 0;
            }
            else if (std::strcmp(key, "Icon") == 0)
            {
                device.audioIcon = value.rfind("audio-", 0) == 0;
            }

            return;
        }

        if (std::strcmp(contents, "b") == 0 && std::strcmp(key, "Connected") == 0)
        {
            device.connected = ReadBoolVariant(reply);
            return;
        }

        sd_bus_message_skip(reply, "v");
    }

    // sd_bus_message_read_basic() takes void*; the cast keeps the pointer
    // laundering in one place instead of at every call site.
    static const char *ReadBasicString(sd_bus_message *reply, char type)
    {
        const char *value = nullptr;
        sd_bus_message_read_basic(reply, type, static_cast<void *>(&value));
        return value;
    }

    static std::string ReadStringVariant(sd_bus_message *reply)
    {
        if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "s") <= 0)
        {
            return {};
        }

        const char *value = ReadBasicString(reply, SD_BUS_TYPE_STRING);
        sd_bus_message_exit_container(reply);

        return value != nullptr ? std::string(value) : std::string();
    }

    static bool ReadBoolVariant(sd_bus_message *reply)
    {
        if (sd_bus_message_enter_container(reply, SD_BUS_TYPE_VARIANT, "b") <= 0)
        {
            return false;
        }

        int value = 0;
        sd_bus_message_read_basic(reply, SD_BUS_TYPE_BOOLEAN, static_cast<void *>(&value));
        sd_bus_message_exit_container(reply);

        return value != 0;
    }

    sd_bus *bus = nullptr;
};

} // namespace bt
