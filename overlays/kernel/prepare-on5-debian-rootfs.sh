#!/bin/sh
set -eu

KERNEL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$KERNEL_DIR/.." && pwd)
WORK_DIR=${WORK_DIR:-"$PROJECT_DIR/builds/debian-armhf-rootfs"}
ROOT_DIR="$WORK_DIR/root"
DEBIAN_SUITE=${DEBIAN_SUITE:-trixie}
MIRROR=${MIRROR:-http://deb.debian.org/debian}
QEMU_ARM=${QEMU_ARM:-/usr/bin/qemu-arm-static}
ANDROID_BLOBS=${ANDROID_BLOBS:-"$PROJECT_DIR/on5-android-blobs/system"}
SSH_PUBLIC_KEY=${SSH_PUBLIC_KEY:-}
ZIRARCH_ROOT_PASSWORD=${ZIRARCH_ROOT_PASSWORD:-}
OUTPUT_ARCHIVE=${OUTPUT_ARCHIVE:-"$PROJECT_DIR/builds/zirarch-debian-rootfs.tar.gz"}
SYSTEM_BYTES=${SYSTEM_BYTES:-3061841920}

die()
{
	echo "error: $*" >&2
	exit 1
}

[ "$(id -u)" -eq 0 ] || die "run this script as root"
[ "$(uname -m)" = x86_64 ] || die "the foreign debootstrap host must be x86_64"
for tool in debootstrap mountpoint readlink du install chroot; do
	command -v "$tool" >/dev/null 2>&1 || die "missing host tool: $tool"
done
[ -x "$QEMU_ARM" ] || die "missing qemu-arm-static: $QEMU_ARM"
[ -d /proc/sys/fs/binfmt_misc ] || die "binfmt_misc is unavailable"
find /proc/sys/fs/binfmt_misc -maxdepth 1 -type f -name 'qemu-arm*' \
	-exec grep -lx enabled {} + | grep -q . ||
	die "arm binfmt is not enabled; install qemu-user-static binfmt support"
for firmware in bcmdhd_sta.bin nvram_net.txt; do
	[ -s "$ANDROID_BLOBS/etc/wifi/$firmware" ] ||
		die "missing stock Wi-Fi blob: $ANDROID_BLOBS/etc/wifi/$firmware"
done
for firmware in audience-es804-fw.bin audience-es804-data.bin; do
	[ -s "$ANDROID_BLOBS/etc/firmware/$firmware" ] ||
		die "missing stock EarSmart firmware: $firmware"
	install -m 644 "$ANDROID_BLOBS/etc/firmware/$firmware" \
		"$ROOT_DIR/lib/firmware/$firmware"
done

case "$ROOT_DIR" in
	"$PROJECT_DIR"/builds/*/root) ;;
	*) die "unsafe root directory: $ROOT_DIR" ;;
esac

mkdir -p "$WORK_DIR"
mkdir -p "$(dirname "$OUTPUT_ARCHIVE")"
[ ! -e "$ROOT_DIR" ] || rm -rf "$ROOT_DIR"
mkdir -p "$ROOT_DIR"

mounted=0
cleanup()
{
	if [ "$mounted" -eq 1 ]; then
		umount "$ROOT_DIR/dev/pts" 2>/dev/null || true
		umount "$ROOT_DIR/dev" 2>/dev/null || true
		umount "$ROOT_DIR/proc" 2>/dev/null || true
		umount "$ROOT_DIR/sys" 2>/dev/null || true
		mounted=0
	fi
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$ROOT_DIR/etc/apt" "$ROOT_DIR/etc/dpkg/dpkg.cfg.d"
cat > "$ROOT_DIR/etc/dpkg/dpkg.cfg.d/01-zirarch" <<'EOF'
path-exclude=/usr/share/doc/*
path-exclude=/usr/share/man/*
path-exclude=/usr/share/info/*
path-exclude=/usr/share/locale/*
EOF

debootstrap \
	--foreign \
	--arch=armhf \
	--variant=minbase \
	--exclude=systemd-sysv \
	"$DEBIAN_SUITE" "$ROOT_DIR" "$MIRROR"
install -m 755 "$QEMU_ARM" "$ROOT_DIR/usr/bin/qemu-arm-static"

mount -t proc proc "$ROOT_DIR/proc"
mount -t sysfs sysfs "$ROOT_DIR/sys"
mount --bind /dev "$ROOT_DIR/dev"
mount --bind /dev/pts "$ROOT_DIR/dev/pts"
mounted=1

chroot "$ROOT_DIR" /debootstrap/debootstrap --second-stage

PACKAGES="sysvinit-core openrc elogind openssh-server ifupdown
	wpasupplicant iw iproute2 isc-dhcp-client
	xserver-xorg-core xserver-xorg-video-fbdev xserver-xorg-input-evdev
	xinit x11-xserver-utils x11-utils dbus dbus-x11
	libgtk-4-dev libgtk-3-dev libwebkit2gtk-4.1-dev libx11-dev libxft-dev libxtst-dev pkg-config build-essential
	matchbox-window-manager \
	at-spi2-core python3-pyatspi libatk-adaptor \
	gsettings-desktop-schemas gir1.2-atspi-2.0 \
	xterm kbd console-data zram-tools
	ntpsec-ntpdate fake-hwclock util-linux-extra pcmanfm xclip scrot \
	haveged e2fsprogs ca-certificates nano"

chroot "$ROOT_DIR" /usr/bin/env \
	DEBIAN_FRONTEND=noninteractive APT_LISTCHANGES_FRONTEND=none \
	/usr/bin/apt-get update
chroot "$ROOT_DIR" /usr/bin/env \
	DEBIAN_FRONTEND=noninteractive \
	/usr/bin/apt-get install -y --no-install-recommends $PACKAGES

[ -x "$ROOT_DIR/sbin/init" ] || die "Debian rootfs has no /sbin/init"
init_target=$(chroot "$ROOT_DIR" /usr/bin/readlink -f /sbin/init)
case "$init_target" in
	/*) ;;
	*) die "/sbin/init has no absolute target: $init_target" ;;
esac
[ -x "$ROOT_DIR$init_target" ] ||
	die "/sbin/init target is missing: $init_target"
init_relative=$init_target
init_owner=$(chroot "$ROOT_DIR" dpkg-query -S "$init_relative" 2>/dev/null || true)
case "$init_owner" in
	*systemd*) die "/sbin/init is owned by systemd" ;;
	*sysvinit*) ;;
	*) die "could not confirm sysvinit owns /sbin/init: $init_owner" ;;
esac
for systemd_init in \
	"$ROOT_DIR/lib/systemd/systemd" \
	"$ROOT_DIR/usr/lib/systemd/systemd"; do
	[ ! -x "$systemd_init" ] ||
		die "systemd is installed; Debian must boot with sysvinit/OpenRC"
done

mkdir -p "$ROOT_DIR/root/.ssh" "$ROOT_DIR/etc/ssh/sshd_config.d" \
	"$ROOT_DIR/etc/X11/xorg.conf.d" "$ROOT_DIR/etc/wpa_supplicant" \
	"$ROOT_DIR/etc/init.d" "$ROOT_DIR/usr/src/zirarch" \
	"$ROOT_DIR/usr/share/applications" "$ROOT_DIR/run/sshd"
chmod 700 "$ROOT_DIR/root" "$ROOT_DIR/root/.ssh"
chmod 755 "$ROOT_DIR/run/sshd"

cp -a "$PROJECT_DIR/FieldM" "$ROOT_DIR/usr/src/zirarch/FieldM"
cp -a "$KERNEL_DIR/fbconsole" "$ROOT_DIR/usr/src/zirarch/fbconsole"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-hide-keyboard.c" \
	"$ROOT_DIR/usr/src/zirarch/fieldm-hide-keyboard.c"
install -m 644 "$KERNEL_DIR/boot-mode/zirarch-x11-refresh.c" \
	"$ROOT_DIR/usr/src/zirarch/zirarch-x11-refresh.c"
chroot "$ROOT_DIR" /usr/bin/make -C /usr/src/zirarch/FieldM clean
chroot "$ROOT_DIR" /usr/bin/make -C /usr/src/zirarch/FieldM -j2
chroot "$ROOT_DIR" /usr/bin/make -C /usr/src/zirarch/FieldM \
	check-applications
chroot "$ROOT_DIR" /usr/bin/make -C /usr/src/zirarch/FieldM fieldm-browser
chroot "$ROOT_DIR" /usr/bin/make -C /usr/src/zirarch/FieldM fieldm-lock
chroot "$ROOT_DIR" /usr/bin/cc -O2 -Wall -Wextra -std=c11 -o \
	/usr/bin/zirarch-fbconsole /usr/src/zirarch/fbconsole/fbconsole.c
chroot "$ROOT_DIR" /usr/bin/cc -O2 -Wall -Wextra -std=c11 -o \
	/usr/bin/zirarch-x11-refresh /usr/src/zirarch/zirarch-x11-refresh.c
install -m 755 "$ROOT_DIR/usr/src/zirarch/FieldM/simple-desktop" \
	"$ROOT_DIR/usr/bin/simple-desktop"
install -m 755 "$ROOT_DIR/usr/src/zirarch/FieldM/fieldm-packages" \
	"$ROOT_DIR/usr/bin/fieldm-packages"
install -m 755 "$ROOT_DIR/usr/src/zirarch/FieldM/fieldm-keys" \
	"$ROOT_DIR/usr/bin/fieldm-keys"
install -m 755 "$ROOT_DIR/usr/src/zirarch/FieldM/fieldm-kbd" \
	"$ROOT_DIR/usr/bin/fieldm-kbd"
install -m 755 "$ROOT_DIR/usr/src/zirarch/FieldM/fieldm-browser" \
	"$ROOT_DIR/usr/bin/fieldm-browser"
install -m 755 "$ROOT_DIR/usr/src/zirarch/FieldM/fieldm-lock" \
	"$ROOT_DIR/usr/bin/fieldm-lock"
install -m 644 "$PROJECT_DIR/FieldM/data/fieldm.css" \
	"$ROOT_DIR/usr/share/fieldm/fieldm.css"

install -m 755 "$KERNEL_DIR/wifi" "$ROOT_DIR/usr/bin/wifi"
install -m 755 "$KERNEL_DIR/fbconsole/screen-console.initd" \
	"$ROOT_DIR/etc/init.d/screen-console"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-boot-mode.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-boot-mode"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-boot-mode" \
	"$ROOT_DIR/usr/bin/zirarch-boot-mode"
install -m 755 "$PROJECT_DIR/FieldM/scripts/fieldm-hwprobe" \
	"$ROOT_DIR/usr/lib/fieldm/hwprobe"
install -m 755 "$PROJECT_DIR/FieldM/scripts/zirarch-post" \
	"$ROOT_DIR/usr/bin/zirarch-post"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-session" \
	"$ROOT_DIR/usr/bin/fieldm-session"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-desktop-launch" \
	"$ROOT_DIR/usr/bin/fieldm-desktop-launch"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-keyboard" \
	"$ROOT_DIR/usr/bin/fieldm-keyboard"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-storage" \
	"$ROOT_DIR/usr/bin/fieldm-storage"
chroot "$ROOT_DIR" /usr/bin/cc -O2 -Wall -Wextra -std=c11 -o \
	/usr/bin/fieldm-hide-keyboard \
	/usr/src/zirarch/fieldm-hide-keyboard.c
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-terminal" \
	"$ROOT_DIR/usr/bin/fieldm-terminal"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-logs.initd" \
	"$ROOT_DIR/etc/init.d/fieldm-logs"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-notify" \
	"$ROOT_DIR/usr/bin/fieldm-notify"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-power" \
	"$ROOT_DIR/usr/bin/zirarch-power"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-battery" \
	"$ROOT_DIR/usr/bin/zirarch-battery"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-battery.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-battery"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-power.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-power"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-early-boot.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-early-boot"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-haveged.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-haveged"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-browser" \
	"$ROOT_DIR/usr/bin/zirarch-browser"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-clock-sync" \
	"$ROOT_DIR/usr/bin/zirarch-clock-sync"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-clock-sync.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-clock-sync"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-clock.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-clock"
mkdir -p "$ROOT_DIR/etc/fieldm"
install -m 644 "$PROJECT_DIR/FieldM/terminal.desktop" \
	"$ROOT_DIR/usr/share/applications/terminal.desktop"
install -m 644 "$KERNEL_DIR/boot-mode/zirarch-browser.desktop" \
	"$ROOT_DIR/usr/share/applications/zirarch-browser.desktop"
install -m 644 "$PROJECT_DIR/FieldM/data/packages.desktop" \
	"$ROOT_DIR/usr/share/applications/packages.desktop"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-x11-refresh.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-x11-refresh"
sed -i \
	-e '/need devfs/d' \
	-e '/need udev/d' \
	-e 's/after usb-gadget/after usb-rndis/' \
	-e 's/after local networking/after usb-rndis networking/' \
	"$ROOT_DIR/etc/init.d/screen-console" \
	"$ROOT_DIR/etc/init.d/zirarch-boot-mode" \
	"$ROOT_DIR/etc/init.d/zirarch-x11-refresh"

mkdir -p "$ROOT_DIR/lib/firmware" "$ROOT_DIR/vendor/etc/wifi"
for firmware in bcmdhd_sta.bin nvram_net.txt; do
	install -m 644 "$ANDROID_BLOBS/etc/wifi/$firmware" \
		"$ROOT_DIR/lib/firmware/$firmware"
	install -m 644 "$ANDROID_BLOBS/etc/wifi/$firmware" \
		"$ROOT_DIR/vendor/etc/wifi/$firmware"
done

cat > "$ROOT_DIR/etc/hostname" <<'EOF'
on5-debian
EOF
cat >> "$ROOT_DIR/etc/rc.conf" <<'EOF'

rc_logger="YES"
EOF
cat > "$ROOT_DIR/etc/hosts" <<'EOF'
127.0.0.1 localhost
127.0.1.1 on5-debian
::1 localhost
EOF
cat > "$ROOT_DIR/etc/resolv.conf" <<'EOF'
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF
cat > "$ROOT_DIR/etc/fstab" <<'EOF'
LABEL=SYSTEM / ext4 rw,relatime,errors=remount-ro 0 0
LABEL=zirarch-data /data ext4 defaults,noatime 0 2
/data/root /root none bind 0 0
/data/apt-cache /var/cache/apt none bind 0 0
/data/app-data /var/lib/zirarch none bind 0 0
/data/fieldm /var/lib/fieldm none bind 0 0
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
devpts /dev/pts devpts gid=5,mode=620 0 0
EOF
printf 'America/Chicago\n' > "$ROOT_DIR/etc/timezone"
mkdir -p "$ROOT_DIR/usr/share/zoneinfo/America"
ln -sfn /usr/share/zoneinfo/America/Chicago "$ROOT_DIR/etc/localtime"
cat > "$ROOT_DIR/etc/inittab" <<'EOF'
id:3:initdefault:
si::sysinit:/sbin/openrc sysinit
rc::bootwait:/sbin/openrc boot
rw::wait:/sbin/openrc default
1:3:respawn:/sbin/agetty --noclear 38400 tty1 linux
T0:23:respawn:/sbin/agetty -L 115200 ttySAC2 vt100
ca:12345:ctrlaltdel:/sbin/reboot
pf::powerwait:/sbin/openrc shutdown
EOF
cat > "$ROOT_DIR/etc/network/interfaces" <<'EOF'
auto lo
iface lo inet loopback

iface rndis0 inet static
	address 192.168.7.2
	netmask 255.255.255.0
	gateway 192.168.7.1

iface wlan0 inet manual
EOF
cat > "$ROOT_DIR/etc/wpa_supplicant/wpa_supplicant.conf" <<'EOF'
ctrl_interface=/run/wpa_supplicant
ctrl_interface_group=0
update_config=1
EOF
chmod 600 "$ROOT_DIR/etc/wpa_supplicant/wpa_supplicant.conf"

cat > "$ROOT_DIR/etc/ssh/sshd_config.d/zirarch.conf" <<'EOF'
PermitRootLogin yes
PasswordAuthentication yes
KbdInteractiveAuthentication no
PubkeyAuthentication yes
UsePAM no
EOF
if [ -n "$SSH_PUBLIC_KEY" ]; then
	[ -s "$SSH_PUBLIC_KEY" ] ||
		die "missing SSH public key: $SSH_PUBLIC_KEY"
	install -m 600 "$SSH_PUBLIC_KEY" \
		"$ROOT_DIR/root/.ssh/authorized_keys"
else
	echo "warning: SSH_PUBLIC_KEY is unset; root SSH will have no login key" >&2
fi
printf 'root:%s\n' "$ZIRARCH_ROOT_PASSWORD" |
	chroot "$ROOT_DIR" /usr/sbin/chpasswd
chroot "$ROOT_DIR" /usr/bin/ssh-keygen -A

cat > "$ROOT_DIR/etc/default/zramswap" <<'EOF'
ALGO=lzo
PERCENT=30
PRIORITY=100
EOF
cat > "$ROOT_DIR/etc/init.d/zramswap" <<'EOF'
#!/sbin/openrc-run

description="Compressed RAM swap"

depend()
{
	after localmount
}

start()
{
	total_kib=$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo)
	[ -n "$total_kib" ] || return 1
	remaining=50
	while [ ! -d /sys/block/zram0 ] && [ "$remaining" -gt 0 ]; do
		sleep 0.1
		remaining=$((remaining - 1))
	done
	[ -d /sys/block/zram0 ] || return 1
	echo $((total_kib * 1024 * 30 / 100)) > /sys/block/zram0/disksize
	mkswap /dev/zram0
	swapon -p 100 /dev/zram0
}

stop()
{
	swapoff /dev/zram0
	[ -e /sys/block/zram0/reset ] &&
		echo 1 > /sys/block/zram0/reset
}
EOF
chmod 755 "$ROOT_DIR/etc/init.d/zramswap"

cat > "$ROOT_DIR/etc/init.d/usb-rndis" <<'EOF'
#!/sbin/openrc-run

description="Samsung legacy Android USB RNDIS network"

depend()
{
	before zirarch-sshd
}

start()
{
	gadget=/sys/class/android_usb/android0
	[ -d "$gadget" ] || return 1

	echo 0 > "$gadget/enable"
	echo 04e8 > "$gadget/idVendor"
	echo 6863 > "$gadget/idProduct"
	echo Samsung > "$gadget/f_rndis/manufacturer" 2>/dev/null || true
	echo 04e8 > "$gadget/f_rndis/vendorID" 2>/dev/null || true
	echo 1 > "$gadget/f_rndis/wceis" 2>/dev/null || true
	echo rndis > "$gadget/functions"
	echo 224 > "$gadget/bDeviceClass" 2>/dev/null || true
	echo 1 > "$gadget/enable"

	remaining=50
	while [ ! -d /sys/class/net/rndis0 ] && [ "$remaining" -gt 0 ]; do
		sleep 0.1
		remaining=$((remaining - 1))
	done
	ip link set rndis0 up
	ip addr show dev rndis0 | grep -q '192.168.7.2/24' ||
		ip addr add 192.168.7.2/24 dev rndis0
	if ! ip route show default | grep -q ' dev wlan0 '; then
		ip route replace default via 192.168.7.1 dev rndis0 metric 200
	fi

	ntpdate -u -s time.cloudflare.com 2>/dev/null &&
		date -u +%s > /var/lib/zirarch/clock
	hwclock -w 2>/dev/null || true
}
EOF
chmod 755 "$ROOT_DIR/etc/init.d/usb-rndis"

cat > "$ROOT_DIR/etc/init.d/zirarch-sshd" <<'EOF'
#!/sbin/openrc-run

description="Zirarch OpenSSH server"
command="/usr/sbin/sshd"
command_args="-D -e"
command_background=true
pidfile="/run/zirarch-sshd.pid"

depend()
{
	after usb-rndis
}

start_pre()
{
	mkdir -p /run/sshd
}
EOF
chmod 755 "$ROOT_DIR/etc/init.d/zirarch-sshd"

mkdir -p "$ROOT_DIR/var/lib/zirarch"
date -u +%s > "$ROOT_DIR/var/lib/zirarch/clock"

cat > "$ROOT_DIR/etc/init.d/wifi-link" <<'EOF'
#!/sbin/openrc-run

description="Bring up the Broadcom Wi-Fi interface"

depend()
{
	before wpa-supplicant-zirarch
}

start()
{
	[ -d /sys/class/net/wlan0 ] || return 1
	ip link set wlan0 up
}
EOF
chmod 755 "$ROOT_DIR/etc/init.d/wifi-link"

cat > "$ROOT_DIR/etc/init.d/wpa-supplicant-zirarch" <<'EOF'
#!/sbin/openrc-run

description="WPA supplicant for wlan0"
command="/usr/sbin/wpa_supplicant"
command_args="-i wlan0 -c /etc/wpa_supplicant/wpa_supplicant.conf"
command_background=true
pidfile="/run/wpa-supplicant-zirarch.pid"

depend()
{
	need wifi-link
	after wifi-link
}
start_post()
{
	[ ! -x /usr/bin/wifi ] ||
		nohup /usr/bin/wifi reconnect >/dev/null 2>&1 &
}
EOF
chmod 755 "$ROOT_DIR/etc/init.d/wpa-supplicant-zirarch"

mkdir -p "$ROOT_DIR/etc/dhcp/dhclient-exit-hooks.d"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-dhclient-hook" \
	"$ROOT_DIR/etc/dhcp/dhclient-exit-hooks.d/zirarch-default-route"

cat > "$ROOT_DIR/etc/X11/xorg.conf.d/10-on5-fbdev.conf" <<'EOF'
Section "Device"
	Identifier "On5 fbdev"
	Driver "fbdev"
	Option "fbdev" "/dev/fb0"
EndSection

Section "Screen"
	Identifier "On5 screen"
	Device "On5 fbdev"
	DefaultDepth 24
	SubSection "Display"
		Depth 24
		Virtual 720 1280
	EndSubSection
EndSection
EOF
cat > "$ROOT_DIR/etc/X11/xorg.conf.d/20-on5-touchscreen.conf" <<'EOF'
Section "ServerFlags"
	Option "AutoAddDevices" "false"
EndSection

Section "InputDevice"
	Identifier "On5 touchscreen"
	Driver "evdev"
	Option "Device" "/dev/input/event1"
	Option "SendCoreEvents" "true"
	Option "IgnoreRelativeAxes" "true"
	Option "IgnoreAbsoluteAxes" "false"
	Option "Emulate3Buttons" "false"
	Option "GrabDevice" "off"
EndSection

Section "ServerLayout"
	Identifier "On5 layout"
	Screen "On5 screen"
	InputDevice "On5 touchscreen" "CorePointer"
EndSection
EOF
cat > "$ROOT_DIR/etc/X11/Xwrapper.config" <<'EOF'
allowed_users=anybody
needs_root_rights=yes
EOF

cat > "$ROOT_DIR/etc/motd" <<'EOF'
Zirarch Debian trixie armhf (experimental)

SSH: root@192.168.7.2
Wi-Fi: wifi scan / wifi connect "SSID"
EOF

for runlevel in sysinit boot default shutdown; do
	mkdir -p "$ROOT_DIR/etc/runlevels/$runlevel"
done
for service in dmesg procfs sysfs zirarch-early-boot zirarch-haveged; do
	[ -e "$ROOT_DIR/etc/init.d/$service" ] &&
		chroot "$ROOT_DIR" /sbin/rc-update add "$service" sysinit
done
chroot "$ROOT_DIR" /sbin/rc-update add fieldm-logs sysinit
chroot "$ROOT_DIR" /sbin/rc-update add zirarch-clock sysinit
chroot "$ROOT_DIR" /sbin/rc-update del udev sysinit 2>/dev/null || true
chroot "$ROOT_DIR" /sbin/rc-update del udev-trigger sysinit 2>/dev/null || true
for service in bootmisc hostname hwclock fake-hwclock localmount modules root sysctl; do
	[ -e "$ROOT_DIR/etc/init.d/$service" ] &&
		chroot "$ROOT_DIR" /sbin/rc-update add "$service" boot
done
chroot "$ROOT_DIR" /sbin/rc-update add zirarch-power boot
for runlevel in $(find "$ROOT_DIR/etc/runlevels" -mindepth 1 -maxdepth 1 \
	-type d -printf '%f '); do
	chroot "$ROOT_DIR" /sbin/rc-update del ssh "$runlevel" 2>/dev/null || true
	chroot "$ROOT_DIR" /sbin/rc-update del udev "$runlevel" 2>/dev/null || true
	chroot "$ROOT_DIR" /sbin/rc-update del udev-trigger "$runlevel" 2>/dev/null || true
	chroot "$ROOT_DIR" /sbin/rc-update del fsck "$runlevel" 2>/dev/null || true
done
for service in networking dbus elogind zramswap usb-rndis \
	zirarch-clock-sync zirarch-sshd wifi-link \
	wpa-supplicant-zirarch zirarch-boot-mode zirarch-battery; do
	[ -e "$ROOT_DIR/etc/init.d/$service" ] ||
		die "required OpenRC service is missing: $service"
	chroot "$ROOT_DIR" /sbin/rc-update add "$service" default
done

rm -rf "$ROOT_DIR/usr/src/zirarch"
rm -f "$ROOT_DIR/etc/runlevels/"*"/udev" \
	"$ROOT_DIR/etc/runlevels/"*"/udev-trigger" \
	"$ROOT_DIR/etc/runlevels/"*"/ssh"
rm -f "$ROOT_DIR/etc/init.d/udev" "$ROOT_DIR/etc/init.d/udev-trigger"
chroot "$ROOT_DIR" /usr/bin/apt-get clean
find "$ROOT_DIR/var/lib/apt/lists" -mindepth 1 -delete
find "$ROOT_DIR/var/cache/apt/archives" -mindepth 1 -delete
rm -f "$ROOT_DIR/usr/bin/qemu-arm-static"
find "$ROOT_DIR" -type f -newermt '2001-01-01' \
	-exec touch -t 200001010000 {} +

cleanup
trap - EXIT HUP INT TERM

root_bytes=$(du -sx --block-size=1 "$ROOT_DIR" | cut -d '	' -f 1)
[ "$root_bytes" -lt "$SYSTEM_BYTES" ] ||
	die "rootfs ($root_bytes bytes) does not fit SYSTEM ($SYSTEM_BYTES bytes)"

tar --numeric-owner --owner=0 --group=0 \
	-C "$ROOT_DIR" -czf "$OUTPUT_ARCHIVE" .
echo "Debian rootfs: $OUTPUT_ARCHIVE"
du -sh "$ROOT_DIR"
echo "Rootfs used bytes: $root_bytes / $SYSTEM_BYTES"
echo "PID 1: sysvinit ($init_owner)"
