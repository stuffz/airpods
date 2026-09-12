#include "aap/encrypted_payload.hpp"
#include "bt/aes.hpp"
#include "data/captures.hpp"

#include <QObject>
#include <QTest>

#include <array>
#include <cstdint>

namespace
{
// The encrypted tail of a proximity advertisement, and the AES underneath it.
class TestEncrypted : public QObject
{
    Q_OBJECT

private slots:
    void matchesFipsVector();
    void decodesLevelByte();
    void readsRecordedBlock();
    void followsTheAdvertisingPod();
    void reportsAbsentCase();
    void rejectsShortAdvertisement();
    void rejectsWrongSizedKey();
};
} // namespace

// FIPS 197, Appendix C.1.
void TestEncrypted::matchesFipsVector()
{
    constexpr bt::AesBlock key = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
    constexpr bt::AesBlock plain = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    constexpr bt::AesBlock cipher = {0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                                     0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a};

    QCOMPARE(bt::AesEncryptBlock(key, plain), cipher);
    QCOMPARE(bt::AesDecryptBlock(key, cipher), plain);
}

// The whole byte is absent before any bit of it is a charging flag.
void TestEncrypted::decodesLevelByte()
{
    const auto charging = aap::DecodeLevel(0xbd);
    QVERIFY(charging.charging);
    QCOMPARE(static_cast<int>(charging.level), 61);

    const auto discharging = aap::DecodeLevel(0x3d);
    QVERIFY(!discharging.charging);
    QCOMPARE(static_cast<int>(discharging.level), 61);

    const auto absent = aap::DecodeLevel(aap::kByteAbsent);
    QVERIFY(!absent.charging);
    QCOMPARE(absent.level, aap::kLevelAbsent);
}

void TestEncrypted::readsRecordedBlock()
{
    const auto advert =
        captures::PodsAdvert(captures::kPodsCleartextBothInCase, captures::kPodsBothInCase);

    const auto report = aap::ParseEncryptedBattery(advert, captures::kTestKey, true);
    QVERIFY(report.has_value());
    QCOMPARE(static_cast<int>(report->leftLevel), 94);
    QCOMPARE(static_cast<int>(report->rightLevel), 93);
    QCOMPARE(static_cast<int>(report->caseLevel), 61);
    QVERIFY(report->leftCharging);
    QVERIFY(report->rightCharging);
    QVERIFY(report->caseCharging);
}

// The other pod broadcast the same instant with its own levels first. Reading
// the primary flag is what keeps 94 on the left in both.
void TestEncrypted::followsTheAdvertisingPod()
{
    const auto advert =
        captures::PodsAdvert(captures::kPodsCleartextBothInCase, captures::kPodsBothInCaseSwapped);

    const auto report = aap::ParseEncryptedBattery(advert, captures::kTestKey, false);
    QVERIFY(report.has_value());
    QCOMPARE(static_cast<int>(report->leftLevel), 94);
    QCOMPARE(static_cast<int>(report->rightLevel), 93);
}

// A pod away from the case loses contact with it and reports it absent.
void TestEncrypted::reportsAbsentCase()
{
    const auto advert = captures::PodsAdvert(captures::kPodsCleartextRightOut, captures::kPodsOut);

    const auto report = aap::ParseEncryptedBattery(advert, captures::kTestKey, true);
    QVERIFY(report.has_value());
    QCOMPARE(static_cast<int>(report->leftLevel), 94);
    QCOMPARE(static_cast<int>(report->rightLevel), 93);
    QCOMPARE(report->caseLevel, aap::kLevelAbsent);
    QVERIFY(!report->leftCharging);
    QVERIFY(!report->rightCharging);
    QVERIFY(!report->caseCharging);
}

void TestEncrypted::rejectsShortAdvertisement()
{
    const std::array<uint8_t, bt::kAesBlockSize - 1> tooShort{};

    QVERIFY(!aap::ParseEncryptedBattery(tooShort, captures::kTestKey, true).has_value());
}

void TestEncrypted::rejectsWrongSizedKey()
{
    const auto advert =
        captures::PodsAdvert(captures::kPodsCleartextBothInCase, captures::kPodsBothInCase);
    const std::array<uint8_t, bt::kAesBlockSize - 1> shortKey{};

    QVERIFY(!aap::ParseEncryptedBattery(advert, shortKey, true).has_value());
}

QTEST_APPLESS_MAIN(TestEncrypted)

#include "tst_encrypted.moc"
