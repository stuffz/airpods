#pragma once

// Loads the proximity keys written by --keys. The file holds key material, so
// nothing here logs or prints it; only whether it was found.

#include "core/logger.hpp"
#include "core/storage_paths.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace core
{

class KeyStore
{
public:
    bool Load(const std::filesystem::path &selected = {})
    {
        const std::filesystem::path path = selected.empty() ? Path() : selected;

        std::ifstream file(path);
        if (!file)
        {
            if (!selected.empty())
            {
                throw std::runtime_error("Cannot open the requested proximity key file");
            }
            return false;
        }

        std::string name;
        std::string hex;

        irk.clear();
        encryption.clear();
        while (file >> name)
        {
            if (!(file >> hex))
            {
                throw std::runtime_error("Incomplete proximity key entry");
            }
            if (name == "IRK")
            {
                irk = FromHex(hex);
            }
            else if (name == "ENC_KEY")
            {
                encryption = FromHex(hex);
            }
        }

        if (!file.eof() || !HasIrk() || !HasEncryption())
        {
            throw std::runtime_error("Proximity key file must contain a 16-byte IRK and ENC_KEY");
        }
        LOG_INFO("Loaded proximity keys");
        return HasIrk() || HasEncryption();
    }

    bool HasIrk() const { return !irk.empty(); }

    bool HasEncryption() const { return !encryption.empty(); }

    std::span<const uint8_t> Irk() const { return irk; }

    std::span<const uint8_t> Encryption() const { return encryption; }

    static std::filesystem::path Path()
    {
        return StoragePath(StorageKind::Config) / "proximity-keys";
    }

private:
    static std::vector<uint8_t> FromHex(const std::string &text)
    {
        constexpr int kHexBase = 16;

        std::vector<uint8_t> bytes;
        constexpr size_t kKeyHexLength = 32;
        if (text.size() != kKeyHexLength)
        {
            throw std::runtime_error("Proximity keys must contain 32 hexadecimal digits");
        }

        bytes.reserve(text.size() / 2);

        for (size_t i = 0; i < text.size(); i += 2)
        {
            uint8_t byte = 0;
            const char *begin = text.data() + i;
            const auto [end, error] = std::from_chars(begin, begin + 2, byte, kHexBase);
            if (error != std::errc{} || end != begin + 2)
            {
                throw std::runtime_error("Invalid hexadecimal proximity key");
            }
            bytes.push_back(byte);
        }

        return bytes;
    }

    std::vector<uint8_t> irk;
    std::vector<uint8_t> encryption;
};

} // namespace core
