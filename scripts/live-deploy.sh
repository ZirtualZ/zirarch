#!/bin/sh
# Deploy and roll back FieldM binaries without rewriting phone partitions.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET=${TARGET_HOST:-root@192.168.7.2}
SSH_KEY=${SSH_KEY:-}
REMOTE_ROOT=/data/zirarch-live-deploy
BACKUP_ROOT=/data/zirarch-live-backups
LOG_ROOT=$ROOT/builds/live-deploy
MODE=deploy

while [ "$#" -gt 0 ]; do
	case "$1" in
		--rollback)
			MODE=rollback
			shift
			;;
		--rollback=*)
			MODE=rollback
			export ROLLBACK_NAME=${1#*=}
			shift
			;;
		-h|--help)
			echo "Usage: $0 [--rollback[=deployment-name]]"
			exit 0
			;;
		*)
			echo "live-deploy: unknown argument: $1" >&2
			exit 2
			;;
	esac
done

mkdir -p "$LOG_ROOT"

sha256_file()
{
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | awk '{print $1}'
	elif command -v shasum >/dev/null 2>&1; then
		shasum -a 256 "$1" | awk '{print $1}'
	else
		echo "live-deploy: sha256 tool is unavailable" >&2
		return 1
	fi
}

ssh_run()
{
	if [ -n "$SSH_KEY" ]; then
		ssh -o BatchMode=yes -o StrictHostKeyChecking=no \
			-o UserKnownHostsFile=/tmp/zirarch-known-hosts \
			-i "$SSH_KEY" -o IdentitiesOnly=yes "$TARGET" "$@"
	elif [ -r "$HOME/.ssh/zirarch_ed25519" ]; then
		ssh -o BatchMode=yes -o StrictHostKeyChecking=no \
			-o UserKnownHostsFile=/tmp/zirarch-known-hosts \
			-i "$HOME/.ssh/zirarch_ed25519" -o IdentitiesOnly=yes "$TARGET" "$@"
	else
		ssh -o BatchMode=yes -o StrictHostKeyChecking=no \
			-o UserKnownHostsFile=/tmp/zirarch-known-hosts "$TARGET" "$@"
	fi
}

scp_file()
{
	if [ -n "$SSH_KEY" ]; then
		scp -o BatchMode=yes -o StrictHostKeyChecking=no \
			-o UserKnownHostsFile=/tmp/zirarch-known-hosts \
			-i "$SSH_KEY" -o IdentitiesOnly=yes "$1" "$TARGET:$2"
	elif [ -r "$HOME/.ssh/zirarch_ed25519" ]; then
		scp -o BatchMode=yes -o StrictHostKeyChecking=no \
			-o UserKnownHostsFile=/tmp/zirarch-known-hosts \
			-i "$HOME/.ssh/zirarch_ed25519" -o IdentitiesOnly=yes "$1" "$TARGET:$2"
	else
		scp -o BatchMode=yes -o StrictHostKeyChecking=no \
			-o UserKnownHostsFile=/tmp/zirarch-known-hosts "$1" "$TARGET:$2"
	fi
}

ssh_untar_and_build()
{
	ssh_run mkdir -p "$REMOTE_ROOT/build/FieldM"
	source_archive=/tmp/zirarch-fieldm-source.tar
	tar -C "$ROOT/FieldM" --exclude='*.armv7' -cf "$source_archive" \
		Makefile src data tests
	scp_file "$source_archive" "$REMOTE_ROOT/FieldM-source.tar"
	ssh_run sh -c "set -e
		rm -rf '$REMOTE_ROOT/build/FieldM'
		mkdir -p '$REMOTE_ROOT/build/FieldM'
		tar -C '$REMOTE_ROOT/build/FieldM' -xf '$REMOTE_ROOT/FieldM-source.tar'
		test -f '$REMOTE_ROOT/build/FieldM/Makefile'
		cd '$REMOTE_ROOT/build/FieldM'
		make clean
		make -j1 all
	"
	rm -f "$source_archive"
}

rollback()
{
	BACKUP_NAME=${ROLLBACK_NAME:-}
	if [ -z "$BACKUP_NAME" ]; then
		BACKUP_NAME=$(ssh_run sh -c \
			"ls -1dt $BACKUP_ROOT/deploy-* 2>/dev/null | head -n 1 | xargs -r basename" \
			2>/dev/null | tr -d '\r')
	fi
	if [ -z "$BACKUP_NAME" ]; then
		echo "live-deploy: no target rollback backup found" >&2
		exit 1
	fi

	echo "live-deploy: rolling back $BACKUP_NAME"
	ssh_run sh -c "set -e
		dir=$BACKUP_ROOT/$BACKUP_NAME
		test -d \$dir
		rc-service zirarch-boot-mode stop >/dev/null 2>&1 || true
		rc-service fieldm-keys stop >/dev/null 2>&1 || true
		pkill -TERM -x simple-desktop 2>/dev/null || true
		pkill -TERM -x matchbox-window-manager 2>/dev/null || true
		pkill -TERM -x fieldm-kbd 2>/dev/null || true
		pkill -TERM -x Xorg 2>/dev/null || true
		sleep 2
		pkill -KILL -x simple-desktop 2>/dev/null || true
		pkill -KILL -x matchbox-window-manager 2>/dev/null || true
		pkill -KILL -x fieldm-kbd 2>/dev/null || true
		pkill -KILL -x Xorg 2>/dev/null || true
		if test -f \$dir/simple-desktop; then cp -p \$dir/simple-desktop /usr/bin/simple-desktop.rollback-new; mv -f /usr/bin/simple-desktop.rollback-new /usr/bin/simple-desktop; fi
		if test -f \$dir/fieldm-packages; then cp -p \$dir/fieldm-packages /usr/bin/fieldm-packages.rollback-new; mv -f /usr/bin/fieldm-packages.rollback-new /usr/bin/fieldm-packages; fi
		if test -f \$dir/fieldm-keys; then cp -p \$dir/fieldm-keys /usr/bin/fieldm-keys.rollback-new; mv -f /usr/bin/fieldm-keys.rollback-new /usr/bin/fieldm-keys; fi
		if test -f \$dir/fieldm-kbd; then cp -p \$dir/fieldm-kbd /usr/bin/fieldm-kbd.rollback-new; mv -f /usr/bin/fieldm-kbd.rollback-new /usr/bin/fieldm-kbd; fi
		if test -f \$dir/fieldm-session; then cp -p \$dir/fieldm-session /usr/bin/fieldm-session.rollback-new; mv -f /usr/bin/fieldm-session.rollback-new /usr/bin/fieldm-session; fi
		if test -f \$dir/fieldm-desktop-launch; then cp -p \$dir/fieldm-desktop-launch /usr/bin/fieldm-desktop-launch.rollback-new; mv -f /usr/bin/fieldm-desktop-launch.rollback-new /usr/bin/fieldm-desktop-launch; fi
		if test -f \$dir/fieldm-keyboard; then cp -p \$dir/fieldm-keyboard /usr/bin/fieldm-keyboard.rollback-new; mv -f /usr/bin/fieldm-keyboard.rollback-new /usr/bin/fieldm-keyboard; fi
		if test -f \$dir/fieldm-terminal; then cp -p \$dir/fieldm-terminal /usr/bin/fieldm-terminal.rollback-new; mv -f /usr/bin/fieldm-terminal.rollback-new /usr/bin/fieldm-terminal; fi
		if test -f \$dir/fieldm-imd; then cp -p \$dir/fieldm-imd /usr/bin/fieldm-imd.rollback-new; mv -f /usr/bin/fieldm-imd.rollback-new /usr/bin/fieldm-imd; fi
		if test -f \$dir/fieldm-imd.initd; then cp -p \$dir/fieldm-imd.initd /etc/init.d/fieldm-imd.rollback-new; mv -f /etc/init.d/fieldm-imd.rollback-new /etc/init.d/fieldm-imd; fi
		if test -f \$dir/zirarch-boot-mode; then cp -p \$dir/zirarch-boot-mode /usr/bin/zirarch-boot-mode.rollback-new; mv -f /usr/bin/zirarch-boot-mode.rollback-new /usr/bin/zirarch-boot-mode; fi
		if test -f \$dir/zirarch-boot-mode.initd; then cp -p \$dir/zirarch-boot-mode.initd /etc/init.d/zirarch-boot-mode.rollback-new; mv -f /etc/init.d/zirarch-boot-mode.rollback-new /etc/init.d/zirarch-boot-mode; fi
		if test -f \$dir/fieldm-keys.initd; then cp -p \$dir/fieldm-keys.initd /etc/init.d/fieldm-keys.rollback-new; mv -f /etc/init.d/fieldm-keys.rollback-new /etc/init.d/fieldm-keys; fi
		mkdir -p /run
		printf 'gui\n' > /run/zirarch-boot-mode
		sync
		rc-service fieldm-keys start || true
		rc-service zirarch-boot-mode start
	" </dev/null
	echo "live-deploy: rollback complete"
}

if [ "$MODE" = rollback ]; then
	rollback
	exit 0
fi

STAMP=$(date -u +%Y%m%dT%H%M%SZ)
DEPLOY_NAME=deploy-$STAMP
HOST_MANIFEST=$LOG_ROOT/$DEPLOY_NAME.host.txt
TARGET_MANIFEST=$LOG_ROOT/$DEPLOY_NAME.target.txt
ROLLBACK_MANIFEST=$LOG_ROOT/$DEPLOY_NAME.rollback.txt
: >"$HOST_MANIFEST"
: >"$TARGET_MANIFEST"
: >"$ROLLBACK_MANIFEST"

{
	echo "timestamp=$STAMP"
	echo "git_revision=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
	echo "screen_geometry=720x1280"
	echo "host_uname=$(uname -a)"
} >"$HOST_MANIFEST"

DEPLOY_FILES="
simple-desktop|remote:$REMOTE_ROOT/build/FieldM/simple-desktop|/usr/bin/simple-desktop
fieldm-packages|remote:$REMOTE_ROOT/build/FieldM/fieldm-packages|/usr/bin/fieldm-packages
fieldm-keys|remote:$REMOTE_ROOT/build/FieldM/fieldm-keys|/usr/bin/fieldm-keys
fieldm-kbd|remote:$REMOTE_ROOT/build/FieldM/fieldm-kbd|/usr/bin/fieldm-kbd
fieldm-session|$ROOT/overlays/kernel/boot-mode/fieldm-session|/usr/bin/fieldm-session
fieldm-desktop-launch|$ROOT/overlays/kernel/boot-mode/fieldm-desktop-launch|/usr/bin/fieldm-desktop-launch
fieldm-keyboard|$ROOT/overlays/kernel/boot-mode/fieldm-keyboard|/usr/bin/fieldm-keyboard
fieldm-terminal|$ROOT/overlays/kernel/boot-mode/fieldm-terminal|/usr/bin/fieldm-terminal
fieldm-imd|$ROOT/overlays/kernel/boot-mode/fieldm-imd|/usr/bin/fieldm-imd
fieldm-imd.initd|$ROOT/overlays/kernel/boot-mode/fieldm-imd.initd|/etc/init.d/fieldm-imd
back-keys.conf|$ROOT/overlays/kernel/boot-mode/back-keys.conf|/etc/fieldm/back-keys.conf
zirarch-boot-mode|$ROOT/overlays/kernel/boot-mode/zirarch-boot-mode|/usr/bin/zirarch-boot-mode
zirarch-boot-mode.initd|$ROOT/overlays/kernel/boot-mode/zirarch-boot-mode.initd|/etc/init.d/zirarch-boot-mode
fieldm-keys.initd|$ROOT/overlays/kernel/boot-mode/fieldm-keys.initd|/etc/init.d/fieldm-keys
"

ssh_untar_and_build

MISSING=
OLDIFS=$IFS
IFS='
'
for row in $DEPLOY_FILES; do
	IFS='|' read -r name source destination <<EOF
$row
EOF
	[ -n "$name" ] || continue
	case "$source" in
		remote:*)
			remote_path=${source#remote:}
			sum=$(ssh_run sha256sum "$remote_path" 2>/dev/null | awk '{print $1}')
			if [ -z "$sum" ]; then
				echo "live-deploy: missing target build $remote_path" >&2
				MISSING=1
				continue
			fi
			;;
		*)
			if [ ! -f "$source" ]; then
				echo "live-deploy: missing $source" >&2
				MISSING=1
				continue
			fi
			sum=$(sha256_file "$source") || exit 1
			;;
	esac
	printf 'host_sha256 %s %s %s\n' "$name" "$destination" "$sum" >>"$HOST_MANIFEST"
done
IFS=$OLDIFS
[ -z "$MISSING" ] || exit 1

ssh_run mkdir -p "$REMOTE_ROOT" "$BACKUP_ROOT/$DEPLOY_NAME" >/dev/null

ssh_run sh -c "set -e
	rc-service zirarch-boot-mode stop >/dev/null 2>&1 || true
	rc-service fieldm-keys stop >/dev/null 2>&1 || true
	pkill -TERM -x simple-desktop 2>/dev/null || true
	pkill -TERM -x matchbox-window-manager 2>/dev/null || true
	pkill -TERM -x fieldm-kbd 2>/dev/null || true
	pkill -TERM -x Xorg 2>/dev/null || true
	sleep 2
	pkill -KILL -x simple-desktop 2>/dev/null || true
	pkill -KILL -x fieldm-kbd 2>/dev/null || true
	pkill -KILL -x Xorg 2>/dev/null || true
" >/dev/null

for row in $DEPLOY_FILES; do
	IFS='|' read -r name source destination <<EOF
$row
EOF
	[ -n "$name" ] || continue
	ssh_run sh -c "set -e
		if test -e '$destination'; then
			cp -p '$destination' '$BACKUP_ROOT/$DEPLOY_NAME/$name'
		fi
		mkdir -p '$(dirname "$destination")'
	" >/dev/null
	case "$source" in
		remote:*)
			remote_path=${source#remote:}
			;;
		*)
			scp_file "$source" "$REMOTE_ROOT/$name.new"
			remote_path=$REMOTE_ROOT/$name.new
			;;
	esac
	ssh_run sh -c "set -e
		chmod 755 '$remote_path'
		mv -f '$remote_path' '$destination'
	" >/dev/null
done

ssh_run sh -c "set -e
	echo 'timestamp=$STAMP'
	echo 'deployment=$DEPLOY_NAME'
	uname -a
	cat /proc/cmdline
	if command -v apk >/dev/null 2>&1; then
		apk info -v 2>/dev/null | sort
	elif command -v dpkg-query >/dev/null 2>&1; then
		dpkg-query -W -f='\${Package}=\${Version}\n' 2>/dev/null
	fi
	rc-status 2>/dev/null || true
" >"$TARGET_MANIFEST"

for row in $DEPLOY_FILES; do
	IFS='|' read -r name source destination <<EOF
$row
EOF
	[ -n "$name" ] || continue
	target_sum=$(ssh_run sha256sum "$destination" 2>/dev/null | awk '{print $1}')
	host_sum=$(awk -v name="$name" '$1=="host_sha256" && $2==name {print $4}' "$HOST_MANIFEST")
	if [ -z "$target_sum" ] || [ "$target_sum" != "$host_sum" ]; then
		echo "live-deploy: checksum mismatch after deploy: $destination" >&2
		exit 1
	fi
	printf 'target_sha256 %s %s %s\n' "$name" "$destination" "$target_sum" >>"$TARGET_MANIFEST"
done

cp "$HOST_MANIFEST" "$LOG_ROOT/$DEPLOY_NAME.rollback.txt"
{
	echo "rollback_deployment=$DEPLOY_NAME"
	echo "rollback_backup=$BACKUP_ROOT/$DEPLOY_NAME"
} >>"$ROLLBACK_MANIFEST"

ssh_run sh -c "set -e
	mkdir -p /run
	printf 'gui\n' > /run/zirarch-boot-mode
	sync
	rc-service fieldm-keys start
	rc-service zirarch-boot-mode start
" >/dev/null

echo "live-deploy: deployed $DEPLOY_NAME"
