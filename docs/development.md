# Development

## Layout

| Path | Purpose |
| --- | --- |
| `src/bt/` | BlueZ, sockets, BLE scanning and cryptography |
| `src/aap/` | Protocol packets, parsers and session state |
| `src/core/` | Application coordination, options and persistence |
| `src/ui/` | Qt widgets and embedded artwork |
| `resources/pods/` | Original pod download and adapted SVGs |
| `resources/linux/` | Desktop integration |
| `packaging/arch/` | Arch package definition |

## Build and checks

Use the repository's container targets; do not install build dependencies on the host. Check formatting before lint, then build:

```sh
make container-format-check
make container-lint
make container-build
```

The Makefile passes `GIT_HASH` from the host because the build image does not contain Git. The Arch-based image and host must have compatible runtime libraries; a successful container build alone does not verify that the app runs on the host.

The development binary is `build/release/airpods`. An installed copy may also exist, so check which binary is running before evaluating a change. `make package` builds from committed HEAD and installs the Arch package; it does not package uncommitted edits.

## Windows battery build

```sh
make container-build-windows
make container-test
```

Output: `build/windows/airpods.exe`. The container builds static Qt 6.11.1 and uses C++/WinRT 2.0.250303.1 with Windows SDK contracts 10.0.26100.3916. Linux and Windows share the Qt UI, battery parsers, AES and storage formats; Bluetooth backends differ. Windows supports the tray and `--ble`; control-link commands require Linux.

On Windows, replace `R:` with the shared drive letter:

```powershell
.\airpods.exe --key-file "R:\airpods-keys.conf"
.\airpods.exe --ble --key-file "R:\airpods-keys.conf"
```

Without `--key-file`, Windows loads `%LOCALAPPDATA%/airpods/proximity-keys`. Cached readings are in `%LOCALAPPDATA%/airpods/last-battery`. The key file format is unchanged; `.conf` is an optional extension. `--keys --key-file PATH` writes to that path on Linux.

`make wine-smoke` runs startup regression tests with host Wine and synthetic keys in a temporary prefix. It checks the build timestamp and filenames containing spaces or non-ASCII characters. Wine 11.17 cannot run the BLE watcher because its scanning-mode setter is unimplemented.

Keys still have to come from Linux, but not necessarily from a Linux machine: WSL2 can fetch them by forwarding the adapter over USB/IP. See [WSL](wsl.md) for the adapter, firmware and pairing work that needs.

Hardware checks still needed: exact left/right/case readings, closed case, audio playback, rotating pod addresses, Bluetooth off/on, and tray rendering. A stopped Windows scanner is restarted in place rather than ending the app, which is compile-checked only and still wants a run on real hardware. Avoid `--debug` when sharing logs because it prints advertisement identifiers and payloads.

## UI inspection

The overview paints SVG paths directly through `QSvgRenderer`, at the widget's display resolution. Keep this vector rendering path: scaling an intermediate bitmap softened the icons, particularly on scaled displays. Both pods use the extracted right-pod shape, mirrored for the left; the case reuses the tray glyph. Preserve aspect ratios when fitting artwork into the 72-pixel icon area.

`resources/pods/right.svg` and the embedded pod paths in `src/ui/pod_artwork.hpp` must stay in sync. `left.svg` is its mirrored asset, and `case.svg` corresponds to `AirPodsGlyph()` in `src/ui/airpods_glyph.hpp`. Keep the original download and [attribution](../ATTRIBUTION.md) with the adapted assets.

Qt registers the tray item under a unique D-Bus name, not necessarily `org.kde.StatusNotifierItem-PID-N`. Inspect `IconPixmap` on `org.kde.StatusNotifierItem` for the rendered ARGB icon, or call `Activate` to open its window. These are manual inspection techniques, not automated UI tests.

On Wayland, `setDesktopFileName("airpods")` must match the installed `airpods.desktop`; the compositor obtains the window icon through that desktop entry. See [main.cpp](../src/main.cpp).

For connection diagnostics and the single-client control-link limitation, see [L2CAP](l2cap.md). For radio captures and battery freshness, see [BLE](ble.md).

## Open work

- Autostart and a preferences UI are not implemented.
- Tray artwork uses fixed light colours; visibility on light panels needs checking.
- The L2CAP ear-detection prefix exists but is not dispatched.
