#pragma once

#include "aap/packets.hpp"

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

namespace core
{

struct Options
{
    std::filesystem::path keyFile;
    std::string address; // empty means "discover it"
    int intervalMs = 1000;
    bool gestures = false;
    aap::HeadTrackingVariant headTracking = aap::HeadTrackingVariant::Alternate;
    bool once = false;
    bool list = false;
    bool console = false;
    bool ble = false;
    bool keys = false;
    bool debug = false;
};

enum class ParseResult
{
    Run,
    Exit,
    Error
};

inline void PrintUsage(std::ostream &out)
{
    out << "airpods - AirPods battery in the system tray\n"
        << "\n"
        << "Usage: airpods [options]\n"
        << "\n"
        << "Runs as a tray icon by default; use --ble for advertisement diagnostics.\n"
        << "\n"
        << "Options:\n"
        << "  -a, --address MAC   AirPods Bluetooth address (default: autodetect)\n"
#ifndef _WIN32
        << "  -c, --console       Print battery readings to the terminal instead\n"
        << "  -i, --interval MS   Console print interval in ms (default: 1000)\n"
        << "  -g, --gestures      Also detect head nods and shakes (console only)\n"
        << "  -t, --tracking KIND Head tracking packets: alt (default) or macos\n"
        << "  -1, --once          Print one battery reading and exit\n"
        << "  -l, --list          List connected Bluetooth devices and exit\n"
#endif
        << "  -b, --ble           Watch BLE proximity adverts and print lid events\n"
#ifndef _WIN32
        << "  -k, --keys          Fetch the proximity keys and store them, then exit\n"
#endif
        << "      --key-file PATH Load proximity keys from this file (or write with --keys)\n"
        << "  -d, --debug         Log every protocol packet to stderr\n"
        << "  -h, --help          Show this help\n"
        << "  -V, --version       Show version\n"
        << "\n"
#ifdef _WIN32
        << "Windows supports the tray and --ble using imported proximity keys.\n"
        << "Use --key-file PATH or place the file in %LOCALAPPDATA%/airpods/proximity-keys.\n";
#else
        << "The AirPods must be paired and connected before this runs, and no other\n"
        << "device may hold the control link - they accept only one at a time.\n"
        << "\n"
        << "Which head tracking packets a firmware answers varies; if --gestures\n"
        << "never reports anything, try --tracking macos.\n";
#endif
}

inline ParseResult ParseOptions(int argc, char **argv, Options &options)
{
    const std::vector<std::string_view> args(argv + 1, argv + argc);

    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string_view arg = args[i];

        const auto next = [&](std::string_view &value)
        {
            if (i + 1 >= args.size())
            {
                std::cerr << "Missing value for " << arg << "\n";
                return false;
            }

            value = args[++i];
            return true;
        };

        if (arg == "-h" || arg == "--help")
        {
            PrintUsage(std::cout);
            return ParseResult::Exit;
        }

        if (arg == "-V" || arg == "--version")
        {
            std::cout << "airpods " << GIT_HASH << " (built " << BUILD_DATE << ")\n";
            return ParseResult::Exit;
        }

        if (arg == "-g" || arg == "--gestures")
        {
            options.gestures = true;
            continue;
        }

        if (arg == "-c" || arg == "--console")
        {
            options.console = true;
            continue;
        }

        if (arg == "-1" || arg == "--once")
        {
            options.once = true;
            continue;
        }

        if (arg == "-k" || arg == "--keys")
        {
            options.keys = true;
            continue;
        }

        if (arg == "-b" || arg == "--ble")
        {
            options.ble = true;
            continue;
        }

        if (arg == "-l" || arg == "--list")
        {
            options.list = true;
            continue;
        }

        if (arg == "-d" || arg == "--debug")
        {
            options.debug = true;
            continue;
        }

        if (arg == "-t" || arg == "--tracking")
        {
            std::string_view value;
            if (!next(value))
            {
                return ParseResult::Error;
            }

            if (value == "alt")
            {
                options.headTracking = aap::HeadTrackingVariant::Alternate;
            }
            else if (value == "macos")
            {
                options.headTracking = aap::HeadTrackingVariant::Macos;
            }
            else
            {
                std::cerr << "Head tracking must be 'alt' or 'macos'\n";
                return ParseResult::Error;
            }

            continue;
        }

        if (arg == "-a" || arg == "--address")
        {
            std::string_view value;
            if (!next(value))
            {
                return ParseResult::Error;
            }

            options.address = value;
            continue;
        }

        if (arg == "--key-file")
        {
            std::string_view value;
            if (!next(value) || value.empty())
            {
                return ParseResult::Error;
            }
            options.keyFile = std::filesystem::path(std::u8string(value.begin(), value.end()));
            continue;
        }

        if (arg == "-i" || arg == "--interval")
        {
            std::string_view value;
            if (!next(value))
            {
                return ParseResult::Error;
            }

            const std::string text(value);
            const char *end = text.data() + text.size();

            int parsed = 0;
            const auto [stopped, ec] = std::from_chars(text.data(), end, parsed);

            if (ec != std::errc{} || stopped != end || parsed <= 0)
            {
                std::cerr << "Interval must be a positive number of milliseconds\n";
                return ParseResult::Error;
            }

            options.intervalMs = parsed;
            continue;
        }

        std::cerr << "Unknown option: " << arg << "\n";
        PrintUsage(std::cerr);
        return ParseResult::Error;
    }

#ifdef _WIN32
    if (options.keys || options.console || options.once || options.list || options.gestures)
    {
        std::cerr
            << "Windows supports the battery tray and --ble; control-link modes require Linux.\n";
        return ParseResult::Error;
    }
#endif
    return ParseResult::Run;
}

} // namespace core
