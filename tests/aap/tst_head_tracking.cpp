#include "aap/head_tracking.hpp"
#include "aap/packets.hpp"

#include <QObject>
#include <QTest>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace
{
std::vector<uint8_t> Frame();

constexpr size_t kFrameSize = 55;
constexpr size_t kKindOffset = 10;
constexpr size_t kReservedOffset = 11;
constexpr size_t kOrientationOffset = 43;
constexpr size_t kAccelOffset = 51;
constexpr uint8_t kSensorKind = 0x44;

// The sensor stream behind --gestures. No capture of one was kept, so the
// frames here are built from the offset table in docs/l2cap.md; what they pin
// is the reading, which is little endian and signed.
class TestHeadTracking : public QObject
{
    Q_OBJECT

private slots:
    void readsSignedLittleEndianFields();
    void acceptsBothSensorKinds();
    void rejectsAnAcknowledgement();
    void rejectsAShortPacket();
    void rejectsAnotherOpcode();
};

void TestHeadTracking::readsSignedLittleEndianFields()
{
    auto packet = Frame();

    // 0x8000 is the most negative int16, which a byte-swapped or unsigned read
    // would turn into 128 or 32768.
    packet[kOrientationOffset] = 0x00;
    packet[kOrientationOffset + 1] = 0x80;
    packet[kOrientationOffset + 2] = 0xff;
    packet[kOrientationOffset + 3] = 0xff;
    packet[kOrientationOffset + 4] = 0x34;
    packet[kOrientationOffset + 5] = 0x12;
    packet[kAccelOffset] = 0xff;
    packet[kAccelOffset + 1] = 0x7f;
    packet[kAccelOffset + 2] = 0x01;
    packet[kAccelOffset + 3] = 0x00;

    const auto sample = aap::ParseHeadSample(packet);
    QVERIFY(sample.has_value());
    QCOMPARE(sample->orientation1, int16_t{-32768});
    QCOMPARE(sample->orientation2, int16_t{-1});
    QCOMPARE(sample->orientation3, int16_t{0x1234});
    QCOMPARE(sample->horizontalAccel, int16_t{32767});
    QCOMPARE(sample->verticalAccel, int16_t{1});
}

void TestHeadTracking::acceptsBothSensorKinds()
{
    constexpr uint8_t kOtherSensorKind = 0x45;

    auto packet = Frame();
    QVERIFY(aap::ParseHeadSample(packet).has_value());

    packet[kKindOffset] = kOtherSensorKind;
    QVERIFY(aap::ParseHeadSample(packet).has_value());
}

// Opcode 0x17 carries acknowledgements as well as sensor frames, and reading
// one of those as a pose would feed the detector noise.
void TestHeadTracking::rejectsAnAcknowledgement()
{
    auto packet = Frame();
    packet[kKindOffset] = 0x00;
    QVERIFY(!aap::ParseHeadSample(packet).has_value());

    packet[kKindOffset] = kSensorKind;
    packet[kReservedOffset] = 0x01;
    QVERIFY(!aap::ParseHeadSample(packet).has_value());
}

void TestHeadTracking::rejectsAShortPacket()
{
    const auto packet = Frame();
    const auto truncated = std::span<const uint8_t>(packet).first(kFrameSize - 1);

    QVERIFY(!aap::ParseHeadSample(truncated).has_value());
}

void TestHeadTracking::rejectsAnotherOpcode()
{
    auto packet = Frame();
    constexpr size_t kOpcodeOffset = 4;
    packet[kOpcodeOffset] = 0x04;

    QVERIFY(!aap::ParseHeadSample(packet).has_value());
}

std::vector<uint8_t> Frame()
{
    std::vector<uint8_t> packet(kFrameSize, 0x00);
    std::copy(aap::kHeadTracking.begin(), aap::kHeadTracking.end(), packet.begin());
    packet[kKindOffset] = kSensorKind;
    return packet;
}
} // namespace

QTEST_APPLESS_MAIN(TestHeadTracking)

#include "tst_head_tracking.moc"
