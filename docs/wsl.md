# Fetching keys from WSL

Key extraction needs BlueZ and an L2CAP control link, so it is Linux-only. A Windows-only machine can fetch keys through WSL2 by forwarding its Bluetooth adapter over USB/IP.

Verified against WSL2 kernel 6.18.33.2-microsoft-standard-WSL2, Ubuntu 24.04 and an ASUS USB-BT500 (Realtek RTL8761BU, `0b05:190e`).

Use a Linux machine if one is available; WSL2 requires the extra setup below.

## Requirements

The tested WSL2 kernel includes `CONFIG_BT=m`, `CONFIG_BT_BREDR=y`, `CONFIG_BT_HCIBTUSB=m` and `CONFIG_USBIP_VHCI_HCD=m`, with `bluetooth.ko`, `btusb.ko` and `vhci-hcd.ko` shipped. The `AF_BLUETOOTH` SEQPACKET sockets opened by [`l2cap_socket.hpp`](../src/bt/l2cap_socket.hpp) work once a radio is attached. The distro needs systemd running because device lookup in [`bluez_devices.hpp`](../src/bt/bluez_devices.hpp) reaches BlueZ over the D-Bus system bus.

Install the Linux packages in WSL:

```sh
sudo apt install bluez linux-firmware hwdata linux-tools-generic
```

Install [usbipd-win](https://github.com/dorssel/usbipd-win) on Windows:

```powershell
winget install --exact dorssel.usbipd-win
```

The tested setup needed the USB/IP and firmware fixes below. The USBPcap fix applies only if USBPcap is installed.

### Point usbip at the installed binary

`/usr/bin/usbip` is a wrapper that looks for a build matching the running kernel. No such package exists for WSL kernels, so it reports `usbip not found for kernel 6.18.33.2-microsoft` and names packages that are not in the Ubuntu archive. The binary installed by `linux-tools-generic` works against the tested WSL kernel; point at it directly.

```sh
sudo ln -sf /usr/lib/linux-tools/*/usbip /usr/local/bin/usbip
```

### Make the adapter firmware available

The BT500 has two firmware-loading problems, both reported as `Direct firmware load for rtl_bt/rtl8761bu_fw.bin failed with error -2`.

Ubuntu ships firmware zstd-compressed, and the WSL kernel sets `CONFIG_FW_LOADER_COMPRESS_XZ=y` but not `CONFIG_FW_LOADER_COMPRESS_ZSTD`. The kernel tries the uncompressed name first, so decompressing alongside is enough:

```sh
sudo apt install zstd
cd /lib/firmware/rtl_bt
for f in rtl8761*.zst; do sudo zstd -q -d "$f" -o "${f%.zst}"; done
```

The firmware loader also runs in WSL's init mount namespace, where the distro's `/lib/firmware` does not exist. `/mnt/wsl` is shared across namespaces, so stage the firmware there and point the loader at it:

```sh
sudo mkdir -p /mnt/wsl/firmware/rtl_bt
sudo cp /lib/firmware/rtl_bt/rtl8761bu_{fw,config}.bin /mnt/wsl/firmware/rtl_bt/
echo -n /mnt/wsl/firmware | sudo tee /sys/module/firmware_class/parameters/path
```

Both the staged copy and the path reset when the WSL VM restarts.

### Remove the USBPcap filter if installed

If [USBPcap](https://desowin.org/usbpcap/) is installed, it registers as an `UpperFilters` entry on the USB device class, and usbipd's stub driver never takes over the device. `usbipd bind --force` appears to succeed and the device is renamed to `USBIP Shared Device`, but `usbipd state` shows an empty `StubInstanceId` and every attach fails with `Device busy (exported)`.

Save the value, remove it, and replug the adapter. The filter only detaches on re-enumeration.

```powershell
# elevated
$k = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\{36fc9e60-c465-11cf-8056-444553540000}"
(Get-ItemProperty $k -Name UpperFilters).UpperFilters | Set-Content "$env:USERPROFILE\usb-upperfilters-backup.txt"
Remove-ItemProperty -Path $k -Name UpperFilters
```

To restore it afterwards, write the saved value back as a `MultiString` and start the `USBPcap` service again.

## Attach the adapter

Run `usbipd list` to find the adapter's bus ID; the BT500 has USB ID `0b05:190e`. Replace `4-15` below and in the cleanup commands with your bus ID.

```powershell
usbipd bind --force --busid 4-15
usbipd attach --wsl --busid 4-15
```

Windows cannot use the adapter while it is attached to WSL. Confirm the radio is running:

```sh
hciconfig -a
```

A real `BD Address` and `UP RUNNING` mean the firmware loaded. All-zero address with `DOWN` means it did not; check `dmesg | grep RTL`.

The attach does not survive `wsl --shutdown`, a replug or a reboot. `usbipd bind` persists; `usbipd attach` must be repeated.

## Preserve the Windows pairing

Pairing under BlueZ negotiates a new link key, and the buds keep only one per adapter address. Since the same physical adapter serves both systems, pairing in WSL invalidates the key Windows holds and audio there stops working until it is paired again.

Copy the existing Windows link key to BlueZ to preserve the pairing. Windows keeps it in `HKLM\SYSTEM\CurrentControlSet\Services\BTHPORT\Parameters\Keys\<adapter>\<device>`, readable only as `SYSTEM`, using PsExec or a one-shot scheduled task running as `SYSTEM`. BlueZ wants the same 16 bytes as hex in `/var/lib/bluetooth/<ADAPTER>/<DEVICE>/info`, with both addresses uppercase and colon-separated:

```ini
[General]
Name=AirPods
Class=0x240418
SupportedTechnologies=BR/EDR;
Trusted=true
Blocked=false

[LinkKey]
Key=<32 hex characters>
Type=4
PINLength=0
```

Stop `bluetoothd` before writing because it rewrites its store on shutdown, then start it again. `bluetoothctl info <MAC>` should report `Paired: yes` and `Bonded: yes` without any pairing having taken place.

## Fetch the keys

The binary from `make container-build` is linked against the Arch image's libraries and will not run on Ubuntu; a native WSL build is not an option either, because Ubuntu 24.04 carries Qt 6.4.2 and the UI needs `<QtLogging>` from Qt 6.5. Run it in the build container instead. `--net=host` gives the container the host's Bluetooth sockets; mounting the system bus socket lets it reach BlueZ.

Put the buds **in your ears** first. Out of the case is not enough: they sleep within seconds, and the connect then fails with `Host is down`.

```sh
bluetoothctl connect <MAC>

docker run --rm --net=host \
  -v /run/dbus/system_bus_socket:/run/dbus/system_bus_socket \
  -v "/mnt/c/Users/<user>/AppData/Local/airpods:/keys" \
  -v "$PWD:/work" -w /work \
  airpods-build ./build/release/airpods --keys \
    --address <MAC> \
    --key-file /keys/proximity-keys
```

Writing straight to `%LOCALAPPDATA%\airpods\` puts the file where `airpods.exe` reads it, and keeps it out of the repository.

`--address` is not optional here. `PickAirPods` in [`bluez_devices.hpp`](../src/bt/bluez_devices.hpp) skips devices BlueZ does not report as connected, and BlueZ drops the link with `br-connection-profile-unavailable` because WSL has no audio stack to satisfy an A2DP or HFP profile. The ACL link itself works; `--address` skips the lookup and connects directly.

Do not add `--debug`. It prints whole received packets, key replies included.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| `Device busy (exported)` on attach | USBPcap still filtering; remove it and replug |
| `hci0` present, `DOWN`, all-zero address | Firmware not found; check compression and namespace |
| `connect(...): Host is down` | Buds asleep or on another host; wear them and retry |
| `No key reply` | Request outran the handshake; fixed by waiting for `IsReady()` |
| `The AirPods must be out of the case and connected` | Device lookup found nothing connected; pass `--address` |

## Cleanup

```powershell
usbipd detach --busid 4-15
usbipd unbind --busid 4-15
```

Then restore `UpperFilters` if USBPcap was unhooked. The adapter returns to Windows as a Bluetooth radio on its own; if it does not, replug it.
