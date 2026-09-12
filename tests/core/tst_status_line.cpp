#include "core/status_line.hpp"

#include <QObject>
#include <QTest>

#include <chrono>
#include <string>

namespace
{
using namespace std::chrono_literals;

constexpr core::RadioHealth kHealthy = {true, true, true, true};

core::StatusInputs Healthy();

// The one line the window keeps: which fault wins, and how a reading ages.
class TestStatusLine : public QObject
{
    Q_OBJECT

private slots:
    void reportsNothingWhileEverythingWorks();
    void ordersFaultsByHowActionableTheyAre();
    void faultOutranksTheAge();
    void reportsAConnectedButSilentLink();
    void reportsNoPodsInRangeAfterASilence();
    void agesAStoredReadingByThisRunsTime();
    void keepsTheStoredAgeWhileTheLinkIsDown();
    void spellsOutTheAge();
};

void TestStatusLine::reportsNothingWhileEverythingWorks()
{
    QCOMPARE(core::StatusLine(Healthy()), std::string());
}

// A scanner that is not running hides everything underneath it.
void TestStatusLine::ordersFaultsByHowActionableTheyAre()
{
    core::StatusInputs state = Healthy();

    state.radio = {false, false, false, false};
    QCOMPARE(core::StatusLine(state), std::string("Error: no Bluetooth adapter"));

    state.radio.hasAdapter = true;
    QCOMPARE(core::StatusLine(state), std::string("Error: Bluetooth is off"));

    state.radio.powered = true;
    QCOMPARE(core::StatusLine(state), std::string("Error: Bluetooth scanning is not running"));

    state.radio.scanning = true;
    QCOMPARE(core::StatusLine(state), std::string("Error: no proximity keys, run --keys"));

    state.radio.hasKeys = true;
    QCOMPARE(core::StatusLine(state), std::string());
}

// How old a reading is only matters while everything that could refresh it
// works, so the fault takes the line even with a reading to show.
void TestStatusLine::faultOutranksTheAge()
{
    core::StatusInputs state = Healthy();
    state.radio.powered = false;
    state.storedAge = 5min;

    QCOMPARE(core::StatusLine(state), std::string("Error: Bluetooth is off"));
}

// The link is up and reporting nothing, which no amount of waiting fixes.
void TestStatusLine::reportsAConnectedButSilentLink()
{
    core::StatusInputs state = Healthy();
    state.link = core::LinkHealth::Open;
    state.sinceArrival = core::kQuietTimeout;

    QCOMPARE(core::StatusLine(state), std::string("Error: connected but silent"));
}

void TestStatusLine::reportsNoPodsInRangeAfterASilence()
{
    core::StatusInputs state = Healthy();
    state.sinceArrival = core::kQuietTimeout;

    QCOMPARE(core::StatusLine(state), std::string("No AirPods in range, Bluetooth is on"));

    // One second short of quiet is not yet worth saying anything about.
    state.sinceArrival = core::kQuietTimeout - 1s;
    QCOMPARE(core::StatusLine(state), std::string());
}

// A reading loaded from the cache does not get fresher while the app sits here
// without a new one.
void TestStatusLine::agesAStoredReadingByThisRunsTime()
{
    core::StatusInputs state = Healthy();
    state.storedAge = 20min;
    state.sinceStart = 0s;
    QCOMPARE(core::StatusLine(state), std::string("Last seen 20 min ago"));

    state.sinceStart = 40min;
    QCOMPARE(core::StatusLine(state), std::string("Last seen 1 hour ago"));
}

void TestStatusLine::keepsTheStoredAgeWhileTheLinkIsDown()
{
    core::StatusInputs state = Healthy();
    state.sinceArrival = core::kQuietTimeout;
    state.storedAge = 3min;

    QCOMPARE(core::StatusLine(state), std::string("Last seen 3 min ago"));
}

void TestStatusLine::spellsOutTheAge()
{
    QCOMPARE(core::Age(0s), std::string("just now"));
    QCOMPARE(core::Age(59s), std::string("just now"));
    QCOMPARE(core::Age(60s), std::string("1 min ago"));
    QCOMPARE(core::Age(59min), std::string("59 min ago"));
    QCOMPARE(core::Age(60min), std::string("1 hour ago"));
    QCOMPARE(core::Age(2h), std::string("2 hours ago"));
    QCOMPARE(core::Age(24h), std::string("1 day ago"));
    QCOMPARE(core::Age(48h), std::string("2 days ago"));
}

core::StatusInputs Healthy()
{
    core::StatusInputs state;
    state.radio = kHealthy;
    return state;
}
} // namespace

QTEST_APPLESS_MAIN(TestStatusLine)

#include "tst_status_line.moc"
