#include "bt/rpa.hpp"

#include <QObject>
#include <QTest>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace
{
// Bluetooth Core Specification sample data for the address hash ah().
//
//   IRK    ec0234a3 57c8ad05 341010a6 0a397d9b
//   prand  00000000 00000000 00000000 00708194
//   ah     0dfbaa
//
// The specification writes its octet strings least significant byte first, so
// the key is stored here reversed and the address reads prand then hash:
// 70:81:94 is the random part and 0D:FB:AA the hash of it under this key.
constexpr std::array<uint8_t, 16> kIrk = {0x9b, 0x7d, 0x39, 0x0a, 0xa6, 0x10, 0x10, 0x34,
                                          0x05, 0xad, 0xc8, 0x57, 0xa3, 0x34, 0x02, 0xec};

constexpr std::string_view kResolvable = "70:81:94:0D:FB:AA";

// Address resolution, which is the only thing that tells our pods' rotating
// address apart from another pair's.
class TestRpa : public QObject
{
    Q_OBJECT

private slots:
    void resolvesSpecificationVector();
    void acceptsLowercaseAddress();
    void rejectsAlteredHash();
    void rejectsRandomStaticAddress();
    void rejectsMalformedAddress();
    void rejectsWrongSizedKey();
};
} // namespace

void TestRpa::resolvesSpecificationVector()
{
    QVERIFY(bt::AddressBelongsTo(std::string(kResolvable), kIrk));
}

// BlueZ prints addresses uppercase, but nothing in the parser depends on it.
void TestRpa::acceptsLowercaseAddress()
{
    QVERIFY(bt::AddressBelongsTo("70:81:94:0d:fb:aa", kIrk));
}

// One bit of the hash is enough: another pair's advertisement is rejected even
// when it carries the same model and layout.
void TestRpa::rejectsAlteredHash()
{
    QVERIFY(!bt::AddressBelongsTo("70:81:94:0D:FB:AB", kIrk));
}

// The case advertises from a random static address, which no IRK resolves. Its
// broadcasts are identified by decrypting them instead. This is the vector's
// own address with the top two bits set, which is what makes one static.
void TestRpa::rejectsRandomStaticAddress()
{
    QVERIFY(!bt::AddressBelongsTo("F0:81:94:0D:FB:AA", kIrk));
}

void TestRpa::rejectsMalformedAddress()
{
    QVERIFY(!bt::AddressBelongsTo("70:81:94:0D:FB", kIrk));
    QVERIFY(!bt::AddressBelongsTo("70-81-94-0D-FB-AA", kIrk));
    QVERIFY(!bt::AddressBelongsTo("70:81:94:0D:FB:0g", kIrk));
    QVERIFY(!bt::AddressBelongsTo("70:81:94:0D:FB:gg", kIrk));
    QVERIFY(!bt::AddressBelongsTo("", kIrk));
}

void TestRpa::rejectsWrongSizedKey()
{
    constexpr std::array<uint8_t, 15> kTooShort = {};

    QVERIFY(!bt::AddressBelongsTo(std::string(kResolvable), kTooShort));
}

QTEST_APPLESS_MAIN(TestRpa)

#include "tst_rpa.moc"
