#!/bin/sh
set -eu

KERNEL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$KERNEL_DIR/.." && pwd)
KERNEL_IMAGE=${KERNEL_IMAGE:-"$KERNEL_DIR/arch/arm/boot/zImage"}
BUSYBOX=${BUSYBOX:-"$PROJECT_DIR/builds/minimal/busybox-armv7l"}
INIT_SCRIPT=${INIT_SCRIPT:-"$KERNEL_DIR/initramfs-on5/init-native"}
WIFI_FIRMWARE_DIR=${WIFI_FIRMWARE_DIR:-"$PROJECT_DIR/on5-android-blobs/system/etc/wifi"}
DEVICE_FIRMWARE_DIR=${DEVICE_FIRMWARE_DIR:-"$PROJECT_DIR/on5-android-blobs/system/etc/firmware"}
WORK_DIR=${WORK_DIR:-"$PROJECT_DIR/builds/native-initramfs"}
OUTPUT=${OUTPUT:-"$PROJECT_DIR/builds/zirarch-boot.img"}
DEBUG_CONSOLE=${DEBUG_CONSOLE:-1}

mkdir -p "$WORK_DIR"

[ -f "$KERNEL_IMAGE" ] || {
	echo "missing kernel: $KERNEL_IMAGE" >&2
	exit 1
}
[ -f "$BUSYBOX" ] ||
	BUSYBOX_ONLY=1 "$KERNEL_DIR/build-on5-minimal.sh"

for firmware in bcmdhd_sta.bin nvram_net.txt; do
	[ -s "$WIFI_FIRMWARE_DIR/$firmware" ] || {
		echo "missing stock Wi-Fi blob: $WIFI_FIRMWARE_DIR/$firmware" >&2
		exit 1
	}
done
for firmware in audience-es804-fw.bin audience-es804-data.bin; do
	[ -s "$DEVICE_FIRMWARE_DIR/$firmware" ] || {
		echo "missing stock EarSmart firmware: $firmware" >&2
		exit 1
	}
done

"$PROJECT_DIR/arm-eabi-4.8/bin/arm-eabi-gcc" \
	-Os -nostdlib -static -fno-builtin -fno-stack-protector \
	-Wl,-e,_start -o "$WORK_DIR/initfb" \
	"$KERNEL_DIR/initramfs-on5/initfb.c"
python3 "$KERNEL_DIR/initramfs-on5/boot-splash.py" \
	"$WORK_DIR/boot-splash.bgra"

cat > "$WORK_DIR/initramfs.list" <<EOF
dir /bin 0755 0 0
dir /dev 0755 0 0
dir /lib 0755 0 0
dir /lib/firmware 0755 0 0
dir /newroot 0755 0 0
dir /proc 0755 0 0
dir /run 0755 0 0
dir /sys 0755 0 0
dir /vendor 0755 0 0
dir /vendor/etc 0755 0 0
dir /vendor/etc/wifi 0755 0 0
nod /dev/console 0600 0 0 c 5 1
nod /dev/null 0666 0 0 c 1 3
file /bin/busybox $BUSYBOX 0755 0 0
file /bin/initfb $WORK_DIR/initfb 0755 0 0
file /boot-splash.bgra $WORK_DIR/boot-splash.bgra 0644 0 0
file /init $INIT_SCRIPT 0755 0 0
file /lib/firmware/audience-es804-fw.bin $DEVICE_FIRMWARE_DIR/audience-es804-fw.bin 0644 0 0
file /lib/firmware/audience-es804-data.bin $DEVICE_FIRMWARE_DIR/audience-es804-data.bin 0644 0 0
file /vendor/etc/wifi/bcmdhd_sta.bin $WIFI_FIRMWARE_DIR/bcmdhd_sta.bin 0644 0 0
file /vendor/etc/wifi/nvram_net.txt $WIFI_FIRMWARE_DIR/nvram_net.txt 0644 0 0
slink /bin/sh busybox 0755 0 0
EOF

"$KERNEL_DIR/usr/gen_init_cpio" "$WORK_DIR/initramfs.list" |
	gzip -9 > "$WORK_DIR/initramfs.cpio.gz"

SKIP_BUILD=1 \
KERNEL_IMAGE="$KERNEL_IMAGE" \
RAMDISK="$WORK_DIR/initramfs.cpio.gz" \
CMDLINE="console=tty0 console=ttySAC2,115200 consoleblank=0 loglevel=4 androidboot.selinux=permissive" \
DEBUG_CONSOLE="$DEBUG_CONSOLE" \
DEBUG_ADB=0 \
OUTPUT="$OUTPUT" \
	"$KERNEL_DIR/build-on5-boot.sh"

echo "native boot image: $OUTPUT"
