#pragma once

// Fixture bytes for the parser tests.
//
// The decrypted blocks and the battery levels are the September 2026 records
// transcribed in docs/ble-research.md. The advertisements around them are not:
// a real one carries its block encrypted under the owner's key, which does not
// belong in a repository, so the tests reseal the recorded plaintext under
// kTestKey and hand the parsers the result.
//
// One cleartext section is assembled rather than recorded, and says so.

#include "bt/aes.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace captures
{

// FIPS 197, Appendix C.1. Any key would do; a published one keeps the fixtures
// readable and makes it obvious this is not anybody's encryption key.
inline constexpr bt::AesBlock kTestKey = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                          0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

// ----------------------------------------------------- pods' decrypted block
//
// Recorded 2026-09-09 with the pods at 94/93% and the case at 61%. Bytes 7-9
// are an address fragment, redacted as aa bb cc in the notes and irrelevant to
// every parser here.
//
//   flags | primary | secondary | case | 4c 96 ff | address x3 | state x2 | varies x4

inline constexpr bt::AesBlock kPodsBothInCase = {0x00, 0xde, 0xdd, 0xbd, 0x4c, 0x96, 0xff, 0xaa,
                                                 0xbb, 0xcc, 0x00, 0x10, 0xf6, 0x2f, 0x20, 0x79};

// The same instant from the other advertiser: bytes 1 and 2 swap, so the pair
// reads 94/93 again only if the primary flag is followed.
inline constexpr bt::AesBlock kPodsBothInCaseSwapped = {0x00, 0xdd, 0xde, 0xbd, 0x4c, 0x96,
                                                        0xff, 0xaa, 0xbb, 0xcc, 0x00, 0x10,
                                                        0x6a, 0xbe, 0xc7, 0xe0};

// Both pods out of the case, which puts the case out of contact: ff, absent.
inline constexpr bt::AesBlock kPodsOut = {0x04, 0x5e, 0x5d, 0xff, 0x4c, 0x96, 0xff, 0x00,
                                          0x00, 0x00, 0x01, 0x10, 0x2e, 0x53, 0xbf, 0x13};

// ----------------------------------------------------- case's decrypted block
//
//   29 20 | lid | case | left | right | 51 0b 00 | ? | 00 00 | counter x4
//
// First run, pods 94/93% and case 61%. The charger is the only change between
// the first two, and the case byte is what moves.

inline constexpr bt::AesBlock kCaseOnTable = {0x29, 0x20, 0x08, 0x3d, 0xde, 0xdd, 0x51, 0x0b,
                                              0x00, 0x00, 0x00, 0x00, 0xf7, 0x82, 0x16, 0x00};

inline constexpr bt::AesBlock kCaseOnCharger = {0x29, 0x20, 0x08, 0xbd, 0xde, 0xdd, 0x51, 0x0b,
                                                0x00, 0x00, 0x00, 0x00, 0x25, 0x83, 0x16, 0x00};

inline constexpr bt::AesBlock kCasePodsOut = {0x29, 0x20, 0x09, 0x3d, 0xff, 0xff, 0x51, 0x0b,
                                              0x00, 0x00, 0x00, 0x00, 0x40, 0x83, 0x16, 0x00};

// Second run, which isolated the right pod. Its lid bytes are the ones that
// agree with bit 3 meaning shut; the first run's 09 is labelled "lid open" in
// the notes and contradicts its own bit, so no lid state is asserted from it.

inline constexpr bt::AesBlock kCaseLidShutBothIn = {0x29, 0x20, 0x0a, 0x3d, 0xde, 0xdd, 0x51, 0x0b,
                                                    0x00, 0x0e, 0x00, 0x00, 0xb6, 0x84, 0x16, 0x00};

inline constexpr bt::AesBlock kCaseLidOpenBothIn = {0x29, 0x20, 0x03, 0x3d, 0xde, 0xdd, 0x51, 0x0b,
                                                    0x00, 0x00, 0x00, 0x00, 0xc3, 0x84, 0x16, 0x00};

inline constexpr bt::AesBlock kCaseLidOpenRightOut = {0x29, 0x20, 0x03, 0x3d, 0xde, 0xff,
                                                      0x51, 0x0b, 0x00, 0x00, 0x00, 0x00,
                                                      0xcd, 0x84, 0x16, 0x00};

inline constexpr bt::AesBlock kCaseLidShutRightOut = {0x29, 0x20, 0x0b, 0x3d, 0xde, 0xff,
                                                      0x51, 0x0b, 0x00, 0x05, 0x00, 0x00,
                                                      0xda, 0x84, 0x16, 0x00};

// The counter 19 hours after kCaseOnTable, when byte 14 carried from 16 to 17
// and an identity check that covered it began rejecting valid adverts.
inline constexpr std::array<uint8_t, 4> kCaseCounterLater = {0x27, 0x8e, 0x17, 0x00};

// -------------------------------------------------------- pods' cleartext
//
// Assembled, not recorded: the notes kept decrypted blocks and decoded values,
// never the cleartext bytes that carried them. The fields are the layout table
// in docs/ble.md filled in from the 2026-09-09 session, so the nibbles here are
// that session rounded to 10% steps - 94/93/61 becomes 90/90/60.
//
//   07 19 | mode | model x2 | status | pods | case+flags | lid | colour | link

// Both pods in the case and charging, lid shut, left primary. The counterpart
// of kPodsBothInCase.
inline constexpr std::array<uint8_t, 11> kPodsCleartextBothInCase = {0x07, 0x19, 0x01, 0x27,
                                                                     0x20, 0x64, 0x99, 0x36,
                                                                     0x08, 0x00, 0x00};

// The right pod out of the case and gone from the broadcast: its nibble is 15,
// the absent marker. Lid open, third open of this power cycle.
inline constexpr std::array<uint8_t, 11> kPodsCleartextRightOut = {0x07, 0x19, 0x01, 0x27,
                                                                   0x20, 0x70, 0xf9, 0x16,
                                                                   0x03, 0x00, 0x00};

// ----------------------------------------------------------- control link
//
// The battery report frame from docs/l2cap.md carrying the same session's
// readings: right 93 and left 94 discharging, case 61 charging.
inline constexpr std::array<uint8_t, 22> kBatteryReport = {0x04, 0x00, 0x04, 0x00, 0x04, 0x00,
                                                           0x03, 0x02, 0x01, 0x5d, 0x02, 0x01,
                                                           0x04, 0x01, 0x5e, 0x02, 0x01, 0x08,
                                                           0x01, 0x3d, 0x01, 0x01};

inline std::vector<uint8_t> Seal(std::span<const uint8_t> cleartext, const bt::AesBlock &block);

// A complete 27-byte pods advertisement: the nine cleartext bytes after the
// type and length, then the block.
inline std::vector<uint8_t>
PodsAdvert(std::span<const uint8_t> cleartext, const bt::AesBlock &block)
{
    return Seal(cleartext, block);
}

// A complete 19-byte case advertisement. The case sends no cleartext beyond the
// type, the length and one flag byte.
inline std::vector<uint8_t> CaseAdvert(const bt::AesBlock &block)
{
    constexpr std::array<uint8_t, 3> kPrefix = {0x07, 0x11, 0x06};

    return Seal(kPrefix, block);
}

inline std::vector<uint8_t> Seal(std::span<const uint8_t> cleartext, const bt::AesBlock &block)
{
    const auto sealed = bt::AesEncryptBlock(kTestKey, block);
    if (!sealed)
    {
        throw std::runtime_error("Fixture block did not encrypt");
    }

    std::vector<uint8_t> advert(cleartext.begin(), cleartext.end());
    advert.insert(advert.end(), sealed->begin(), sealed->end());
    return advert;
}

} // namespace captures
