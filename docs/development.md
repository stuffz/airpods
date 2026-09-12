# Development

## Layout

| Path | Purpose |
| --- | --- |
| `src/bt/` | BlueZ, sockets, BLE scanning and cryptography |
| `src/aap/` | Protocol packets, parsers and session state |
| `src/core/` | Application coordination, options and persistence |
| `src/ui/` | Qt widgets and embedded artwork |
| `tests/` | QTest suites, laid out to mirror `src/`, with fixtures in `tests/data/` |
| `.devcontainer/` | Devcontainer definition over the same build image |
| `resources/pods/` | Original pod download and adapted SVGs |
| `resources/linux/` | Desktop integration |
| `packaging/arch/` | Arch package definition |

## Build and checks

Use the repository's container targets; do not install build dependencies on the host. Check formatting before lint, then build:

```sh
make container-format-check
make container-lint
make container-test
make container-build
```

The same toolchain is available as a devcontainer over the same `Dockerfile`, for running the plain targets without a `docker run` wrapper each time:

```sh
devcontainer up
devcontainer exec make test
```

Inside it, use `make test`, `make lint`, `make format-check` and `make coverage`; the `container-*` targets start a container of their own and would need a docker socket in there to work. It mounts the repository at `/work`, the same path `make container-*` uses, and sets `TEST_DIR` and `COVERAGE_DIR` to the same directories, so the two share their build output instead of invalidating each other's CMake cache. It attaches as `builder`, which the CLI moves to the host uid so files on the bind mount belong to whoever is developing.

The Makefile passes `GIT_HASH` from the host because the build image does not contain Git. The Arch-based image and host must have compatible runtime libraries; a successful container build alone does not verify that the app runs on the host.

The development binary is `build/release/airpods`. An installed copy may also exist, so check which binary is running before evaluating a change. `make package` builds from committed HEAD and installs the Arch package; it does not package uncommitted edits.

## Tests

`make container-test` configures the CMake project, builds one executable per `tests/*/tst_*.cpp` and runs them through CTest. The directories mirror `src/`, and each test is registered under that path, so `ctest -R core` runs one layer of them. Most are pure parsers and decisions. Two are not: `tst_discovery` runs the scanner against a fake BlueZ, and the decisions extracted into [status_line.hpp](../src/core/status_line.hpp) and [link_retry.hpp](../src/core/link_retry.hpp) exist as separate headers so the fault precedence and the wedged-link backoff can be checked without an event loop or a ten minute wait. The plain Makefile build does not reach them: QTest needs moc, which `CMAKE_AUTOMOC` provides and the Makefile has no rule for. Formatting and lint cover `tests/` on the same terms as `src/`, so `make container-lint` builds the suite first: every test includes moc output that exists only in the build directory, and clang-tidy reads it through the compile database rather than a flag list. `tests/.clang-tidy` turns off the three checks that QTest defeats, and explains each.

Host and container use separate build directories, `build/test` and `build/test-container`, because a CMake cache records the absolute path it was configured at and the container's is `/work`. `make container-test` also writes `compile_commands.json` at the repository root with those paths rewritten, which is what clangd and editors should read; `compile_flags.txt` remains as the fallback before anything is built, and cannot resolve the generated `.moc` includes on its own.

`tst_discovery` needs no Bluetooth hardware. [fake_bluez.py](../tests/bt/fake_bluez.py) starts a private system bus, puts python-dbusmock's `bluez5` template on it and takes one command per line, so the test can remove the adapter, power it off, stop discovery quietly or make `StartDiscovery` answer `org.bluez.Error.InProgress`. The scanner reaches it through `DBUS_SYSTEM_BUS_ADDRESS`, which is why nothing touches the machine's real BlueZ. `dbus-daemon` refuses to serve a uid it cannot name and the image has no entry for the host user, so the Makefile generates a one-line passwd to mount rather than exposing the host's.

`make container-coverage` builds the suite with `--coverage` in its own directory and reports covered lines per file. Read the columns, not the total: most of `src/ui` is Qt painting and most of `src/bt` is socket work, so a single percentage across all of `src/` measures the shape of the program rather than the tests. What the report is for is the files at zero that had no business being there.

Fixtures in `tests/data/captures.hpp` are the decrypted blocks recorded in [BLE research evidence](ble-research.md), resealed under a published test key: a real advertisement carries that block encrypted under the owner's key, which is not in the repository. The pods' cleartext bytes there are assembled from the field table in [BLE](ble.md), not recorded, and say so.

## Windows battery build

```sh
make container-build-windows
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
