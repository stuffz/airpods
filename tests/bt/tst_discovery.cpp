#include "bt/ble_scanner.hpp"
#include "data/captures.hpp"

#include <QObject>
#include <QProcess>
#include <QTest>
#include <QtEnvironmentVariables>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace
{
// Drives fake_bluez.py beside it: one command in, one reply out. The scanner sees
// a private bus with nothing but the fake on it.
class FakeBluez
{
public:
    static bool Available();

    bool Start();
    QString Send(const QString &command);
    void Stop();

private:
    static constexpr int kReplyMs = 10000;

    QString ReadLine();

    QProcess process;
};

// Pumps the scanner's own bus until the condition holds, rather than sleeping
// for a fixed time and hoping: nothing here is slower than a round trip on a
// socket in the same container.
bool Until(bt::BleScanner &scanner, const std::function<bool()> &condition);
QString Hex(std::span<const uint8_t> bytes);

constexpr uint16_t kAppleVendorId = 0x004c;

// The scanner against a fake BlueZ. Everything here is a recovery path, which
// is the half of the scanner no parser test can reach.
class TestDiscovery : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startsDiscoveryOnAPoweredAdapter();
    void treatsInProgressAsSuccess();
    void restartsWhenBlueZDropsDiscovery();
    void waitsForAnAdapterToAppear();
    void restartsWhenTheAdapterComesBack();
    void waitsForBluetoothToBePoweredOn();
    void recheckRestartsSilentlyStoppedDiscovery();
    void deliversAppleManufacturerData();

private:
    FakeBluez fake;
};
} // namespace

void TestDiscovery::init()
{
    if (!FakeBluez::Available())
    {
        QSKIP("python-dbusmock is not installed; make container-test has it");
    }

    QVERIFY(fake.Start());
}

void TestDiscovery::cleanup()
{
    fake.Stop();
}

void TestDiscovery::startsDiscoveryOnAPoweredAdapter()
{
    QCOMPARE(fake.Send("add-adapter"), QString("/org/bluez/hci0"));

    bt::BleScanner scanner;
    QVERIFY(scanner.Start(kAppleVendorId));
    QVERIFY(scanner.HasAdapter());
    QVERIFY(scanner.IsPowered());
    QVERIFY(scanner.IsScanning());
    QCOMPARE(fake.Send("discovering?"), QString("on"));
}

// The session is already there, which is the state this wanted. Treating the
// error as a failure would leave the scanner believing it is not scanning
// while advertisements arrive anyway.
void TestDiscovery::treatsInProgressAsSuccess()
{
    fake.Send("add-adapter");
    fake.Send("fail-start-discovery");

    bt::BleScanner scanner;
    QVERIFY(scanner.Start(kAppleVendorId));
    QVERIFY(scanner.IsScanning());
}

// BlueZ ends every client's session when the adapter goes down, and does not
// bring it back on its own.
void TestDiscovery::restartsWhenBlueZDropsDiscovery()
{
    fake.Send("add-adapter");

    bt::BleScanner scanner;
    QVERIFY(scanner.Start(kAppleVendorId));
    QVERIFY(scanner.IsScanning());

    fake.Send("discovering off");
    QVERIFY(Until(scanner, [&] { return fake.Send("discovering?") == QString("on"); }));
    QVERIFY(scanner.IsScanning());
}

// An autostart that beats BlueZ to the bus must not end the app.
void TestDiscovery::waitsForAnAdapterToAppear()
{
    bt::BleScanner scanner;
    QVERIFY(scanner.Start(kAppleVendorId));
    QVERIFY(!scanner.HasAdapter());
    QVERIFY(!scanner.IsScanning());

    fake.Send("add-adapter");
    QVERIFY(Until(scanner, [&] { return scanner.IsScanning(); }));
    QVERIFY(scanner.HasAdapter());
}

// A dongle unplugged and plugged back in, or BlueZ restarting: the old session
// is gone with the old object, whatever this side still believes.
void TestDiscovery::restartsWhenTheAdapterComesBack()
{
    fake.Send("add-adapter");

    bt::BleScanner scanner;
    QVERIFY(scanner.Start(kAppleVendorId));
    QVERIFY(scanner.IsScanning());

    fake.Send("remove-adapter");
    fake.Send("add-adapter");

    QVERIFY(Until(scanner, [&] { return fake.Send("discovering?") == QString("on"); }));
    QVERIFY(scanner.IsScanning());
}

void TestDiscovery::waitsForBluetoothToBePoweredOn()
{
    fake.Send("add-adapter");
    fake.Send("powered off");

    bt::BleScanner scanner;
    QVERIFY(scanner.Start(kAppleVendorId));
    QVERIFY(scanner.HasAdapter());
    QVERIFY(!scanner.IsPowered());
    QVERIFY(!scanner.IsScanning());

    fake.Send("powered on");
    QVERIFY(Until(scanner, [&] { return scanner.IsScanning(); }));
    QVERIFY(scanner.IsPowered());
}

// The signal that would have reported this is the one that went missing, so
// the recheck is the only thing that can notice.
void TestDiscovery::recheckRestartsSilentlyStoppedDiscovery()
{
    fake.Send("add-adapter");

    bt::BleScanner scanner;
    QVERIFY(scanner.Start(kAppleVendorId));
    QVERIFY(scanner.IsScanning());

    // Straight into the adapter's property, with no PropertiesChanged behind
    // it: the scanner is left believing discovery is still running.
    fake.Send("discovering-quietly off");
    QCOMPARE(fake.Send("discovering?"), QString("off"));
    QVERIFY(scanner.IsScanning());

    scanner.Recheck();
    QCOMPARE(fake.Send("discovering?"), QString("on"));
    QVERIFY(scanner.IsScanning());
}

void TestDiscovery::deliversAppleManufacturerData()
{
    fake.Send("add-adapter");

    std::string seen;
    std::vector<uint8_t> payload;

    bt::BleScanner scanner;
    scanner.OnAdvertisement(
        [&](const std::string &address, std::span<const uint8_t> data)
        {
            seen = address;
            payload.assign(data.begin(), data.end());
        }
    );

    QVERIFY(scanner.Start(kAppleVendorId));

    const auto advert = std::span<const uint8_t>(captures::kPodsCleartextBothInCase);
    fake.Send("advertise AA:BB:CC:DD:EE:FF " + Hex(advert));

    QVERIFY(Until(scanner, [&] { return !payload.empty(); }));
    QCOMPARE(seen, std::string("AA:BB:CC:DD:EE:FF"));
    QCOMPARE(payload, std::vector<uint8_t>(advert.begin(), advert.end()));
}

namespace
{
// Missing entirely is a machine without the fake, which is worth saying rather
// than failing over. A fake that is there and misbehaves is a failure.
bool FakeBluez::Available()
{
    QProcess probe;
    probe.start("python3", {QStringLiteral("-c"), QStringLiteral("import dbusmock")});

    return probe.waitForFinished(kReplyMs) && probe.exitCode() == 0;
}

bool FakeBluez::Start()
{
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process.start("python3", {QStringLiteral(FAKE_BLUEZ_SCRIPT)});

    if (!process.waitForStarted(kReplyMs))
    {
        return false;
    }

    const QString ready = ReadLine();
    if (!ready.startsWith("ready "))
    {
        return false;
    }

    // sd_bus_open_system reads this, so the scanner reaches the fake instead
    // of whatever BlueZ the machine running the test happens to have.
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", ready.mid(ready.indexOf(' ') + 1).toUtf8());
    return true;
}

QString FakeBluez::Send(const QString &command)
{
    process.write(command.toUtf8() + '\n');
    process.waitForBytesWritten(kReplyMs);

    QString reply = ReadLine();
    if (!reply.startsWith("ok"))
    {
        return reply;
    }

    return reply.mid(QStringLiteral("ok").size()).trimmed();
}

void FakeBluez::Stop()
{
    if (process.state() == QProcess::NotRunning)
    {
        return;
    }

    process.write("quit\n");
    process.closeWriteChannel();

    if (!process.waitForFinished(kReplyMs))
    {
        process.kill();
        process.waitForFinished(kReplyMs);
    }
}

QString FakeBluez::ReadLine()
{
    while (!process.canReadLine())
    {
        if (!process.waitForReadyRead(kReplyMs))
        {
            return {};
        }
    }

    return QString::fromUtf8(process.readLine()).trimmed();
}

QString Hex(std::span<const uint8_t> bytes)
{
    QString text;

    for (const uint8_t byte : bytes)
    {
        text += QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0'));
    }

    return text;
}

bool Until(bt::BleScanner &scanner, const std::function<bool()> &condition)
{
    constexpr int kAttempts = 100;
    constexpr int kWaitMs = 20;

    for (int attempt = 0; attempt < kAttempts; ++attempt)
    {
        if (condition())
        {
            return true;
        }

        if (!scanner.Process(kWaitMs))
        {
            return false;
        }
    }

    return condition();
}
} // namespace

QTEST_GUILESS_MAIN(TestDiscovery)

#include "tst_discovery.moc"
