#include "aap/encrypted_payload.hpp"
#include "bt/aes.hpp"
#include "core/key_store.hpp"
#include "core/options.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void Require(bool value, const char *message);
void TestCrypto();
void TestKeyFile(const std::filesystem::path &path);
} // namespace

int main(int argc, char **argv)
{
    try
    {
        Require(argc == 2, "Pass a temporary fixture path");
        TestCrypto();
        TestKeyFile(argv[1]);
        std::filesystem::remove(argv[1]);
        std::cout << "AES vector, battery decoding and key import passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

namespace
{
void TestCrypto()
{
    // FIPS 197, Appendix C.1.
    constexpr bt::AesBlock key = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    constexpr bt::AesBlock plain = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    constexpr bt::AesBlock cipher = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                     0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};
    Require(bt::AesDecryptBlock(key, cipher) == plain, "AES decryption differs from FIPS vector");
    Require(bt::AesEncryptBlock(key, plain) == cipher, "AES encryption differs from FIPS vector");
    const auto charging = aap::DecodeLevel(0xbd);
    Require(charging.charging && charging.level == 61, "Charging battery decoding failed");
    const auto absent = aap::DecodeLevel(0xff);
    Require(
        !absent.charging && absent.level == aap::kLevelAbsent, "Absent battery decoding failed"
    );
}

void TestKeyFile(const std::filesystem::path &path)
{
    Require(!std::filesystem::exists(path), "Fixture path must not exist");
    {
        std::ofstream file(path, std::ios::binary);
        file << "IRK 000102030405060708090a0b0c0d0e0f\r\n"
             << "ENC_KEY 0f0e0d0c0b0a09080706050403020100\r\n";
    }
    core::KeyStore keys;
    Require(keys.Load(path), "CRLF key file did not load");
    Require(keys.Irk().size() == 16 && keys.Irk()[15] == 15, "IRK bytes changed on import");
    Require(
        keys.Encryption().size() == 16 && keys.Encryption()[0] == 15,
        "Encryption key bytes changed on import"
    );
    {
        std::ofstream file(path);
        file << "IRK 0g0102030405060708090a0b0c0d0e0f\n"
             << "ENC_KEY 0f0e0d0c0b0a09080706050403020100\n";
    }
    bool rejected = false;
    try
    {
        keys.Load(path);
    }
    catch (const std::runtime_error &)
    {
        rejected = true;
    }
    Require(rejected, "Invalid imported key was accepted");
}

void Require(bool value, const char *message)
{
    if (!value)
    {
        throw std::runtime_error(message);
    }
}
} // namespace
