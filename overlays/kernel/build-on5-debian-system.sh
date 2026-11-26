#!/bin/sh
set -eu

KERNEL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$KERNEL_DIR/.." && pwd)
ROOTFS_ARCHIVE=${ROOTFS_ARCHIVE:-"$PROJECT_DIR/builds/zirarch-debian-rootfs.tar.gz"}
WORK_DIR=${WORK_DIR:-"$PROJECT_DIR/builds/debian-armhf-rootfs"}
ROOT_DIR=${ROOT_DIR:-"$WORK_DIR/image-root"}
OUTPUT=${OUTPUT:-"$PROJECT_DIR/builds/zirarch-debian-system.img"}
SYSTEM_BYTES=${SYSTEM_BYTES:-3061841920}
LABEL=${LABEL:-SYSTEM}
QEMU_ARM=${QEMU_ARM:-/usr/bin/qemu-arm-static}

die()
{
	echo "error: $*" >&2
	exit 1
}

[ "$(id -u)" -eq 0 ] || die "run this script as root"
[ -s "$ROOTFS_ARCHIVE" ] || die "missing Debian rootfs archive: $ROOTFS_ARCHIVE"
[ -x "$QEMU_ARM" ] || die "missing qemu-arm-static: $QEMU_ARM"
for tool in tar truncate mke2fs e2fsck debugfs stat du sha256sum; do
	command -v "$tool" >/dev/null 2>&1 || die "missing host tool: $tool"
done
case "$ROOT_DIR" in
	"$PROJECT_DIR"/builds/*/image-root) ;;
	*) die "unsafe image root directory: $ROOT_DIR" ;;
esac

mkdir -p "$WORK_DIR" "$(dirname "$OUTPUT")"
[ ! -e "$ROOT_DIR" ] || rm -rf "$ROOT_DIR"
mkdir -p "$ROOT_DIR"
tar --same-owner --numeric-owner -xzf "$ROOTFS_ARCHIVE" -C "$ROOT_DIR"

install -m 755 "$QEMU_ARM" "$ROOT_DIR/usr/bin/qemu-arm-static"
init_target=$(chroot "$ROOT_DIR" /usr/bin/qemu-arm-static \
	/usr/bin/readlink -f /sbin/init)
rm -f "$ROOT_DIR/usr/bin/qemu-arm-static"
case "$init_target" in
	/*) [ -x "$ROOT_DIR$init_target" ] ||
		die "/sbin/init target is not executable: $init_target" ;;
	*) die "/sbin/init has no absolute target: $init_target" ;;
esac
init_relative=$init_target
init_list=$(grep -lFx "$init_relative" \
	"$ROOT_DIR"/var/lib/dpkg/info/*.list 2>/dev/null | head -1 || true)
[ -n "$init_list" ] || die "sysvinit package ownership for /sbin/init not found"
case "$init_list" in
	*/sysvinit-core.list|*/sysvinit*.list) ;;
	*) die "/sbin/init target is not owned by sysvinit: $init_list" ;;
esac
for systemd_init in \
	"$ROOT_DIR/lib/systemd/systemd" \
	"$ROOT_DIR/usr/lib/systemd/systemd"; do
	[ ! -x "$systemd_init" ] ||
		die "systemd is present; refusing to image it as PID 1"
done

root_bytes=$(du -sx --block-size=1 "$ROOT_DIR" | cut -d '	' -f 1)
[ "$root_bytes" -lt "$SYSTEM_BYTES" ] ||
	die "rootfs ($root_bytes bytes) does not fit SYSTEM ($SYSTEM_BYTES bytes)"

rm -f "$OUTPUT"
truncate -s "$SYSTEM_BYTES" "$OUTPUT"
mke2fs -q -F -t ext4 -L "$LABEL" \
	-O ^64bit,^metadata_csum -d "$ROOT_DIR" "$OUTPUT"
e2fsck -pf "$OUTPUT"

[ "$(stat -c %s "$OUTPUT")" -eq "$SYSTEM_BYTES" ] ||
	die "system image has the wrong size"
debugfs -R 'stat /sbin/init' "$OUTPUT" 2>/dev/null |
	grep -q 'Inode:' ||
	die "/sbin/init is missing from the ext4 image"

block_size=$(dumpe2fs -h "$OUTPUT" 2>/dev/null |
	awk -F: '/^Block size:/ {gsub(/[[:space:]]/, "", $2); print $2}')
block_count=$(dumpe2fs -h "$OUTPUT" 2>/dev/null |
	awk -F: '/^Block count:/ {gsub(/[[:space:]]/, "", $2); print $2}')
free_blocks=$(dumpe2fs -h "$OUTPUT" 2>/dev/null |
	awk -F: '/^Free blocks:/ {gsub(/[[:space:]]/, "", $2); print $2}')
case "$block_size:$block_count:$free_blocks" in
	*[!0-9:]*|:*) die "could not read ext4 usage" ;;
esac
used_bytes=$(((block_count - free_blocks) * block_size))

sha256sum "$OUTPUT"
echo "Debian system image: $OUTPUT"
echo "Exact partition bytes: $(stat -c %s "$OUTPUT")"
echo "Used ext4 bytes: $used_bytes / $SYSTEM_BYTES"
echo "/sbin/init: sysvinit target $init_relative"
du -h "$OUTPUT"
ls -lh "$OUTPUT"
