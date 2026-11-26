# Zirarch: native Alpine and FieldM on SM-G550T

Zirarch is a reproducible source repository for the Samsung Galaxy On5
SM-G550T (`on5ltetmo`). It combines:

- A patched Samsung/Lineage Linux 3.10.108 Exynos 3475 kernel.
- A minimal native `switch_root` ramdisk.
- An Alpine 3.22 armv7/OpenRC root filesystem.
- The touch-first `FieldM` fork (`simple-desktop` and `simple-wm`).
- fbdev console, early RNDIS/OpenSSH, Wi-Fi, DECON refresh, and a TTY/GUI
  boot selector.

Generated images and proprietary device files are deliberately **not**
stored in Git. This repository contains pinned source revisions, patches,
and project overlays needed to reconstruct the build.

## Bootstrap sources

```sh
./bootstrap.sh
```

This checks out the exact kernel and Android ARM toolchain revisions from
`SOURCES.lock`, applies `patches/kernel-zirarch.patch`, and copies the kernel
overlay. FieldM is bundled directly in this repository and is rebuilt in
place.

To also fetch postmarketOS pmaports and install the local Zirarch port
recipes:

```sh
./bootstrap.sh --with-pmaports
```

## Build kernel, fbconsole, and FieldM

```sh
./build.sh
```

The default output is:

```text
kernel/arch/arm/boot/zImage
builds/zirarch-fbconsole
FieldM/simple-desktop
FieldM/simple-wm
```

Environment overrides:

```sh
CROSS_COMPILE=/path/to/arm-eabi- JOBS=8 ./build.sh
```

The historical ARM EABI 4.8 toolchain is pinned by `bootstrap.sh`; newer
toolchains are not compatible with every downstream kernel component.

## Build the native boot and root images

Provide device-specific inputs that cannot be redistributed:

```text
inputs/stock-on5-boot.img
on5-android-blobs/system/etc/wifi/bcmdhd_sta.bin
on5-android-blobs/system/etc/wifi/nvram_net.txt
```

The stock boot image supplies Samsung's DTBH container. Wi-Fi files can be
extracted from a connected Linux boot with:

```sh
SSH_IDENTITY=/path/to/device-key \
SSH_KNOWN_HOSTS=/path/to/known-hosts \
  kernel/extract-on5-wifi.sh
```

Prepare and image the native rootfs:

```sh
SSH_PUBLIC_KEY=/path/to/id_ed25519.pub \
  kernel/prepare-on5-native-rootfs.sh

kernel/build-on5-native-system.sh
kernel/build-on5-native-boot.sh
```

Outputs under `builds/` are ignored by Git:

```text
zirarch-boot.img
zirarch-native-rootfs.tar.gz
zirarch-alpine-system.img
```

## Reproduce FieldM on an ARM Alpine target

From an armv7 Alpine chroot/device with build dependencies installed:

```sh
cd FieldM
make clean
make
make check-applications
```

`simple-desktop` and `simple-wm` are the FieldM runtime pair. Local ARM
binary products (`*.armv7`) are ignored and are not repository inputs.

## Source layout

```text
patches/                  tracked changes against pinned upstreams
overlays/kernel/          Zirarch build/init/fbconsole/boot-mode sources
overlays/pmaports/        SM-G550T postmarketOS recipes
SOURCES.lock              exact upstream repository revisions
bootstrap.sh              source checkout and patch application
build.sh                  kernel/fbconsole/FieldM rebuild entrypoint
kernel/                   ignored bootstrapped kernel checkout
FieldM/                   tracked Zirarch mobile desktop source
```

Long-lived hardware and troubleshooting notes are in:

```text
kernel/knowledge.md
```

## Safety

The native install replaces `SYSTEM` only. It must not overwrite:

```text
RECOVERY
EFS / CPEFS
RADIO
bootloader partitions
```

Back up the exact live `BOOT` and `SYSTEM` partitions and verify TWRP access
before flashing. Keep generated `builds/`, `backups/`, extracted Android
blobs, stock boot images, SSH keys, and firmware outside commits.
