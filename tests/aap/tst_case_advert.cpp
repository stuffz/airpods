#include "aap/case_advert.hpp"
#include "aap/encrypted_payload.hpp"
#include "aap/proximity.hpp"
#include "bt/aes.hpp"
#include "data/captures.hpp"

#include <QObject>
#include <QTest>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

namespace
{
bt::AesBlock Changed(bt::AesBlock block, size_t offset, uint8_t value);
bool Mentions(const std::string &notes, const std::string &wanted);

constexpr size_t kFlagOffset = 2;
constexpr size_t kCaseLevelOffset = 3;
constexpr size_t kFixedOffset = 6;
constexpr size_t kCounterOffset = 12;

// The case's own advertisement, which has no cleartext to identify it: the
// block has to decrypt into the recorded layout before it counts as ours.
class TestCaseAdvert : public QObject
{
    Q_OBJECT

private slots:
    void identifiesShapeByLength();
    void readsRecordedBlock();
    void chargerMovesTheCaseByte();
    void absentPodsReadAbsent();
    void isolatesTheRightPod();
    void tracksLidAndOpenCount();
    void rejectsForeignBlock();
    void acceptsCarriedCounter();
    void reportsNoAnomaliesForRecordedAdvert();
    void reportsUnexpectedHeaderFlagAndLevel();
};
} // namespace

void TestCaseAdvert::identifiesShapeByLength()
{
    QVERIFY(aap::IsCaseAdvert(captures::CaseAdvert(captures::kCaseOnTable)));

    const auto pods =
        captures::PodsAdvert(captures::kPodsCleartextBothInCase, captures::kPodsBothInCase);
    QVERIFY(!aap::IsCaseAdvert(pods));

    auto truncated = captures::CaseAdvert(captures::kCaseOnTable);
    truncated.pop_back();
    QVERIFY(!aap::IsCaseAdvert(truncated));
}

void TestCaseAdvert::readsRecordedBlock()
{
    const auto advert = captures::CaseAdvert(captures::kCaseOnTable);

    const auto report = aap::ParseCaseAdvert(advert, captures::kTestKey);
    QVERIFY(report.has_value());
    QCOMPARE(static_cast<int>(report->battery.leftLevel), 94);
    QCOMPARE(static_cast<int>(report->battery.rightLevel), 93);
    QCOMPARE(static_cast<int>(report->battery.caseLevel), 61);
    QVERIFY(report->battery.leftCharging);
    QVERIFY(report->battery.rightCharging);
    QVERIFY(!report->battery.caseCharging);
    QCOMPARE(report->header, aap::kCaseKnownHeader);
    QCOMPARE(report->flag, aap::kCaseAdvertFlag);
}

// Putting the case on a charger was the only change between these two.
void TestCaseAdvert::chargerMovesTheCaseByte()
{
    const auto report =
        aap::ParseCaseAdvert(captures::CaseAdvert(captures::kCaseOnCharger), captures::kTestKey);
    QVERIFY(report.has_value());
    QCOMPARE(static_cast<int>(report->battery.caseLevel), 61);
    QVERIFY(report->battery.caseCharging);
    QCOMPARE(static_cast<int>(report->battery.leftLevel), 94);
    QCOMPARE(static_cast<int>(report->battery.rightLevel), 93);
}

void TestCaseAdvert::absentPodsReadAbsent()
{
    const auto report =
        aap::ParseCaseAdvert(captures::CaseAdvert(captures::kCasePodsOut), captures::kTestKey);
    QVERIFY(report.has_value());
    QCOMPARE(report->battery.leftLevel, aap::kLevelAbsent);
    QCOMPARE(report->battery.rightLevel, aap::kLevelAbsent);
    QVERIFY(!report->battery.leftCharging);
    QVERIFY(!report->battery.rightCharging);
    QCOMPARE(static_cast<int>(report->battery.caseLevel), 61);
}

// Byte 5 alone going absent is what established left/right order.
void TestCaseAdvert::isolatesTheRightPod()
{
    const auto report = aap::ParseCaseAdvert(
        captures::CaseAdvert(captures::kCaseLidOpenRightOut), captures::kTestKey
    );
    QVERIFY(report.has_value());
    QCOMPARE(static_cast<int>(report->battery.leftLevel), 94);
    QCOMPARE(report->battery.rightLevel, aap::kLevelAbsent);
}

// Three consecutive adverts from the run that opened and shut the lid.
void TestCaseAdvert::tracksLidAndOpenCount()
{
    const auto shut = aap::ParseCaseAdvert(
        captures::CaseAdvert(captures::kCaseLidShutBothIn), captures::kTestKey
    );
    QVERIFY(shut.has_value());
    QCOMPARE(shut->lid, aap::LidState::Closed);
    QCOMPARE(static_cast<int>(shut->lidCounter), 2);

    const auto opened = aap::ParseCaseAdvert(
        captures::CaseAdvert(captures::kCaseLidOpenBothIn), captures::kTestKey
    );
    QVERIFY(opened.has_value());
    QCOMPARE(opened->lid, aap::LidState::Open);
    QCOMPARE(static_cast<int>(opened->lidCounter), 3);

    const auto shutAgain = aap::ParseCaseAdvert(
        captures::CaseAdvert(captures::kCaseLidShutRightOut), captures::kTestKey
    );
    QVERIFY(shutAgain.has_value());
    QCOMPARE(shutAgain->lid, aap::LidState::Closed);
    QCOMPARE(static_cast<int>(shutAgain->lidCounter), 3);
}

// Another pair's advert decrypts to noise under our key, and noise does not
// have 51 0b sitting at offset 6.
void TestCaseAdvert::rejectsForeignBlock()
{
    const auto advert = captures::CaseAdvert(Changed(captures::kCaseOnTable, kFixedOffset, 0x52));

    QVERIFY(!aap::ParseCaseAdvert(advert, captures::kTestKey).has_value());
}

// The seconds counter carried overnight and a fixed-byte check that covered it
// started rejecting valid adverts. Bytes 12-15 must stay out of that check.
void TestCaseAdvert::acceptsCarriedCounter()
{
    bt::AesBlock later = captures::kCaseOnTable;
    std::copy(
        captures::kCaseCounterLater.begin(), captures::kCaseCounterLater.end(),
        later.begin() + kCounterOffset
    );

    const auto report = aap::ParseCaseAdvert(captures::CaseAdvert(later), captures::kTestKey);
    QVERIFY(report.has_value());
    QCOMPARE(static_cast<int>(report->battery.caseLevel), 61);
}

void TestCaseAdvert::reportsNoAnomaliesForRecordedAdvert()
{
    const auto report =
        aap::ParseCaseAdvert(captures::CaseAdvert(captures::kCaseOnTable), captures::kTestKey);
    QVERIFY(report.has_value());
    QVERIFY(aap::CaseAdvertAnomalies(*report).empty());
}

// Each of these still parses: they are warnings about a layout that moved, not
// grounds for dropping the reading.
void TestCaseAdvert::reportsUnexpectedHeaderFlagAndLevel()
{
    const auto header = aap::ParseCaseAdvert(
        captures::CaseAdvert(Changed(captures::kCaseOnTable, 0, 0x28)), captures::kTestKey
    );
    QVERIFY(header.has_value());
    QVERIFY(Mentions(aap::CaseAdvertAnomalies(*header), "header"));

    auto flagged = captures::CaseAdvert(captures::kCaseOnTable);
    flagged[kFlagOffset] = 0x07;
    const auto flag = aap::ParseCaseAdvert(flagged, captures::kTestKey);
    QVERIFY(flag.has_value());
    QVERIFY(Mentions(aap::CaseAdvertAnomalies(*flag), "flag"));

    const auto level = aap::ParseCaseAdvert(
        captures::CaseAdvert(Changed(captures::kCaseOnTable, kCaseLevelOffset, 0x65)),
        captures::kTestKey
    );
    QVERIFY(level.has_value());
    QVERIFY(Mentions(aap::CaseAdvertAnomalies(*level), "level above 100"));
}

namespace
{
bt::AesBlock Changed(bt::AesBlock block, size_t offset, uint8_t value)
{
    block[offset] = value;
    return block;
}

bool Mentions(const std::string &notes, const std::string &wanted)
{
    return notes.find(wanted) != std::string::npos;
}
} // namespace

QTEST_APPLESS_MAIN(TestCaseAdvert)

#include "tst_case_advert.moc"
