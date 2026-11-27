#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

[ -d "$ROOT/kernel/.git" ] || {
	echo "Run ./bootstrap.sh first." >&2
	exit 1
}

CROSS_COMPILE=${CROSS_COMPILE:-"$ROOT/arm-eabi-4.8/bin/arm-eabi-"}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}

make -C "$ROOT/kernel" \
	ARCH=arm \
	CROSS_COMPILE="$CROSS_COMPILE" \
	lineage-on5ltetmo_defconfig
make -C "$ROOT/kernel" \
	ARCH=arm \
	CROSS_COMPILE="$CROSS_COMPILE" \
	-j"$JOBS" \
	zImage

make -C "$ROOT/FieldM" clean
make -C "$ROOT/FieldM" -j"$JOBS"
make -C "$ROOT/FieldM" check-applications

"$ROOT/kernel/build-on5-fbconsole.sh"

echo "Kernel: $ROOT/kernel/arch/arm/boot/zImage"
echo "FieldM: $ROOT/FieldM/simple-desktop, simple-wm, and fieldm-packages"
