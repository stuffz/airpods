#include "aap/battery.hpp"
#include "core/battery_store.hpp"
#include "core/storage_paths.hpp"

#include <QByteArray>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>
#include <QtEnvironmentVariables>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>

namespace
{
using namespace std::chrono_literals;

// The cache the tray shows while the buds are away in a shut case, where
// nothing can refresh it.
class TestBatteryStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void writesUnderTheStateDirectory();
    void restoresEveryKnownComponent();
    void marksWhatItLoadedAsStored();
    void reportsTheAgeOfWhatItLoaded();
    void loadsNothingWhenThereIsNoCache();
    void writesNothingWithoutAReading();
    void ignoresAnUnknownComponentName();

private:
    std::filesystem::path Cache() const;

    QTemporaryDir directory;
    QByteArray previousState;
    QByteArray previousConfig;
};

void TestBatteryStore::init()
{
    QVERIFY(directory.isValid());

    previousState = qgetenv("XDG_STATE_HOME");
    previousConfig = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_STATE_HOME", directory.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());

    std::filesystem::remove(Cache());
}

void TestBatteryStore::cleanup()
{
    qputenv("XDG_STATE_HOME", previousState);
    qputenv("XDG_CONFIG_HOME", previousConfig);
}

// State, not configuration: this is a cache the program rewrites, not
// something anyone edits.
void TestBatteryStore::writesUnderTheStateDirectory()
{
    aap::Battery battery;
    battery.Restore(aap::Component::Left, 94, aap::ChargeStatus::Discharging);

    core::BatteryStore().Save(battery);

    QVERIFY(std::filesystem::exists(Cache()));
    QCOMPARE(
        core::StoragePath(core::StorageKind::State),
        std::filesystem::path(directory.path().toStdString()) / "airpods"
    );
}

void TestBatteryStore::restoresEveryKnownComponent()
{
    aap::Battery saved;
    saved.Restore(aap::Component::Left, 94, aap::ChargeStatus::Discharging);
    saved.Restore(aap::Component::Right, 93, aap::ChargeStatus::Discharging);
    saved.Restore(aap::Component::Case, 61, aap::ChargeStatus::Charging);

    const core::BatteryStore store;
    store.Save(saved);

    aap::Battery loaded;
    QVERIFY(store.Load(loaded).has_value());

    QCOMPARE(static_cast<int>(loaded.Get(aap::Component::Left).level), 94);
    QCOMPARE(static_cast<int>(loaded.Get(aap::Component::Right).level), 93);
    QCOMPARE(static_cast<int>(loaded.Get(aap::Component::Case).level), 61);
    QCOMPARE(loaded.Get(aap::Component::Case).status, aap::ChargeStatus::Charging);

    // Never reported by these buds, so never written and never read back.
    QVERIFY(!loaded.Get(aap::Component::Headset).known);
}

// A reading off the disk is not fresh data, and the display says so.
void TestBatteryStore::marksWhatItLoadedAsStored()
{
    aap::Battery saved;
    saved.Restore(aap::Component::Left, 94, aap::ChargeStatus::Discharging);

    const core::BatteryStore store;
    store.Save(saved);

    aap::Battery loaded;
    store.Load(loaded);

    QCOMPARE(loaded.Get(aap::Component::Left).source, aap::Source::Stored);
}

void TestBatteryStore::reportsTheAgeOfWhatItLoaded()
{
    const auto stamp = core::BatteryStore::Clock::to_time_t(
        core::BatteryStore::Clock::now() - std::chrono::minutes(90)
    );

    std::filesystem::create_directories(Cache().parent_path());
    {
        std::ofstream file(Cache(), std::ios::trunc);
        file << "saved " << stamp << "\nLeft 94 2\n";
    }

    aap::Battery loaded;
    const auto age = core::BatteryStore().Load(loaded);

    QVERIFY(age.has_value());
    QVERIFY(*age >= 89min);
    QVERIFY(*age <= 91min);
    QCOMPARE(static_cast<int>(loaded.Get(aap::Component::Left).level), 94);
}

void TestBatteryStore::loadsNothingWhenThereIsNoCache()
{
    aap::Battery loaded;

    QVERIFY(!core::BatteryStore().Load(loaded).has_value());
    QVERIFY(!loaded.HasReading());
}

void TestBatteryStore::writesNothingWithoutAReading()
{
    const aap::Battery empty;
    core::BatteryStore().Save(empty);

    QVERIFY(!std::filesystem::exists(Cache()));
}

// The file is the program's own, but a stale one from another version must not
// put a reading somewhere it does not belong.
void TestBatteryStore::ignoresAnUnknownComponentName()
{
    std::filesystem::create_directories(Cache().parent_path());
    {
        std::ofstream file(Cache(), std::ios::trunc);
        file << "saved 0\nTemple 50 2\nLeft 94 2\n";
    }

    aap::Battery loaded;
    core::BatteryStore().Load(loaded);

    QCOMPARE(static_cast<int>(loaded.Get(aap::Component::Left).level), 94);
    QVERIFY(!loaded.Get(aap::Component::Headset).known);
}

std::filesystem::path TestBatteryStore::Cache() const
{
    return core::StoragePath(core::StorageKind::State) / "last-battery";
}
} // namespace

QTEST_APPLESS_MAIN(TestBatteryStore)

#include "tst_battery_store.moc"
