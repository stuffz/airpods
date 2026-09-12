#include "aap/proximity.hpp"
#include "data/captures.hpp"

#include <QObject>
#include <QTest>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <utility>
#include <vector>

namespace
{
std::vector<uint8_t>
Changed(std::span<const uint8_t> advert, std::initializer_list<std::pair<size_t, uint8_t>> edits);

constexpr size_t kModeOffset = 2;
constexpr size_t kModelLowOffset = 4;
constexpr size_t kStatusOffset = 5;
constexpr size_t kPodsOffset = 6;
constexpr size_t kCaseOffset = 7;
constexpr size_t kLidOffset = 8;

constexpr uint8_t kPrimaryLeftBit = 0x20;
constexpr uint8_t kThisPodInCaseBit = 0x40;

// The cleartext half of the pods' 25-byte advertisement.
class TestProximity : public QObject
{
    Q_OBJECT

private slots:
    void namesKnownModels();
    void readsPodsInCase();
    void reportsAbsentPodAsUnknown();
    void mirrorsFieldsWhenRightIsPrimary();
    void trustsLidOnlyFromInsideTheCase();
    void mapsInEarToTheAdvertisingPod();
    void rejectsPairingMode();
    void rejectsOtherAppleHardware();
    void rejectsShortPayload();
};
} // namespace

// An unknown id whose suffix is 0x20 is still a pair of AirPods, just unnamed.
void TestProximity::namesKnownModels()
{
    QCOMPARE(aap::ModelName(0x1420), "AirPods Pro 2");
    QCOMPARE(aap::ModelName(0x0a20), "AirPods Max");
    QCOMPARE(aap::ModelName(0x2720), "AirPods");
}

void TestProximity::readsPodsInCase()
{
    const auto advert =
        captures::PodsAdvert(captures::kPodsCleartextBothInCase, captures::kPodsBothInCase);

    const auto report = aap::ParseProximity(advert);
    QVERIFY(report.has_value());
    QCOMPARE(static_cast<int>(report->model), 0x2720);

    // The block behind this advert holds 94/93/61; the nibbles carry 10% steps.
    QCOMPARE(static_cast<int>(report->leftLevel), 90);
    QCOMPARE(static_cast<int>(report->rightLevel), 90);
    QCOMPARE(static_cast<int>(report->caseLevel), 60);
    QVERIFY(report->leftCharging);
    QVERIFY(report->rightCharging);
    QVERIFY(!report->caseCharging);

    QVERIFY(report->primaryLeft);
    QVERIFY(report->bothPodsInCase);
    QVERIFY(!report->onePodInCase);
    QVERIFY(!report->leftInEar);
    QVERIFY(!report->rightInEar);

    QVERIFY(report->lid.has_value());
    QCOMPARE(*report->lid, aap::LidState::Closed);
    QCOMPARE(static_cast<int>(report->lidCounter), 0);
}

// Nibble 15 is the absent marker and must not decode as 150%.
void TestProximity::reportsAbsentPodAsUnknown()
{
    const auto report = aap::ParseProximity(captures::kPodsCleartextRightOut);
    QVERIFY(report.has_value());
    QCOMPARE(static_cast<int>(report->leftLevel), 90);
    QCOMPARE(report->rightLevel, aap::kUnknownLevel);
    QCOMPARE(static_cast<int>(report->caseLevel), 60);
    QVERIFY(report->leftCharging);
    QVERIFY(!report->rightCharging);

    QVERIFY(report->onePodInCase);
    QVERIFY(!report->bothPodsInCase);
    QVERIFY(report->lid.has_value());
    QCOMPARE(*report->lid, aap::LidState::Open);
    QCOMPARE(static_cast<int>(report->lidCounter), 3);
}

// The same state advertised by the other pod: the nibbles swap and so do the
// charging bits, and the reading has to come out identical.
void TestProximity::mirrorsFieldsWhenRightIsPrimary()
{
    constexpr auto kRightPrimary =
        static_cast<uint8_t>(captures::kPodsCleartextRightOut[kStatusOffset] & ~kPrimaryLeftBit);
    constexpr uint8_t kSwappedPods = 0x9f;
    constexpr uint8_t kSwappedCharging = 0x26;

    const auto advert = Changed(
        captures::kPodsCleartextRightOut, {{kStatusOffset, kRightPrimary},
                                           {kPodsOffset, kSwappedPods},
                                           {kCaseOffset, kSwappedCharging}}
    );

    const auto report = aap::ParseProximity(advert);
    QVERIFY(report.has_value());
    QVERIFY(!report->primaryLeft);
    QCOMPARE(static_cast<int>(report->leftLevel), 90);
    QCOMPARE(report->rightLevel, aap::kUnknownLevel);
    QVERIFY(report->leftCharging);
    QVERIFY(!report->rightCharging);
}

// A pod out of the case cannot see the lid, so its bit means nothing there.
void TestProximity::trustsLidOnlyFromInsideTheCase()
{
    constexpr auto kPodOutOfCase = static_cast<uint8_t>(
        captures::kPodsCleartextBothInCase[kStatusOffset] & ~kThisPodInCaseBit
    );
    constexpr uint8_t kShutAfterThreeOpens = 0x0b;

    const auto advert = Changed(
        captures::kPodsCleartextBothInCase,
        {{kStatusOffset, kPodOutOfCase}, {kLidOffset, kShutAfterThreeOpens}}
    );

    const auto report = aap::ParseProximity(advert);
    QVERIFY(report.has_value());
    QVERIFY(!report->lid.has_value());
    QCOMPARE(static_cast<int>(report->lidCounter), 3);
}

// Which in-ear bit belongs to which pod depends on the advertiser, so this is
// the left pod out of the case, primary, and worn.
void TestProximity::mapsInEarToTheAdvertisingPod()
{
    constexpr uint8_t kWornAndPrimary = 0x22;

    const auto advert =
        Changed(captures::kPodsCleartextBothInCase, {{kStatusOffset, kWornAndPrimary}});

    const auto report = aap::ParseProximity(advert);
    QVERIFY(report.has_value());
    QVERIFY(report->leftInEar);
    QVERIFY(!report->rightInEar);
}

// Pairing mode lays its fields out differently; reading it here invents values.
void TestProximity::rejectsPairingMode()
{
    const auto advert = Changed(captures::kPodsCleartextBothInCase, {{kModeOffset, 0x00}});

    QVERIFY(!aap::ParseProximity(advert).has_value());
}

// Other Apple devices broadcast type 07 with a payload that rotates every
// advertisement, and without the model suffix it decodes into plausible levels.
void TestProximity::rejectsOtherAppleHardware()
{
    const auto advert = Changed(captures::kPodsCleartextBothInCase, {{kModelLowOffset, 0x15}});

    QVERIFY(!aap::ParseProximity(advert).has_value());
}

void TestProximity::rejectsShortPayload()
{
    const std::span<const uint8_t> truncated =
        std::span(captures::kPodsCleartextBothInCase).first(10);

    QVERIFY(!aap::ParseProximity(truncated).has_value());
}

namespace
{
std::vector<uint8_t>
Changed(std::span<const uint8_t> advert, std::initializer_list<std::pair<size_t, uint8_t>> edits)
{
    std::vector<uint8_t> copy(advert.begin(), advert.end());

    for (const auto &[offset, value] : edits)
    {
        copy[offset] = value;
    }

    return copy;
}
} // namespace

QTEST_APPLESS_MAIN(TestProximity)

#include "tst_proximity.moc"
