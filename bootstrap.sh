#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
LOCK="$ROOT/SOURCES.lock"
INCLUDE_PMAPORTS=0

usage()
{
	echo "Usage: $0 [--with-pmaports]" >&2
	exit 1
}

while [ "$#" -gt 0 ]; do
	case "$1" in
		--with-pmaports) INCLUDE_PMAPORTS=1 ;;
		*) usage ;;
	esac
	shift
done

clone_pinned()
{
	commit=$1
	url=$2
	directory=$3
	path="$ROOT/$directory"

	if [ -d "$path/.git" ]; then
		:
	elif [ -e "$path" ]; then
		echo "$path exists but is not a Git checkout" >&2
		exit 1
	else
		git clone "$url" "$path"
	fi

	git -C "$path" cat-file -e "$commit^{commit}" 2>/dev/null ||
		git -C "$path" fetch origin "$commit"
	if [ "$(git -C "$path" rev-parse HEAD)" != "$commit" ]; then
		[ -z "$(git -C "$path" status --porcelain)" ] || {
			echo "$directory has local changes; refusing to switch commits" >&2
			exit 1
		}
		git -C "$path" checkout --detach "$commit"
	fi
}

apply_overlay()
{
	patch=$1
	directory=$2
	overlay=$3

	if git -C "$ROOT/$directory" apply --check "$patch" 2>/dev/null; then
		git -C "$ROOT/$directory" apply "$patch"
	elif git -C "$ROOT/$directory" apply --reverse --check "$patch" \
		2>/dev/null; then
		echo "$directory patch already applied"
	else
		echo "cannot apply $patch cleanly to $directory" >&2
		exit 1
	fi

	[ -d "$overlay" ] && cp -a "$overlay/." "$ROOT/$directory/"
}

while read -r commit url directory; do
	case "$commit" in
		''|'#'*) continue ;;
	esac
	case "$directory" in
		pmaports)
			[ "$INCLUDE_PMAPORTS" -eq 1 ] || continue
			;;
	esac
	clone_pinned "$commit" "$url" "$directory"
done < "$LOCK"

apply_overlay "$ROOT/patches/kernel-zirarch.patch" \
	kernel "$ROOT/overlays/kernel"

if [ "$INCLUDE_PMAPORTS" -eq 1 ]; then
	cp -a "$ROOT/overlays/pmaports/." "$ROOT/pmaports/"
fi

chmod 755 "$ROOT"/kernel/build-on5-*.sh \
	"$ROOT"/kernel/prepare-on5-*.sh \
	"$ROOT"/kernel/extract-on5-wifi.sh 2>/dev/null || true

mkdir -p "$ROOT/builds" "$ROOT/inputs"
echo "Bootstrap complete."
echo "Place a stock SM-G550T boot image at inputs/stock-on5-boot.img."
echo "Wi-Fi calibration remains device-specific; use kernel/extract-on5-wifi.sh."
