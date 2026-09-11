# AirPods

A Qt system tray app for AirPods battery levels, noise control and BLE lid detection on Linux, with experimental battery-only support on Windows. It reads battery reports over the connected control link and BLE broadcasts, including the case's closed-lid advertisements. Support depends on the model and firmware; recorded BLE testing used AirPods Pro 3.

## Build and run

Requires Linux, Docker and Make to build. Running requires BlueZ, a Bluetooth adapter, Qt 6 Widgets/SVG, OpenSSL, libsystemd and a desktop system tray. The container uses Arch Linux; the host needs compatible runtime libraries.

```sh
make container-build
./build/release/airpods
```

Pair and connect the buds through your desktop's Bluetooth settings for noise control and connected battery reports. Close other clients holding the AirPods control link, such as LibrePods.

Fetch BLE keys once while the buds are connected, then restart the app:

```sh
./build/release/airpods --keys
```

Keys enable identifying your pods and decrypting exact BLE battery levels. Keep the key file private and do not combine `--keys` with `--debug`, which logs key replies.

Other modes:

```sh
./build/release/airpods --console
./build/release/airpods --once
./build/release/airpods --ble
./build/release/airpods --help
```

## Windows

Build with `make container-build-windows`; the executable is `build/windows/airpods.exe`. Windows uses BLE advertisements and keys fetched on Linux:

```powershell
.\airpods.exe --key-file "R:\airpods-keys.conf"
```

Replace `R:` with your shared drive letter. Noise control and key extraction remain Linux-only. See [development](docs/development.md#windows-battery-build) for paths and hardware checks.

With no Linux machine to fetch keys on, WSL2 can do it by forwarding the Bluetooth adapter over USB/IP. See [WSL](docs/wsl.md); it is the long way round.

## Documentation

- [BLE](docs/ble.md): advertisements, identity and battery freshness.
- [L2CAP](docs/l2cap.md): connection, handshake and control packets.
- [BLE research](docs/ble-research.md): captures and remaining hypotheses.
- [Development](docs/development.md): checks and UI debugging.
- [WSL](docs/wsl.md): fetching keys on a Windows-only machine.

Code is licensed under [MIT](LICENSE). Noun Project icons retain their [CC BY 3.0 attribution and license](ATTRIBUTION.md).
