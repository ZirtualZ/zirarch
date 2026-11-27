#!/bin/sh
set -eu

cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

[ "$(id -u)" -eq 0 ] || {
	echo "Run this script as root: sudo ./build-debian-on5.sh" >&2
	exit 1
}

if [ -n "${SUDO_USER:-}" ]; then
	USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
else
	USER_HOME=$HOME
fi
SSH_KEY=${SSH_KEY:-"$USER_HOME/.ssh/zirarch_ed25519"}
SSH_PUBLIC_KEY=${SSH_PUBLIC_KEY:-"$SSH_KEY.pub"}
mkdir -p "$(dirname "$SSH_KEY")"
chmod 700 "$(dirname "$SSH_KEY")"
[ -s "$SSH_KEY" ] || ssh-keygen -q -t ed25519 -N '' -f "$SSH_KEY"
[ -s "$SSH_PUBLIC_KEY" ] || ssh-keygen -y -f "$SSH_KEY" > "$SSH_PUBLIC_KEY"
[ -s "$SSH_PUBLIC_KEY" ] || {
	echo "SSH public key is missing: $SSH_PUBLIC_KEY" >&2
	echo "Set SSH_PUBLIC_KEY=/path/to/id_ed25519.pub" >&2
	exit 1
}
[ -n "${ZIRARCH_ROOT_PASSWORD:-}" ] || {
	echo "ZIRARCH_ROOT_PASSWORD is required for root SSH password login" >&2
	exit 1
}

if command -v pacman >/dev/null 2>&1; then
	pacman -S --needed --noconfirm \
		debootstrap qemu-user-static-binfmt qemu-user-static e2fsprogs
elif command -v apt-get >/dev/null 2>&1; then
	apt-get update
	apt-get install -y debootstrap qemu-user-static binfmt-support e2fsprogs
elif command -v dnf >/dev/null 2>&1; then
	dnf install -y debootstrap qemu-user-static binfmt-support e2fsprogs
else
	echo "Unsupported package manager; install debootstrap, qemu-user-static, binfmt support, and e2fsprogs" >&2
	exit 1
fi

./build.sh

env SSH_PUBLIC_KEY="$SSH_PUBLIC_KEY" \
	ZIRARCH_ROOT_PASSWORD="$ZIRARCH_ROOT_PASSWORD" \
	kernel/prepare-on5-debian-rootfs.sh

kernel/build-on5-debian-system.sh
kernel/build-on5-native-boot.sh

sha256sum \
	builds/zirarch-boot.img \
	builds/zirarch-debian-rootfs.tar.gz \
	builds/zirarch-debian-system.img
