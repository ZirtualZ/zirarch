#!/bin/sh
set -eu

cd "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

[ "$(id -u)" -eq 0 ] || {
	echo "Run this script as root: sudo ./build-native-on5.sh" >&2
	exit 1
}

if [ -n "${SUDO_USER:-}" ]; then
	USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
else
	USER_HOME=$HOME
fi
SSH_KEY=${SSH_KEY:-"$USER_HOME/.ssh/zirarch_ed25519"}
SSH_PUBLIC_KEY=${SSH_PUBLIC_KEY:-"$SSH_KEY.pub"}

[ -s "$SSH_KEY" ] || {
	echo "SSH private key is missing: $SSH_KEY" >&2
	exit 1
}
[ -s "$SSH_PUBLIC_KEY" ] || ssh-keygen -y -f "$SSH_KEY" > "$SSH_PUBLIC_KEY"

./build.sh
env SSH_PUBLIC_KEY="$SSH_PUBLIC_KEY" \
	kernel/prepare-on5-native-rootfs.sh
kernel/build-on5-native-system.sh
kernel/build-on5-native-boot.sh

sha256sum \
	builds/zirarch-boot.img \
	builds/zirarch-native-rootfs.tar.gz \
	builds/zirarch-alpine-system.img
