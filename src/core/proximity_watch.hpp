#pragma once

#include "core/local_time.hpp"

// The --ble runner: prints Apple proximity advertisements as they arrive, and
// says plainly when the case lid opens or shuts.

#include "aap/case_advert.hpp"
#include "aap/encrypted_payload.hpp"
#include "aap/proximity.hpp"
#include "bt/aes.hpp"
#include "bt/ble_scanner.hpp"
#include "bt/rpa.hpp"
#include "core/key_store.hpp"
#include "core/logger.hpp"
#include "core/options.hpp"
#include "core/stop_flag.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <span>
#include <sstream>
#include <string>

namespace core
{

class ProximityWatch
{
public:
    explicit ProximityWatch(const Options &opts) : options(opts) {}

    int Run()
    {
        if (keys.Load(options.keyFile))
        {
            std::cout << "Proximity keys loaded: address resolution and exact levels on.\n";
        }
        else
        {
            std::cout << "No proximity keys; use --key-file with keys fetched on Linux.\n";
#ifdef _WIN32
            return kFailure;
#endif
        }

        scanner.OnAdvertisement([this](const std::string &address, std::span<const uint8_t> data)
                                { Handle(address, data); });

        if (!scanner.Start(aap::kAppleVendorId))
        {
            return kFailure;
        }

        std::cout << "Watching for AirPods advertisements. Open the case lid.\n" << std::flush;

        while (stopRequested == 0)
        {
            if (!scanner.Process(kPollMs))
            {
                scanner.Stop();
                return kFailure;
            }
        }

        scanner.Stop();
        return kSuccess;
    }

private:
    static constexpr int kSuccess = 0;
    static constexpr int kFailure = 1;
    static constexpr int kPollMs = 500;

    struct Seen
    {
        std::string summary;
        int lid = -1;
    };

    void Handle(const std::string &address, std::span<const uint8_t> data)
    {
        if (!options.address.empty() && address != options.address)
        {
            return;
        }

        // Resolved before any format check: an advert that is ours matters even
        // when it is not a proximity payload, because it says which rotating
        // address the hardware is using right now.
        const bool mine = keys.HasIrk() && bt::AddressBelongsTo(address, keys.Irk());

        if (mine)
        {
            const int type = data.empty() ? -1 : data[0];
            std::cout << address << "  [MINE]  apple type 0x" << std::hex << type << std::dec
                      << "  " << data.size() << " bytes\n"
                      << std::flush;
        }

        if (options.debug)
        {
            LOG_DEBUG(address + "  " + ToHex(data));
        }

        if (!data.empty() && data[0] == aap::kProximityType)
        {
            PrintRaw(address, data);
        }

        if (aap::IsCaseAdvert(data))
        {
            PrintCase(address, data);
            return;
        }

        const auto report = aap::ParseProximity(data);
        if (!report)
        {
            return;
        }

        std::string summary = Describe(*report);

        if (mine)
        {
            summary += "  [mine]";
        }

        if (mine && keys.HasEncryption())
        {
            const auto exact =
                aap::ParseEncryptedBattery(data, keys.Encryption(), report->primaryLeft);

            if (exact)
            {
                summary += "  EXACT L " + Exact(exact->leftLevel) + Charge(exact->leftCharging) +
                           " R " + Exact(exact->rightLevel) + Charge(exact->rightCharging) +
                           " case " + Exact(exact->caseLevel) + Charge(exact->caseCharging);
            }
        }
        Seen &previous = history[address];

        const int lid = report->lid ? static_cast<int>(*report->lid) : -1;

        if (lid != previous.lid && report->lid)
        {
            std::cout << address << "  LID "
                      << (*report->lid == aap::LidState::Open ? "OPENED" : "CLOSED")
                      << "   (open count " << static_cast<int>(report->lidCounter) << ")\n";
        }

        // Levels only move in 10% steps here, so unchanged payloads repeat
        // constantly; only the transitions are worth a line.
        if (summary != previous.summary)
        {
            std::cout << address << "  " << summary << "\n";
        }

        std::cout << std::flush;
        previous.summary = summary;
        previous.lid = lid;
    }

    // One line per proximity advertisement whatever its layout, so the bytes can
    // be watched while the lid or the pods move. The shut-case format
    // (07 11 06 + one AES block) has no cleartext at all; the decrypted block
    // is the only thing in it to read.
    void PrintRaw(const std::string &address, std::span<const uint8_t> data)
    {
        std::cout << Now() << "  " << address << "  " << ToHex(data);

        if (keys.HasEncryption() && data.size() >= bt::kAesBlockSize)
        {
            const auto block = bt::AesDecryptBlock(keys.Encryption(), data.last(bt::kAesBlockSize));

            if (block)
            {
                std::cout << "  ->  " << ToHex(*block);
            }
        }

        std::cout << "\n";
    }

    void PrintCase(const std::string &address, std::span<const uint8_t> data)
    {
        if (!keys.HasEncryption())
        {
            return;
        }

        const auto report = aap::ParseCaseAdvert(data, keys.Encryption());
        if (!report)
        {
            std::cout << address << "  case advert, not ours\n" << std::flush;
            return;
        }

        const auto &b = report->battery;
        std::cout << address << "  CASE lid "
                  << (report->lid == aap::LidState::Open ? "OPEN " : "SHUT ") << "(open count "
                  << static_cast<int>(report->lidCounter) << ")  L " << Exact(b.leftLevel)
                  << Charge(b.leftCharging) << " R " << Exact(b.rightLevel)
                  << Charge(b.rightCharging) << " case " << Exact(b.caseLevel)
                  << Charge(b.caseCharging);

        const std::string anomalies = aap::CaseAdvertAnomalies(*report);
        if (!anomalies.empty())
        {
            std::cout << "  UNEXPECTED:" << anomalies;
        }

        std::cout << "\n" << std::flush;
    }

    static std::string Now()
    {
        const auto timeT = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

        const std::tm tm = core::LocalTime(timeT);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S");
        return oss.str();
    }

    static std::string Describe(const aap::ProximityReport &report)
    {
        std::string text = "model " + Hex16(report.model) + "  L " + Level(report.leftLevel) +
                           Charge(report.leftCharging) + "  R " + Level(report.rightLevel) +
                           Charge(report.rightCharging) + "  case " + Level(report.caseLevel) +
                           Charge(report.caseCharging);

        if (report.leftInEar || report.rightInEar)
        {
            text += "  in-ear";
            text += report.leftInEar ? " L" : "";
            text += report.rightInEar ? " R" : "";
        }

        if (report.bothPodsInCase)
        {
            text += "  both in case";
        }
        else if (report.onePodInCase)
        {
            text += "  one in case";
        }

        return text;
    }

    static std::string Hex16(uint16_t value)
    {
        constexpr char kDigits[] = "0123456789abcdef";
        constexpr int kNibbles = 4;
        constexpr int kBitsPerNibble = 4;

        std::string text;
        for (int shift = (kNibbles - 1) * kBitsPerNibble; shift >= 0; shift -= kBitsPerNibble)
        {
            text.push_back(kDigits[(value >> shift) & 0x0f]);
        }

        return text;
    }

    static std::string Exact(uint8_t level)
    {
        return level == aap::kLevelAbsent ? "--" : std::to_string(static_cast<int>(level)) + "%";
    }

    static std::string Level(uint8_t level)
    {
        return level == aap::kUnknownLevel ? "--" : std::to_string(static_cast<int>(level)) + "%";
    }

    static std::string Charge(bool charging) { return charging ? "+" : " "; }

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

    const Options &options;
    bt::BleScanner scanner;
    KeyStore keys;
    std::map<std::string, Seen> history;
};

} // namespace core
