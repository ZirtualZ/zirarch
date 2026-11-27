# Agent handoff: SM-G550T Zirarch phone

Use this file as the operational guide for connecting, compiling, flashing,
and inspecting the connected Samsung Galaxy On5 SM-G550T.

## Bootstrap and build

From the repository root:

```sh
./bootstrap.sh
./build.sh
```

`build.sh` produces:

```text
kernel/arch/arm/boot/zImage
builds/zirarch-fbconsole
FieldM/simple-desktop
FieldM/simple-wm
```

The pinned ARM kernel compiler is:

```text
arm-eabi-4.8/bin/arm-eabi-
```

To rebuild only the kernel:

```sh
make -C kernel \
  ARCH=arm \
  CROSS_COMPILE="$PWD/arm-eabi-4.8/bin/arm-eabi-" \
  -j"$(nproc)" zImage
```

To build the native Alpine boot and SYSTEM images:

```sh
SSH_PUBLIC_KEY=/path/to/id_ed25519.pub \
  kernel/prepare-on5-native-rootfs.sh

kernel/build-on5-native-system.sh
kernel/build-on5-native-boot.sh
```

Outputs are under ignored `builds/`:

```text
builds/zirarch-boot.img
builds/zirarch-native-rootfs.tar.gz
builds/zirarch-alpine-system.img
```

## When Zirarch/Alpine is booted

The phone exposes RNDIS. Assign the host side `192.168.7.1/24`; the phone is
`192.168.7.2/24`.

```sh
ip -brief address show enp0s20f0u10
ping 192.168.7.2
ssh \
  -i ~/.ssh/zirarch_ed25519 \
  -o IdentitiesOnly=yes \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/tmp/zirarch-known-hosts \
  root@192.168.7.2
```

The host key and private-key paths above are local workspace conventions,
not repository files. Use whatever authenticated SSH key is provisioned on
the current host/phone.

Useful live checks:

```sh
uname -a
cat /proc/cmdline
cat /proc/mounts | grep ' / '
rc-status default
ip link
ip route
wifi scan
wifi status
fastfetch
apk add --no-cache htop
```

TTY/X/FieldM state:

```sh
rc-service zirarch-boot-mode status
rc-service zirarch-x11-refresh status
cat /run/zirarch-boot-mode
pgrep -a -x simple-desktop
pgrep -a -x simple-wm
DISPLAY=:0 xrandr -q
```

Input devices and framebuffer:

```sh
for f in /sys/class/input/event*/device/name; do
  echo "$f=$(cat "$f")"
done

cat /sys/class/graphics/fb0/virtual_size
cat /sys/class/graphics/fb0/bits_per_pixel
evtest /dev/input/event1
```

## ADB and TWRP

ADB only appears when the phone is in TWRP/recovery. Check it with:

```sh
adb devices -l
adb get-state
```

Expected state:

```text
recovery
```

To enter recovery manually when normal boot does not respond:

1. Hold Power + Volume Down to force power off.
2. Hold Volume Up + Home + Power.
3. Release into TWRP.

Run all destructive partition commands only after confirming TWRP:

```sh
adb shell 'id'
adb shell 'ls -l \
  /dev/block/mmcblk0p9 \
  /dev/block/mmcblk0p10 \
  /dev/block/mmcblk0p19 \
  /dev/block/mmcblk0p3 \
  /dev/block/mmcblk0p4 \
  /dev/block/mmcblk0p13'
```

Relevant partitions:

```text
mmcblk0p3   EFS
mmcblk0p4   CPEFS
mmcblk0p9   BOOT
mmcblk0p10  RECOVERY
mmcblk0p13  RADIO
mmcblk0p19  SYSTEM
```

Never write EFS, CPEFS, RADIO, RECOVERY, or bootloader partitions.

Back up live BOOT before flashing:

```sh
adb exec-out 'dd if=/dev/block/mmcblk0p9 bs=1M 2>/dev/null' \
  > backups/live-boot-$(date +%F-%H%M%S).img
```

Push the verified images:

```sh
adb push builds/zirarch-boot.img /data/zirarch-boot.img
adb push builds/zirarch-alpine-system.img.gz \
  /data/zirarch-alpine-system.img.gz
```

Create the compressed SYSTEM image when needed:

```sh
gzip -c builds/zirarch-alpine-system.img \
  > builds/zirarch-alpine-system.img.gz
```

Flash only `SYSTEM` and `BOOT`:

```sh
adb shell 'umount /system 2>/dev/null || true
  gzip -dc /data/zirarch-alpine-system.img.gz |
    dd of=/dev/block/mmcblk0p19 bs=4M conv=fsync
  dd if=/data/zirarch-boot.img \
    of=/dev/block/mmcblk0p9 bs=1M conv=fsync
  sync'
adb reboot
```

Verify written bytes against local SHA-256:

```sh
sha256sum builds/zirarch-alpine-system.img
adb shell 'dd if=/dev/block/mmcblk0p19 bs=4M 2>/dev/null | sha256sum'

BOOT_BYTES=$(stat -c%s builds/zirarch-boot.img)
adb shell \
  "dd if=/dev/block/mmcblk0p9 bs=4M 2>/dev/null |
   head -c $BOOT_BYTES | sha256sum"
sha256sum builds/zirarch-boot.img
```

## Recovery from a bad boot

Restore a fallback boot dump from TWRP:

```sh
adb push backups/twrp-pre-native-live-boot.img \
  /data/twrp-pre-native-live-boot.img
adb shell \
  'dd if=/data/twrp-pre-native-live-boot.img \
     of=/dev/block/mmcblk0p9 bs=1M conv=fsync
   sync'
adb reboot
```

Before experimenting, preserve the current image and check the result. Do not
flash `RECOVERY`, `RADIO`, EFS, or CPEFS.
