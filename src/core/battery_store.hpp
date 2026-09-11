#pragma once

// Remembers the last reading across runs.
//
// The buds are silent in a shut case and refuse connections there, so there is
// no way to refresh while they are away. A number with an age on it beats an
// empty window, which is what the display would otherwise fall back to.

#include "aap/battery.hpp"
#include "core/storage_paths.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <system_error>

namespace core
{

class BatteryStore
{
public:
    using Clock = std::chrono::system_clock;

    void Save(const aap::Battery &battery) const
    {
        if (!battery.HasReading())
        {
            return;
        }

        const std::filesystem::path path = Path();
        std::error_code failed;
        std::filesystem::create_directories(path.parent_path(), failed);

        std::ofstream file(path, std::ios::trunc);
        if (!file)
        {
            return;
        }

        file << "saved " << Clock::to_time_t(Clock::now()) << "\n";

        for (const aap::Component component : kComponents)
        {
            const auto &state = battery.Get(component);
            if (!state.known)
            {
                continue;
            }

            file << aap::Battery::Name(component) << " " << static_cast<int>(state.level) << " "
                 << static_cast<int>(state.status) << "\n";
        }
    }

    // Returns the age of what was loaded, or nothing when there was none.
    std::optional<std::chrono::seconds> Load(aap::Battery &battery) const
    {
        std::ifstream file(Path());
        if (!file)
        {
            return std::nullopt;
        }

        std::string key;
        std::optional<std::chrono::seconds> age;

        while (file >> key)
        {
            if (key == "saved")
            {
                long long stamp = 0;
                file >> stamp;
                age = std::chrono::duration_cast<std::chrono::seconds>(
                    Clock::now() - Clock::from_time_t(static_cast<time_t>(stamp))
                );
                continue;
            }

            int level = 0;
            int status = 0;
            file >> level >> status;

            const auto component = FromName(key);
            if (component)
            {
                battery.Restore(
                    *component, static_cast<uint8_t>(level), static_cast<aap::ChargeStatus>(status)
                );
            }
        }

        return age;
    }

private:
    static constexpr std::array<aap::Component, 4> kComponents = {
        aap::Component::Headset, aap::Component::Left, aap::Component::Right, aap::Component::Case
    };

    static std::optional<aap::Component> FromName(const std::string &name)
    {
        for (const aap::Component component : kComponents)
        {
            if (aap::Battery::Name(component) == name)
            {
                return component;
            }
        }

        return std::nullopt;
    }

    // State, not configuration: this is a cache the program rewrites, not
    // something anyone edits.
    static std::filesystem::path Path() { return StoragePath(StorageKind::State) / "last-battery"; }
};

} // namespace core
