#include "core/link_retry.hpp"

#include <QObject>
#include <QTest>

#include <chrono>

namespace
{
using namespace std::chrono_literals;

// Which of the two backoffs a poll earns. The long one exists because the
// buds only accept one holder of the control link.
class TestLinkRetry : public QObject
{
    Q_OBJECT

private slots:
    void aLostSocketComesBackSoon();
    void aWorkingLinkIsLeftAlone();
    void waitsOutTheHandshakeBeforeCallingItWedged();
    void backsOffFromAWedgedLink();
    void spacesTheTwoBackoffsFarApart();
};

// The ordinary case: the buds went away, or the socket died. Nothing about
// that needs a minute of thought.
void TestLinkRetry::aLostSocketComesBackSoon()
{
    QCOMPARE(core::RetryAfterPoll(core::Link::Lost, core::Handshake::Done, 0ms), core::Retry::Soon);
    QCOMPARE(
        core::RetryAfterPoll(core::Link::Lost, core::Handshake::Pending, 1h), core::Retry::Soon
    );
}

void TestLinkRetry::aWorkingLinkIsLeftAlone()
{
    QCOMPARE(core::RetryAfterPoll(core::Link::Alive, core::Handshake::Done, 1h), core::Retry::None);
}

// The buds answer in well under a second when they are listening at all, but
// the allowance is generous and nothing may be dropped inside it.
void TestLinkRetry::waitsOutTheHandshakeBeforeCallingItWedged()
{
    QCOMPARE(
        core::RetryAfterPoll(core::Link::Alive, core::Handshake::Pending, 0ms), core::Retry::None
    );
    QCOMPARE(
        core::RetryAfterPoll(
            core::Link::Alive, core::Handshake::Pending, core::kHandshakeTimeout - 1ms
        ),
        core::Retry::None
    );
}

// A socket that opened and never answered is the buds wedged on their side,
// which no reconnect clears.
void TestLinkRetry::backsOffFromAWedgedLink()
{
    QCOMPARE(
        core::RetryAfterPoll(core::Link::Alive, core::Handshake::Pending, core::kHandshakeTimeout),
        core::Retry::Later
    );
    QCOMPARE(
        core::RetryAfterPoll(core::Link::Alive, core::Handshake::Pending, 1h), core::Retry::Later
    );
}

// Retrying a wedge on the short delay would seize and drop the single-holder
// link continuously, so the two must not drift together.
void TestLinkRetry::spacesTheTwoBackoffsFarApart()
{
    QCOMPARE(core::RetryDelay(core::Retry::Soon), core::kRetrySoon);
    QCOMPARE(core::RetryDelay(core::Retry::Later), core::kRetryLater);
    QVERIFY(!core::RetryDelay(core::Retry::None).has_value());
    QVERIFY(core::kRetryLater >= core::kRetrySoon * 10);
}
} // namespace

QTEST_APPLESS_MAIN(TestLinkRetry)

#include "tst_link_retry.moc"
