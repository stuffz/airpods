#include "aap/battery.hpp"
#include "aap/encrypted_payload.hpp"
#include "aap/proximity.hpp"
#include "data/captures.hpp"

#include <QObject>
#include <QTest>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace
{
aap::Battery FromLink();
aap::Battery FromAir();

// 04 00 04 00 04 00 | count | [component 01 level status 01] ...
constexpr std::array<uint8_t, 12> kLeftDisconnected = {0x04, 0x00, 0x04, 0x00, 0x04, 0x00,
                                                       0x01, 0x04, 0x01, 0x00, 0x04, 0x01};

// Where a reading came from decides whether the next one may replace it.
class TestBattery : public QObject
{
    Q_OBJECT

private slots:
    void parsesLinkReport();
    void rejectsMalformedReport();
    void disconnectedKeepsTheLevel();
    void linkOutranksAirWhileItIsUp();
    void airTakesOverWhenTheLinkIsDown();
    void airTakesAReleasedComponent();
    void encryptedLevelsAreExact();
    void absentEncryptedLevelKeepsTheReading();
    void restoreMarksTheReadingStored();
};
} // namespace

void TestBattery::parsesLinkReport()
{
    aap::Battery battery;
    QVERIFY(!battery.HasReading());
    QVERIFY(battery.Parse(captures::kBatteryReport));
    QVERIFY(battery.HasReading());

    const auto &left = battery.Get(aap::Component::Left);
    QVERIFY(left.known);
    QCOMPARE(static_cast<int>(left.level), 94);
    QCOMPARE(left.status, aap::ChargeStatus::Discharging);
    QCOMPARE(left.source, aap::Source::Link);

    const auto &right = battery.Get(aap::Component::Right);
    QCOMPARE(static_cast<int>(right.level), 93);
    QCOMPARE(right.status, aap::ChargeStatus::Discharging);

    const auto &caseSlot = battery.Get(aap::Component::Case);
    QCOMPARE(static_cast<int>(caseSlot.level), 61);
    QCOMPARE(caseSlot.status, aap::ChargeStatus::Charging);

    // These buds report two pods and a case, never the Max's single component.
    QVERIFY(!battery.Get(aap::Component::Headset).known);
}

void TestBattery::rejectsMalformedReport()
{
    constexpr std::array<uint8_t, 27> kFourEntries = {0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x04,
                                                      0x02, 0x01, 0x5d, 0x02, 0x01, 0x04, 0x01,
                                                      0x5e, 0x02, 0x01, 0x08, 0x01, 0x3d, 0x01,
                                                      0x01, 0x01, 0x01, 0x5a, 0x02, 0x01};
    constexpr std::array<uint8_t, 12> kEarDetection = {0x04, 0x00, 0x04, 0x00, 0x06, 0x00,
                                                       0x01, 0x04, 0x01, 0x5e, 0x02, 0x01};

    // Heap allocated so that a read past the count byte is detectable rather
    // than quietly landing in whatever follows on the stack.
    constexpr size_t kPrefixSize = 6;
    const std::vector<uint8_t> headerOnly(
        captures::kBatteryReport.begin(), captures::kBatteryReport.begin() + kPrefixSize
    );

    std::vector<uint8_t> truncated(
        captures::kBatteryReport.begin(), captures::kBatteryReport.end()
    );
    truncated.pop_back();

    std::vector<uint8_t> unmarked(captures::kBatteryReport.begin(), captures::kBatteryReport.end());
    constexpr size_t kFirstEntryMarker = 8;
    unmarked[kFirstEntryMarker] = 0x00;

    aap::Battery battery;
    QVERIFY(!battery.Parse(headerOnly));
    QVERIFY(!battery.Parse(kFourEntries));
    QVERIFY(!battery.Parse(kEarDetection));
    QVERIFY(!battery.Parse(truncated));
    QVERIFY(!battery.Parse(unmarked));
    QVERIFY(!battery.HasReading());
}

// The buds send Disconnected for a pod sitting in the case. Overwriting the
// level there would blank the display, so only the priority is released.
void TestBattery::disconnectedKeepsTheLevel()
{
    aap::Battery battery = FromLink();
    QVERIFY(battery.Parse(kLeftDisconnected));

    const auto &left = battery.Get(aap::Component::Left);
    QCOMPARE(static_cast<int>(left.level), 94);
    QCOMPARE(left.status, aap::ChargeStatus::Discharging);
    QCOMPARE(left.source, aap::Source::Stored);
}

void TestBattery::linkOutranksAirWhileItIsUp()
{
    aap::Battery battery = FromLink();
    battery.Merge(FromAir(), aap::LinkState::Up);

    const auto &left = battery.Get(aap::Component::Left);
    QCOMPARE(static_cast<int>(left.level), 94);
    QCOMPARE(left.source, aap::Source::Link);
}

void TestBattery::airTakesOverWhenTheLinkIsDown()
{
    aap::Battery battery = FromLink();
    battery.Merge(FromAir(), aap::LinkState::Down);

    const auto &left = battery.Get(aap::Component::Left);
    QCOMPARE(static_cast<int>(left.level), 90);
    QCOMPARE(left.source, aap::Source::Air);
}

// A component the link has stopped vouching for is available again even while
// the rest of the link's readings stand.
void TestBattery::airTakesAReleasedComponent()
{
    aap::Battery battery = FromLink();
    QVERIFY(battery.Parse(kLeftDisconnected));
    battery.Merge(FromAir(), aap::LinkState::Up);

    const auto &left = battery.Get(aap::Component::Left);
    QCOMPARE(static_cast<int>(left.level), 90);
    QCOMPARE(left.source, aap::Source::Air);

    const auto &right = battery.Get(aap::Component::Right);
    QCOMPARE(static_cast<int>(right.level), 93);
    QCOMPARE(right.source, aap::Source::Link);
}

// Same transport as the nibbles, a byte per component instead.
void TestBattery::encryptedLevelsAreExact()
{
    const auto advert =
        captures::PodsAdvert(captures::kPodsCleartextBothInCase, captures::kPodsBothInCase);
    const auto report = aap::ParseEncryptedBattery(advert, captures::kTestKey, true);
    QVERIFY(report.has_value());

    aap::Battery battery;
    battery.ApplyEncrypted(*report);

    const auto &left = battery.Get(aap::Component::Left);
    QCOMPARE(static_cast<int>(left.level), 94);
    QCOMPARE(left.status, aap::ChargeStatus::Charging);
    QCOMPARE(left.source, aap::Source::Air);
}

// Both pods out of the case put the case out of contact. An absent field must
// preserve what was last known rather than blank it.
void TestBattery::absentEncryptedLevelKeepsTheReading()
{
    const auto advert = captures::PodsAdvert(captures::kPodsCleartextRightOut, captures::kPodsOut);
    const auto report = aap::ParseEncryptedBattery(advert, captures::kTestKey, true);
    QVERIFY(report.has_value());

    aap::Battery battery;
    battery.Restore(aap::Component::Case, 61, aap::ChargeStatus::Charging);
    battery.ApplyEncrypted(*report);

    const auto &caseSlot = battery.Get(aap::Component::Case);
    QCOMPARE(static_cast<int>(caseSlot.level), 61);
    QCOMPARE(caseSlot.status, aap::ChargeStatus::Charging);
    QCOMPARE(caseSlot.source, aap::Source::Stored);
}

void TestBattery::restoreMarksTheReadingStored()
{
    aap::Battery battery;
    battery.Restore(aap::Component::Left, 94, aap::ChargeStatus::Discharging);

    const auto &left = battery.Get(aap::Component::Left);
    QVERIFY(left.known);
    QCOMPARE(static_cast<int>(left.level), 94);
    QCOMPARE(left.source, aap::Source::Stored);
    QVERIFY(battery.HasReading());
}

namespace
{
aap::Battery FromLink()
{
    aap::Battery battery;
    battery.Parse(captures::kBatteryReport);
    return battery;
}

aap::Battery FromAir()
{
    const auto report = aap::ParseProximity(captures::kPodsCleartextBothInCase);

    aap::Battery battery;
    battery.ApplyProximity(*report);
    return battery;
}
} // namespace

QTEST_APPLESS_MAIN(TestBattery)

#include "tst_battery.moc"
