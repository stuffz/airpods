#pragma once

// The --keys runner: asks the buds for their proximity keys once and stores
// them. The material never reaches the terminal - it identifies this hardware
// to anything listening for its advertisements - so only the types, lengths
// and a short fingerprint are printed.

#include "aap/proximity_keys.hpp"
#include "aap/session.hpp"
#include "bt/bluez_devices.hpp"
#include "core/key_store.hpp"
#include "core/logger.hpp"
#include "core/options.hpp"
#include "core/stop_flag.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace core
{

class KeyFetch
{
public:
    explicit KeyFetch(const Options &opts) : options(opts) {}

    int Run()
    {
        const std::string address = ResolveAddress();
        if (address.empty())
        {
            std::cerr << "The AirPods must be out of the case and connected for this.\n";
            return kFailure;
        }

        if (!session.Connect(address))
        {
            return kFailure;
        }

        session.OnProximityKeys([this](std::span<const uint8_t> packet) { Store(packet); });

        // The buds ignore everything until the handshake lands, so the request
        // has to wait for the session rather than race it. On directly attached
        // hardware the handshake wins that race anyway; put the adapter behind
        // anything slower -- USB/IP forwarding it into a VM, say -- and the
        // request arrives first, is dropped, and no reply ever comes.
        const auto ready = Clock::now() + std::chrono::milliseconds(kReadyTimeoutMs);

        while (stopRequested == 0 && !session.IsReady() && Clock::now() < ready)
        {
            if (!session.Poll(kPollMs))
            {
                session.Close();
                return kFailure;
            }
        }

        if (!session.IsReady())
        {
            // Worth trying regardless: a firmware that never reports ready may
            // still answer, and this is the behaviour that shipped before.
            LOG_WARN("Control link never became ready; requesting the keys anyway");
        }

        if (!session.RequestProximityKeys())
        {
            session.Close();
            return kFailure;
        }

        const auto deadline = Clock::now() + std::chrono::milliseconds(kTimeoutMs);

        while (stopRequested == 0 && !answered && Clock::now() < deadline)
        {
            if (!session.Poll(kPollMs))
            {
                break;
            }
        }

        session.Close();

        if (!answered)
        {
            std::cerr << "No key reply; this firmware may not answer opcode 0x30.\n";
            return kFailure;
        }

        return kSuccess;
    }

private:
    using Clock = std::chrono::steady_clock;

    static constexpr int kSuccess = 0;
    static constexpr int kFailure = 1;
    static constexpr int kPollMs = 200;
    static constexpr int kTimeoutMs = 5000;
    // Long enough for the handshake and the feature acknowledgement, including
    // the 1.5s fallback timer the session falls back on when a firmware never
    // acknowledges the features.
    static constexpr int kReadyTimeoutMs = 4000;
    static constexpr size_t kFingerprintBytes = 2;

    void Store(std::span<const uint8_t> packet)
    {
        const auto keys = aap::ParseProximityKeys(packet);
        answered = true;

        if (keys.empty())
        {
            std::cerr << "The buds replied but carried no keys.\n";
            return;
        }

        const std::filesystem::path path = KeyPath();
        std::error_code failed;
        std::filesystem::create_directories(path.parent_path(), failed);

        std::ofstream file(path, std::ios::trunc);
        if (!file)
        {
            std::cerr << "Cannot write " << path.string() << "\n";
            return;
        }

        for (const auto &key : keys)
        {
            file << aap::KeyTypeName(key.type) << " " << ToHex(key.material) << "\n";

            std::cout << aap::KeyTypeName(key.type) << "  " << key.material.size()
                      << " bytes  starts " << Fingerprint(key.material) << "\n";
        }

        file.close();
        std::filesystem::permissions(
            path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace, failed
        );

        std::cout << "\nWritten to " << path.string() << " (owner read/write only).\n"
                  << "Keep it private: it identifies these AirPods to anything listening.\n";
    }

    std::filesystem::path KeyPath() const
    {
        return options.keyFile.empty() ? KeyStore::Path() : options.keyFile;
    }

    static std::string HomeDirectory()
    {
        const char *home = std::getenv("HOME");
        return home != nullptr ? home : ".";
    }

    static std::string ToHex(const std::vector<uint8_t> &bytes)
    {
        constexpr char kDigits[] = "0123456789abcdef";

        std::string hex;
        hex.reserve(bytes.size() * 2);

        for (const uint8_t byte : bytes)
        {
            hex.push_back(kDigits[byte >> 4]);
            hex.push_back(kDigits[byte & 0x0f]);
        }

        return hex;
    }

    static std::string Fingerprint(const std::vector<uint8_t> &bytes)
    {
        const std::vector<uint8_t> head(
            bytes.begin(),
            bytes.begin() + static_cast<long>(std::min(kFingerprintBytes, bytes.size()))
        );

        return ToHex(head) + "...";
    }

    std::string ResolveAddress() const
    {
        if (!options.address.empty())
        {
            return options.address;
        }

        const auto devices = discovery.List();
        const auto *airpods = bt::BluezDevices::PickAirPods(devices);

        return airpods != nullptr ? airpods->address : std::string{};
    }

    const Options &options;
    bt::BluezDevices discovery;
    aap::Session session;
    bool answered = false;
};

} // namespace core
