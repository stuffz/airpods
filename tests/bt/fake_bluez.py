#!/usr/bin/env python3

"""A fake BlueZ on a private bus, driven a line at a time.

python-dbusmock's bluez5 template already answers the adapter interface the
scanner talks to. What it does not have is a way to reach in from C++ and say
"now lose the adapter", so this wraps it in a command loop: the test writes one
command per line and reads one reply per line.

Every typed D-Bus value the scanner has to decode is built here rather than in
the test, which is the whole reason this is a separate process.

Print "ready <bus address>" on startup, then answer "ok [value]" or "error
<reason>" per command. See tst_discovery.cpp beside it for the commands in use.
"""

import os
import sys

import dbus
import dbusmock

APPLE_VENDOR_ID = 0x004C
ADAPTER_IFACE = "org.bluez.Adapter1"
DEVICE_IFACE = "org.bluez.Device1"
MOCK_IFACE = "org.freedesktop.DBus.Mock"
ADAPTER_NAME = "hci0"

# BlueZ answers this when the caller already holds a discovery session.
IN_PROGRESS = "org.bluez.Error.InProgress"


class FakeBluez:
    def __init__(self):
        dbusmock.DBusTestCase.start_system_bus()
        # The mock server logs every call it serves. That goes to stderr, where
        # ctest shows it on failure, because stdout carries the replies.
        self.server, self.bluez = dbusmock.DBusTestCase.spawn_server_template(
            "bluez5", {}, stdout=sys.stderr
        )
        self.bus = dbusmock.DBusTestCase.get_dbus(system_bus=True)
        self.adapter_path = None

    def add_adapter(self):
        self.adapter_path = str(self.bluez.AddAdapter(ADAPTER_NAME, "fake"))
        return self.adapter_path

    def remove_adapter(self):
        self.bluez.RemoveAdapter(ADAPTER_NAME)
        self.adapter_path = None

    # Powered and Discovering both reach the scanner as PropertiesChanged,
    # which is how the real adapter reports them.
    def set_flag(self, name, value):
        self.mock(self.adapter_path).UpdateProperties(
            ADAPTER_IFACE, {name: dbus.Boolean(value)}
        )

    # Changing the property without the signal that normally accompanies it,
    # which is the state only a recheck can notice. dbusmock runs the code
    # string with the mock object as self, so this reaches props directly.
    def set_flag_quietly(self, name, value):
        adapter = self.mock(self.adapter_path)
        adapter.AddMethod(
            ADAPTER_IFACE,
            "SetQuietly",
            "sb",
            "",
            f'self.props["{ADAPTER_IFACE}"][args[0]] = bool(args[1])',
        )
        dbus.Interface(
            self.bus.get_object("org.bluez", self.adapter_path), ADAPTER_IFACE
        ).SetQuietly(name, value)

    def read_flag(self, name):
        properties = dbus.Interface(
            self.bus.get_object("org.bluez", self.adapter_path),
            "org.freedesktop.DBus.Properties",
        )
        return bool(properties.Get(ADAPTER_IFACE, name))

    def fail_start_discovery(self):
        self.mock(self.adapter_path).AddMethod(
            ADAPTER_IFACE,
            "StartDiscovery",
            "",
            "",
            f'raise dbus.exceptions.DBusException("already discovering", name="{IN_PROGRESS}")',
        )

    # An advertisement is a device whose ManufacturerData changed. The nesting
    # is a{qv} inside a variant, which is what the scanner walks by hand.
    def advertise(self, address, payload):
        path = str(self.bluez.AddDevice(ADAPTER_NAME, address, "Fake"))
        data = dbus.Array([dbus.Byte(b) for b in payload], signature="y", variant_level=1)
        self.mock(path).UpdateProperties(
            DEVICE_IFACE,
            {"ManufacturerData": dbus.Dictionary({dbus.UInt16(APPLE_VENDOR_ID): data}, signature="qv")},
        )
        return path

    def mock(self, path):
        return dbus.Interface(self.bus.get_object("org.bluez", path), MOCK_IFACE)

    # The private bus outlives this process unless it is stopped here, and a
    # daemon still holding the inherited stderr is a test run that never ends.
    def stop(self):
        self.server.terminate()
        self.server.wait()
        dbusmock.DBusTestCase.tearDownClass()


def main():
    fake = FakeBluez()
    try:
        reply(f"ready {os.environ['DBUS_SYSTEM_BUS_ADDRESS']}")
        serve(fake)
    finally:
        fake.stop()


def serve(fake):
    for line in sys.stdin:
        command, _, argument = line.strip().partition(" ")

        if command in ("quit", ""):
            return

        try:
            reply(f"ok {run(fake, command, argument)}".strip())
        except Exception as error:  # noqa: BLE001 - the reply is the report
            reply(f"error {type(error).__name__}: {error}")


def run(fake, command, argument):
    if command == "add-adapter":
        return fake.add_adapter()
    if command == "remove-adapter":
        fake.remove_adapter()
        return ""
    if command == "powered":
        fake.set_flag("Powered", argument == "on")
        return ""
    if command == "discovering":
        fake.set_flag("Discovering", argument == "on")
        return ""
    if command == "discovering-quietly":
        fake.set_flag_quietly("Discovering", argument == "on")
        return ""
    if command == "discovering?":
        return "on" if fake.read_flag("Discovering") else "off"
    if command == "fail-start-discovery":
        fake.fail_start_discovery()
        return ""
    if command == "advertise":
        address, _, payload = argument.partition(" ")
        return fake.advertise(address, bytes.fromhex(payload))

    raise ValueError(f"unknown command {command}")


def reply(text):
    sys.stdout.write(text + "\n")
    sys.stdout.flush()


if __name__ == "__main__":
    main()
