# Build image: compiler, BlueZ headers and sd-bus live here - nothing installs
# on the host. Arch matches the development machine's toolchain.
#
# dbus and python-dbusmock are for the discovery test, which runs the scanner
# against a fake BlueZ on a private bus rather than the machine's real one.
# gcovr reads what gcov leaves behind, for make coverage.
FROM archlinux:latest

RUN pacman -Syu --noconfirm --needed \
        base-devel clang cmake bluez-libs systemd-libs qt6-base qt6-svg openssl \
        dbus python-dbusmock gcovr \
    && rm -rf /var/cache/pacman/pkg

# A named user for the devcontainer to attach as, so files created on the bind
# mount belong to whoever is developing. The CLI moves it to the host's uid.
# make container-* passes -u and its own passwd instead, and is unaffected.
RUN useradd -m -u 1000 builder

WORKDIR /work
