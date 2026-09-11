# BLE research evidence

Supporting captures for the [BLE guide](ble.md). These are records from the original September 2026 investigation, not newly repeated measurements or a guarantee across models and firmware. See [L2CAP](l2cap.md) for the connected protocol.

## Pods: exact battery and advertiser order

Decrypted blocks recorded on 2026-09-09, with pods at 94/93% and case at 61%. The public-address fragment at bytes 7–9 is redacted here as `aa bb cc`; it is not a protocol constant.

```text
pods in case, lid open:   00 de dd bd 4c 96 ff aa bb cc 00 10 f6 2f 20 79
same, other pod:         00 dd de bd 4c 96 ff aa bb cc 00 10 6a be c7 e0
pods out:                04 5e 5d ff 4c 96 ff 00 00 00 01 10 2e 53 bf 13
pods out, later:         44 5e 5d ff 4c 96 ff 00 00 00 01 15 93 ef 5b 99
```

Observations:

- Bytes 1–2 swap between advertisers. Pulling the right pod produced right-not-charging and then right-absent, supporting the primary-to-left/right mapping.
- Bytes 7–9 carry an address fragment while both pods are in the case, and zero when either is out.
- `ff` is absent as a whole byte. Splitting it first incorrectly produced “absent, charging”.
- Another comparison gave cleartext 80/80/70% and decrypted 80/82/73%. The encrypted block provides precision unavailable in the nibbles.

Unassigned fields: byte 0 values included `00 08 20 68` with the lid shut and `04 24 44 64` with the lid open or the advertising pod out. Bit `04` tracked that distinction in these captures. Byte 10 was normally `01` with that bit set, otherwise `00`; `91` appeared twice with the lid shut. Byte 11 included `10`, `11`, `15`. Bytes 12–15 varied, but their purpose is unknown.

## Case: controlled changes

Case plaintext layout, with offsets relative to the decrypted block:

```text
0–1     2     3      4      5       6–8      9    10–11   12–15
29 20 | lid | case | left | right | 51 0b 00 | ? | 00 00 | counter
```

Pods 94/93%, case 61%:

```text
on table, pods in:         29 20 08 3d de dd 51 0b 00 00 00 00 f7 82 16 00
case on charger:          29 20 08 bd de dd 51 0b 00 00 00 00 25 83 16 00
pods out, lid open:        29 20 09 3d ff ff 51 0b 00 00 00 00 40 83 16 00
pods out, case charging:   29 20 09 bd ff ff 51 0b 00 00 00 00 45 83 16 00
```

The charger changes case byte `3d` (61%) to `bd` (61%, charging). Removing both pods changes their bytes to `ff`. Pod charging bits remained set while seated in the case, whether or not the case was externally powered.

A second run isolated the right pod:

```text
lid shut, both in:         29 20 0a 3d de dd 51 0b 00 0e 00 00 b6 84 16 00
lid open, both in:         29 20 03 3d de dd 51 0b 00 00 00 00 c3 84 16 00
lid open, right out:       29 20 03 3d de ff 51 0b 00 00 00 00 cd 84 16 00
lid shut, right out:       29 20 0b 3d de ff 51 0b 00 05 00 00 da 84 16 00
lid shut, both in again:   29 20 0c 3d de dd 51 0b 00 00 00 00 e2 84 16 00
lid open, both in:         29 20 05 3d de dd 51 0b 00 0e 00 00 ea 84 16 00
```

Byte 5 alone becoming `ff` establishes right-pod position. Lid bit 3 followed shut/open, while bits 0–2 advanced with opens, including those needed to replace pods. Byte 9 was usually `00`, with `0e` and `05` near lid/pod events; its meaning remains unknown.

### Seconds counter and the rejected-advert regression

| Capture | Bytes 12–15 | Little-endian value |
| --- | --- | --- |
| 2026-09-09 | `f7 82 16 00` | `0x1682f7` |
| About 19 hours later | `27 8e 17 00` | `0x178e27` |

The difference is 68,400, matching 19 hours in seconds. This supports a seconds counter; **case uptime is a hypothesis**, not established by a reset experiment. The later value corresponds to about 18 days. At one tick per second, byte 15 would become nonzero after about 194 days.

Disproven assumption: bytes 14–15 were fixed `16 00`. Byte 14 carried to `17` overnight and the identity check rejected valid adverts, leaving the tray stale. Commit `fa0a9a8` removed offsets 14–15 from the fixed-byte check. Current checks cover bytes 6–8, 10 and 11; the header is checked separately for warnings. None of bytes 12–15 belongs in that identity check.

### Battery trend and phone comparison

Recorded while the pods charged inside the case:

| Time | Case | Pod A, charging | Pod B, charging |
| --- | --- | --- | --- |
| 17:10 | 68% | 78% | 78% |
| 17:11 | 67% | 81% | 79% |
| 17:15 | 66% | 82% | 81% |
| 17:16 | 65% | 85% | 84% |
| 17:22 | 63% | 89% | 88% |

The original notes also record phone readings `77/69` at 17:05 and `80/67` at 17:13, without labeling both numbers. These are not simultaneous comparisons with the table and do not establish the original claim that values “tracked the phone exactly”. The table supports rising pod levels and falling case level.

## Discovery, identity and delivery

Early tests missed the case format:

| Test | Original result |
| --- | --- |
| `--ble`, 40 seconds | No recognized AirPods adverts or IRK matches |
| Passive watch, 7 minutes | Same |
| Raw HCI capture, 45 seconds | No `07 19` or model `27 20`; 93 other Apple adverts |
| Connection attempt | `br-connection-page-timeout` |

These did not prove radio silence. The case uses `07 11 06` plus a 16-byte encrypted block, with no cleartext model. It was already logged as an unparsed proximity payload. A filter for `07 19`, a pod-model check, or an IRK match could not identify it.

An nRF52840 sniffer was used to identify and confirm the format on 2026-09-09, followed by reception through the BT500. The evidence does not show that a sniffer was necessary.

Observed radio behavior was approximately one legacy `ADV_IND` per second on LE 1M, lid open or shut. During a 30-second comparison, the sniffer saw roughly that rate, six advertising reports reached HCI, and BlueZ delivered three distinct payloads to the app. This establishes different observed delivery rates, not which layer caused every missed update.

The case address did not resolve with the IRK. It survived a lid open and pod removal, then changed twice in an hour with the lid shut. Pod addresses rotated on lid opens; six appeared during one session. Both advertisers could arrive in the same second, and a second pair nearby demonstrated why the model filter alone was insufficient identity evidence.

## Sniffer notes

Historical setup details for reproducing the investigation, not newly verified installation instructions:

- Hardware: nRF52840 Dongle (PCA10059), Nordic nRF Sniffer 4.1.1. The recorded flashing command was `nrfutil device program --firmware x.zip --traits nordicDfu`, using a DFU ZIP built from `hex/sniffer_nrf52840dongle_nrf52840_4.1.1.hex`. USB ID after flashing: `1915:522a`.
- The extcap's `Filelock.py` tried `/var/lock/LCK..ttyACM0`, swallowed a permission error, and showed no interface. The investigation changed its lock location to `/tmp`; reference noted as Nordic DevZone 117671.
- Only `nrf_sniffer_ble.sh` should be executable in that extcap setup. An executable `.py` beside it registered the interface twice and opened the port twice.
- The interface name embedded the Wireshark version, for example `/dev/ttyACM0-4.7`. The Arch serial-device group was `uucp`, not `dialout`.
- This sniffer captures LE only. It cannot establish whether Bluetooth Classic traffic is present.
