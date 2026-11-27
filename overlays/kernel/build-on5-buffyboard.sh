#!/bin/sh
set -eu

KERNEL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$KERNEL_DIR/.." && pwd)
SOURCE_TAG=${SOURCE_TAG:-3.2.0}
SOURCE_URL=${SOURCE_URL:-https://gitlab.postmarketos.org/postmarketOS/buffybox.git}
SSH_TARGET=${SSH_TARGET:-root@192.168.7.2}
SSH_IDENTITY=${SSH_IDENTITY:-$HOME/.ssh/zirarch_ed25519}
SSH_KNOWN_HOSTS=${SSH_KNOWN_HOSTS:-/tmp/zirarch-native-known-hosts}
OUTPUT=${OUTPUT:-"$PROJECT_DIR/builds/zirarch-buffyboard"}
PATCH_FILE="$KERNEL_DIR/buffyboard/legacy-uinput.patch"

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT INT TERM

git clone -q --branch "$SOURCE_TAG" --depth 1 "$SOURCE_URL" "$TMP_DIR/buffybox"
git -C "$TMP_DIR/buffybox" submodule update --init --recursive
patch -d "$TMP_DIR/buffybox" -p1 < "$PATCH_FILE"

SSH_ARGS="-i $SSH_IDENTITY -o IdentitiesOnly=yes -o BatchMode=yes \
-o StrictHostKeyChecking=no -o UserKnownHostsFile=$SSH_KNOWN_HOSTS"

# shellcheck disable=SC2086
ssh $SSH_ARGS "$SSH_TARGET" \
	'apk add --no-cache meson ninja inih-dev libinput-dev eudev-dev \
		gcc musl-dev >/dev/null &&
	 rm -rf /root/buffyboard-build &&
	 mkdir -p /root/buffyboard-build'

(
	cd "$TMP_DIR"
	tar -czf - buffybox
) | ssh $SSH_ARGS "$SSH_TARGET" \
	'tar -xzf - -C /root/buffyboard-build'

# shellcheck disable=SC2086
ssh $SSH_ARGS "$SSH_TARGET" \
	'cd /root/buffyboard-build/buffybox/buffyboard &&
	 meson setup build &&
	 meson compile -C build &&
	 test -x build/buffyboard'

# shellcheck disable=SC2086
ssh $SSH_ARGS "$SSH_TARGET" \
	'cat /root/buffyboard-build/buffybox/buffyboard/build/buffyboard' > "$OUTPUT"
chmod 755 "$OUTPUT"
file "$OUTPUT"
sha256sum "$OUTPUT"
