# Zirarch: native Alpine, Debian, and FieldM on SM-G550T

Zirarch is a reproducible source repository for the Samsung Galaxy On5
SM-G550T (`on5ltetmo`). It combines:

- A patched Samsung/Lineage Linux 3.10.108 Exynos 3475 kernel.
- A minimal native `switch_root` ramdisk.
- An Alpine 3.22 armv7/OpenRC root filesystem.
- An experimental Debian 13 armhf/OpenRC root filesystem.
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

## Debian rootfs (experimental)

Debian is a second rootfs option for the same native boot/initramfs path.
It does not change or remove the known-good Alpine image. The Debian build
uses `debootstrap` for a minbase armhf `trixie` foreign stage and
qemu-user-static for the second stage. `sysvinit-core` owns `/sbin/init`;
OpenRC runs under it. The script refuses to image a rootfs with systemd as
PID 1.

On an x86_64 Linux host, install `debootstrap`, qemu-user-static ARM binfmt
support, and `e2fsprogs`, then make the On5 Wi-Fi blobs and SSH public key
available. The preparation script must run as root because its qemu second
stage, package maintainer scripts, and final ownership-preserving archive
need a real chroot:

```sh
sudo env SSH_PUBLIC_KEY=/path/to/id_ed25519.pub \
  kernel/prepare-on5-debian-rootfs.sh
sudo kernel/build-on5-debian-system.sh
```

The preparation script installs the requested lean Debian/OpenRC/Xorg
stack, `elogind`, OpenSSH, RNDIS, Wi-Fi, zram, and FieldM build
dependencies. It builds FieldM and the Zirarch C helpers natively in the
armhf chroot and runs `make check-applications`. `onboard` is the Debian X11
on-screen keyboard equivalent (Alpine continues to use `svkbd`).
`GSK_RENDERER=cairo` remains mandatory for the FieldM X11 session.

Outputs are:

```text
builds/zirarch-debian-rootfs.tar.gz
builds/zirarch-debian-system.img
```

The system image is an ext4 image of exactly `3061841920` bytes, with the
64-bit and metadata-checksum features disabled for Linux 3.10. Both
scripts report rootfs/filesystem usage and the imager verifies
`/sbin/init` in the image. The current native boot image needs no Debian
initramfs-specific changes: `init-native` mounts SYSTEM and executes its
`/sbin/init`. Rebuild the native boot kernel after bootstrap so the
documented built-in zram device is present, using the normal
`kernel/build-on5-native-boot.sh` flow.

This stage is for Debian parity work only. Do not start libhybris work
until boot, SSH over USB, Wi-Fi, and FieldM have all been verified on the
phone.

### Connecting over USB RNDIS

The rootfs configures Samsung's legacy `android_usb` gadget (not configfs)
with vendor `04e8`, RNDIS product `6863`, and the phone address
`192.168.7.2/24`. The `usb-rndis` OpenRC service starts before SSH.

On an Arch host:

```sh
ip link
# Find the new usb0/enp*u* interface.
sudo ip addr add 192.168.7.1/24 dev <iface>
sudo ip link set <iface> up
ssh -i /path/to/device-key root@192.168.7.2
```

Set these variables when using the device helper scripts:

```sh
SSH_TARGET=root@192.168.7.2
SSH_IDENTITY=/path/to/device-key
SSH_KNOWN_HOSTS=/path/to/known-hosts
```

To give the phone temporary Internet access for apt:

```sh
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o <host-uplink> -j MASQUERADE
sudo iptables -A FORWARD -i <iface> -j ACCEPT
sudo iptables -A FORWARD -o <iface> \
  -m state --state RELATED,ESTABLISHED -j ACCEPT
```

The image uses `1.1.1.1` and `8.8.8.8` in `/etc/resolv.conf`. It disables
password and keyboard-interactive root SSH; install the authorized key
through `SSH_PUBLIC_KEY` when preparing the image.

### Debian flash and Alpine rollback

Do not flash these images automatically. Before any flash, enter TWRP,
confirm an `adb shell` opens in recovery, and back up the live partitions
to the host. Only `BOOT` (`mmcblk0p9`) and `SYSTEM` (`mmcblk0p19`) may be
written:

```sh
mkdir -p backups
adb reboot recovery
adb wait-for-recovery
adb exec-out dd if=/dev/block/mmcblk0p9 \
  bs=1M > backups/live-boot.img
adb exec-out dd if=/dev/block/mmcblk0p19 \
  bs=1M > backups/live-system.img
adb exec-out dd if=/dev/block/mmcblk0p3 \
  bs=1M > backups/live-efs.img
sha256sum backups/live-{boot,system,efs}.img
```

After the backups are complete and TWRP has been confirmed working, the
Debian SYSTEM replacement is:

```sh
adb push builds/zirarch-debian-system.img \
  /data/zirarch-debian-system.img
adb shell \
  'dd if=/data/zirarch-debian-system.img \
      of=/dev/block/mmcblk0p19 bs=4M conv=fsync; sync'
adb reboot
```

Rollback restores the known-good Alpine SYSTEM image with the exact same
partition target:

```sh
adb push builds/zirarch-alpine-system.img \
  /data/zirarch-alpine-system.img
adb shell \
  'dd if=/data/zirarch-alpine-system.img \
      of=/dev/block/mmcblk0p19 bs=4M conv=fsync; sync'
adb reboot
```

Never write RECOVERY (`mmcblk0p10`), CPEFS, EFS, RADIO, bootloader
partitions, or any other partition. Keep `builds/zirarch-alpine-system.img`
and its SHA-256 unchanged as the rollback artifact.

## Reproduce FieldM on an ARM Alpine target

From an armv7 Alpine chroot/device with build dependencies installed:

```sh
cd FieldM
make clean
make
make check-applications
```

`simple-desktop` and `simple-wm` are the FieldM runtime pair. Local ARM
binary products (`*.armv7`) are ignored and are not repository inputs. The
Debian rootfs instead builds the binaries with glibc inside its armhf
chroot.

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
