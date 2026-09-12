#include "aap/packets.hpp"
#include "core/options.hpp"

#include <QObject>
#include <QTest>

#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
core::ParseResult Parse(std::initializer_list<const char *> arguments, core::Options &options);

// Keeps the usage text and the complaints out of the test output, and lets
// them be asserted on.
class CapturedOutput
{
public:
    CapturedOutput() : out(std::cout.rdbuf(sink.rdbuf())), err(std::cerr.rdbuf(sink.rdbuf())) {}

    CapturedOutput(const CapturedOutput &) = delete;
    CapturedOutput &operator=(const CapturedOutput &) = delete;

    ~CapturedOutput()
    {
        std::cout.rdbuf(out);
        std::cerr.rdbuf(err);
    }

    std::string Text() const { return sink.str(); }

private:
    std::ostringstream sink;
    std::streambuf *out;
    std::streambuf *err;
};

class TestOptions : public QObject
{
    Q_OBJECT

private slots:
    void defaultsToTheTray();
    void readsTheLongAndShortSpellings();
    void readsValuesThatFollowTheirFlag();
    void picksTheHeadTrackingVariant();
    void rejectsAnIntervalThatIsNotAPositiveNumber();
    void rejectsAFlagWithNothingAfterIt();
    void rejectsAnUnknownOption();
    void printsUsageAndVersionWithoutRunning();
};

void TestOptions::defaultsToTheTray()
{
    core::Options options;
    QCOMPARE(Parse({}, options), core::ParseResult::Run);

    QVERIFY(!options.console);
    QVERIFY(!options.ble);
    QVERIFY(!options.debug);
    QVERIFY(options.address.empty());
    QVERIFY(options.keyFile.empty());
    QCOMPARE(options.intervalMs, 1000);
}

void TestOptions::readsTheLongAndShortSpellings()
{
    core::Options longForm;
    QCOMPARE(
        Parse(
            {"--console", "--ble", "--debug", "--once", "--list", "--gestures", "--keys"}, longForm
        ),
        core::ParseResult::Run
    );
    QVERIFY(longForm.console && longForm.ble && longForm.debug);
    QVERIFY(longForm.once && longForm.list && longForm.gestures && longForm.keys);

    core::Options shortForm;
    QCOMPARE(Parse({"-c", "-b", "-d", "-1", "-l", "-g", "-k"}, shortForm), core::ParseResult::Run);
    QVERIFY(shortForm.console && shortForm.ble && shortForm.debug);
    QVERIFY(shortForm.once && shortForm.list && shortForm.gestures && shortForm.keys);
}

void TestOptions::readsValuesThatFollowTheirFlag()
{
    core::Options options;
    QCOMPARE(
        Parse(
            {"--address", "AA:BB:CC:DD:EE:FF", "--interval", "250", "--key-file", "/tmp/keys.conf"},
            options
        ),
        core::ParseResult::Run
    );

    QCOMPARE(options.address, std::string("AA:BB:CC:DD:EE:FF"));
    QCOMPARE(options.intervalMs, 250);
    QCOMPARE(options.keyFile.string(), std::string("/tmp/keys.conf"));
}

// Which variant a firmware answers varies, and the wrong one is silence.
void TestOptions::picksTheHeadTrackingVariant()
{
    core::Options alternate;
    QCOMPARE(Parse({"--tracking", "alt"}, alternate), core::ParseResult::Run);
    QCOMPARE(alternate.headTracking, aap::HeadTrackingVariant::Alternate);

    core::Options macos;
    QCOMPARE(Parse({"--tracking", "macos"}, macos), core::ParseResult::Run);
    QCOMPARE(macos.headTracking, aap::HeadTrackingVariant::Macos);

    core::Options wrong;
    const CapturedOutput output;
    QCOMPARE(Parse({"--tracking", "windows"}, wrong), core::ParseResult::Error);
    QVERIFY(output.Text().find("alt") != std::string::npos);
}

// A trailing unit or a zero would otherwise become a timer that never fires or
// one that fires continuously.
void TestOptions::rejectsAnIntervalThatIsNotAPositiveNumber()
{
    for (const char *value : {"0", "-5", "250ms", "abc", ""})
    {
        core::Options options;
        const CapturedOutput output;

        QCOMPARE(Parse({"--interval", value}, options), core::ParseResult::Error);
        QVERIFY(!output.Text().empty());
    }
}

void TestOptions::rejectsAFlagWithNothingAfterIt()
{
    for (const char *flag : {"--interval", "--address", "--tracking", "--key-file"})
    {
        core::Options options;
        const CapturedOutput output;

        QCOMPARE(Parse({flag}, options), core::ParseResult::Error);
        QVERIFY(output.Text().find("Missing value for") != std::string::npos);
    }
}

void TestOptions::rejectsAnUnknownOption()
{
    core::Options options;
    const CapturedOutput output;

    QCOMPARE(Parse({"--colour"}, options), core::ParseResult::Error);
    QVERIFY(output.Text().find("Unknown option: --colour") != std::string::npos);
}

void TestOptions::printsUsageAndVersionWithoutRunning()
{
    for (const char *flag : {"-h", "--help", "-V", "--version"})
    {
        core::Options options;
        const CapturedOutput output;

        QCOMPARE(Parse({flag}, options), core::ParseResult::Exit);
        QVERIFY(!output.Text().empty());
    }
}

core::ParseResult Parse(std::initializer_list<const char *> arguments, core::Options &options)
{
    std::vector<char *> argv{const_cast<char *>("airpods")};

    for (const char *argument : arguments)
    {
        argv.push_back(const_cast<char *>(argument));
    }

    return core::ParseOptions(static_cast<int>(argv.size()), argv.data(), options);
}
} // namespace

QTEST_APPLESS_MAIN(TestOptions)

#include "tst_options.moc"
