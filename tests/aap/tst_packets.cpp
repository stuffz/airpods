#include "aap/packets.hpp"

#include <QObject>
#include <QTest>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
// Opcode 09 is shared, so the sub-command byte is part of what identifies a
// noise control report.
constexpr std::array<uint8_t, 11> kTransparency = {0x04, 0x00, 0x04, 0x00, 0x09, 0x00,
                                                   0x0d, 0x03, 0x00, 0x00, 0x00};

class TestPackets : public QObject
{
    Q_OBJECT

private slots:
    void readsEveryNoiseMode();
    void buildsWhatItReads();
    void rejectsAnUnknownMode();
    void rejectsAnotherSettingOnTheSameOpcode();
    void rejectsAPacketWithoutAModeByte();
    void namesTheModes();
    void matchesPrefixesWithoutRunningOff();
};

void TestPackets::readsEveryNoiseMode()
{
    QCOMPARE(aap::ParseNoiseMode(kTransparency), aap::NoiseMode::Transparency);

    for (const auto mode :
         {aap::NoiseMode::Off, aap::NoiseMode::NoiseCancellation, aap::NoiseMode::Transparency,
          aap::NoiseMode::Adaptive})
    {
        QCOMPARE(aap::ParseNoiseMode(aap::NoiseControlPacket(mode)), mode);
    }
}

// The buds report the current mode in the same shape they accept it in.
void TestPackets::buildsWhatItReads()
{
    const auto built = aap::NoiseControlPacket(aap::NoiseMode::Transparency);

    QCOMPARE(
        std::vector<uint8_t>(built.begin(), built.end()),
        std::vector<uint8_t>(kTransparency.begin(), kTransparency.end())
    );
}

void TestPackets::rejectsAnUnknownMode()
{
    auto packet = kTransparency;
    constexpr size_t kModeOffset = 7;
    packet[kModeOffset] = 0x09;

    QVERIFY(!aap::ParseNoiseMode(packet).has_value());
}

// Conversational awareness and the adaptive level ride the same opcode.
void TestPackets::rejectsAnotherSettingOnTheSameOpcode()
{
    auto packet = kTransparency;
    constexpr size_t kSubCommandOffset = 6;
    packet[kSubCommandOffset] = 0x28;

    QVERIFY(!aap::ParseNoiseMode(packet).has_value());
}

void TestPackets::rejectsAPacketWithoutAModeByte()
{
    const auto prefix = std::span<const uint8_t>(aap::kNoiseControl);

    QVERIFY(!aap::ParseNoiseMode(prefix).has_value());
}

void TestPackets::namesTheModes()
{
    QCOMPARE(aap::NoiseModeName(aap::NoiseMode::Off), "Off");
    QCOMPARE(aap::NoiseModeName(aap::NoiseMode::NoiseCancellation), "Noise Cancellation");
    QCOMPARE(aap::NoiseModeName(aap::NoiseMode::Transparency), "Transparency");
    QCOMPARE(aap::NoiseModeName(aap::NoiseMode::Adaptive), "Adaptive");
}

// Every parser in this namespace dispatches through this, so a prefix longer
// than the packet must answer no rather than read past it.
void TestPackets::matchesPrefixesWithoutRunningOff()
{
    QVERIFY(aap::StartsWith(kTransparency, aap::kNoiseControl));
    QVERIFY(!aap::StartsWith(kTransparency, aap::kBatteryReport));
    QVERIFY(!aap::StartsWith(std::span<const uint8_t>(kTransparency).first(3), aap::kNoiseControl));
    QVERIFY(aap::StartsWith(kTransparency, std::span<const uint8_t>{}));
}
} // namespace

QTEST_APPLESS_MAIN(TestPackets)

#include "tst_packets.moc"
