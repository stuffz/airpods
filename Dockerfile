# Build image: compiler, BlueZ headers and sd-bus live here - nothing installs
# on the host. Arch matches the development machine's toolchain.
FROM archlinux:latest

RUN pacman -Syu --noconfirm --needed \
        base-devel clang bluez-libs systemd-libs qt6-base qt6-svg openssl \
    && rm -rf /var/cache/pacman/pkg

WORKDIR /work
