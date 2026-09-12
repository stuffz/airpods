#include "core/key_store.hpp"

#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string_view>

namespace
{
void WriteFile(const std::filesystem::path &path, std::string_view text);

constexpr std::string_view kValidPair = "IRK 000102030405060708090a0b0c0d0e0f\n"
                                        "ENC_KEY 0f0e0d0c0b0a09080706050403020100\n";

// The proximity key file, as --keys writes it and Windows imports it.
class TestKeyStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void loadsKeysWrittenWithCrlf();
    void keepsUnknownEntries();
    void rejectsInvalidHexAndForgetsTheKeys();
    void rejectsShortKey();
    void rejectsFileWithoutBothKeys();
    void rejectsEntryWithoutAValue();
    void rejectsMissingSelectedFile();

private:
    QTemporaryDir directory;
    std::filesystem::path path;
};
} // namespace

void TestKeyStore::init()
{
    QVERIFY(directory.isValid());
    path = std::filesystem::path(directory.path().toStdString()) / "proximity-keys";
    std::filesystem::remove(path);
}

// The file travels to Windows and comes back, so line endings are not fixed.
void TestKeyStore::loadsKeysWrittenWithCrlf()
{
    WriteFile(
        path, "IRK 000102030405060708090a0b0c0d0e0f\r\n"
              "ENC_KEY 0f0e0d0c0b0a09080706050403020100\r\n"
    );

    core::KeyStore keys;
    QVERIFY(keys.Load(path));
    QVERIFY(keys.HasIrk());
    QVERIFY(keys.HasEncryption());
    QCOMPARE(keys.Irk().size(), size_t{16});
    QCOMPARE(static_cast<int>(keys.Irk()[15]), 15);
    QCOMPARE(keys.Encryption().size(), size_t{16});
    QCOMPARE(static_cast<int>(keys.Encryption()[0]), 15);
}

void TestKeyStore::keepsUnknownEntries()
{
    WriteFile(
        path, "COLOUR 00\n"
              "IRK 000102030405060708090a0b0c0d0e0f\n"
              "ENC_KEY 0f0e0d0c0b0a09080706050403020100\n"
    );

    core::KeyStore keys;
    QVERIFY(keys.Load(path));
    QCOMPARE(keys.Irk().size(), size_t{16});
}

// A rejected file must not leave half a key behind for the scanner to use.
void TestKeyStore::rejectsInvalidHexAndForgetsTheKeys()
{
    WriteFile(path, kValidPair);

    core::KeyStore keys;
    QVERIFY(keys.Load(path));

    WriteFile(
        path, "IRK 0g0102030405060708090a0b0c0d0e0f\n"
              "ENC_KEY 0f0e0d0c0b0a09080706050403020100\n"
    );

    QVERIFY_THROWS_EXCEPTION(std::runtime_error, keys.Load(path));
    QVERIFY(!keys.HasIrk());
    QVERIFY(!keys.HasEncryption());
}

void TestKeyStore::rejectsShortKey()
{
    WriteFile(
        path, "IRK 0102030405060708090a0b0c0d0e0f\n"
              "ENC_KEY 0f0e0d0c0b0a09080706050403020100\n"
    );

    core::KeyStore keys;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, keys.Load(path));
}

// The BLE paths need both: the IRK to recognise the pods, the other to decrypt.
void TestKeyStore::rejectsFileWithoutBothKeys()
{
    WriteFile(path, "IRK 000102030405060708090a0b0c0d0e0f\n");

    core::KeyStore keys;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, keys.Load(path));
}

void TestKeyStore::rejectsEntryWithoutAValue()
{
    WriteFile(path, "IRK 000102030405060708090a0b0c0d0e0f\nENC_KEY\n");

    core::KeyStore keys;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, keys.Load(path));
}

// An explicitly requested file that is not there is an error; a missing default
// one is not, and that path is left to the storage-directory tests.
void TestKeyStore::rejectsMissingSelectedFile()
{
    core::KeyStore keys;
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, keys.Load(path));
}

namespace
{
void WriteFile(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary);
    file << text;
}
} // namespace

QTEST_APPLESS_MAIN(TestKeyStore)

#include "tst_key_store.moc"
