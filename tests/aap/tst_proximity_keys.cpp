#include "aap/proximity_keys.hpp"

#include <QObject>
#include <QTest>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
// 04 00 04 00 31 00 | count | [type ?? length ??] key bytes | ...
//
// The key material is the published FIPS 197 block rather than anything the
// buds handed out: an IRK allows tracking across address rotations.
constexpr std::array<uint8_t, 47> kBothKeys = {0x04, 0x00, 0x04, 0x00, 0x31, 0x00, 0x02,       //
                                               0x01, 0x00, 0x10, 0x00,                         //
                                               0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, //
                                               0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, //
                                               0x04, 0x00, 0x10, 0x00,                         //
                                               0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, //
                                               0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};

// The reply to 04 00 04 00 30 00 05 00: a count, then one length-prefixed key
// each. Two of the four header bytes are not interpreted.
class TestProximityKeys : public QObject
{
    Q_OBJECT

private slots:
    void readsBothKeys();
    void namesTheKeyTypes();
    void ignoresAnotherOpcode();
    void stopsAtATruncatedEntry();
    void stopsWhenAKeyRunsPastTheEnd();
    void readsNothingFromAnEmptyReply();
};

void TestProximityKeys::readsBothKeys()
{
    const auto keys = aap::ParseProximityKeys(kBothKeys);

    QCOMPARE(keys.size(), size_t{2});
    QCOMPARE(keys[0].type, aap::KeyType::Irk);
    QCOMPARE(keys[0].material.size(), size_t{16});
    QCOMPARE(static_cast<int>(keys[0].material[15]), 15);
    QCOMPARE(keys[1].type, aap::KeyType::Encryption);
    QCOMPARE(keys[1].material.size(), size_t{16});
    QCOMPARE(static_cast<int>(keys[1].material[0]), 15);
}

// These are the names --keys writes into the key file.
void TestProximityKeys::namesTheKeyTypes()
{
    QCOMPARE(aap::KeyTypeName(aap::KeyType::Irk), "IRK");
    QCOMPARE(aap::KeyTypeName(aap::KeyType::Encryption), "ENC_KEY");
}

void TestProximityKeys::ignoresAnotherOpcode()
{
    std::vector<uint8_t> battery(kBothKeys.begin(), kBothKeys.end());
    constexpr size_t kOpcodeOffset = 4;
    battery[kOpcodeOffset] = 0x04;

    QVERIFY(aap::ParseProximityKeys(battery).empty());
}

// A reply cut short mid-header keeps whatever arrived whole before it.
void TestProximityKeys::stopsAtATruncatedEntry()
{
    constexpr size_t kSecondHeader = 29;
    const auto cut = std::span(kBothKeys).first(kSecondHeader);

    const auto keys = aap::ParseProximityKeys(cut);
    QCOMPARE(keys.size(), size_t{1});
    QCOMPARE(keys[0].type, aap::KeyType::Irk);
}

// A length that claims more than the packet holds is where reading stops, not
// where it reads past the end.
void TestProximityKeys::stopsWhenAKeyRunsPastTheEnd()
{
    std::vector<uint8_t> overlong(kBothKeys.begin(), kBothKeys.end());
    constexpr size_t kFirstLengthOffset = 9;
    overlong[kFirstLengthOffset] = 0xff;

    QVERIFY(aap::ParseProximityKeys(overlong).empty());
}

void TestProximityKeys::readsNothingFromAnEmptyReply()
{
    constexpr std::array<uint8_t, 6> kHeaderOnly = {0x04, 0x00, 0x04, 0x00, 0x31, 0x00};

    QVERIFY(aap::ParseProximityKeys(kHeaderOnly).empty());
}
} // namespace

QTEST_APPLESS_MAIN(TestProximityKeys)

#include "tst_proximity_keys.moc"
