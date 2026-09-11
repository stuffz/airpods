#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#include <QStandardPaths>
#endif

namespace core
{

enum class StorageKind
{
    Config,
    State
};

inline std::filesystem::path StoragePath(StorageKind kind)
{
#ifdef _WIN32
    const auto base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
    {
        throw std::runtime_error("Windows application data directory is unavailable");
    }
    static_cast<void>(kind);
    return std::filesystem::path(base.toStdWString()) / "airpods";
#else
    const char *configured =
        std::getenv(kind == StorageKind::Config ? "XDG_CONFIG_HOME" : "XDG_STATE_HOME");
    if (configured != nullptr && configured[0] != '\0')
    {
        return std::filesystem::path(configured) / "airpods";
    }
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0')
    {
        throw std::runtime_error("HOME is required when the XDG storage directory is unset");
    }
    return std::filesystem::path(home) /
           (kind == StorageKind::Config ? ".config" : ".local/state") / "airpods";
#endif
}

} // namespace core
