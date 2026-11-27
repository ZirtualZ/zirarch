#!/bin/sh
set -eu

KERNEL_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$KERNEL_DIR/.." && pwd)
WORK_DIR=${WORK_DIR:-"$PROJECT_DIR/builds/native-alpine-rootfs"}
ROOT_DIR="$WORK_DIR/root"
TOOLS_DIR="$WORK_DIR/tools"
ALPINE_VERSION=${ALPINE_VERSION:-3.22}
ALPINE_RELEASE=${ALPINE_RELEASE:-3.22.8}
APK_STATIC_VERSION=${APK_STATIC_VERSION:-2.14.13-r0}
MIRROR=${MIRROR:-https://dl-cdn.alpinelinux.org/alpine}
ROOTFS_ARCHIVE="$WORK_DIR/alpine-minirootfs-$ALPINE_RELEASE-armv7.tar.gz"
APK_STATIC_ARCHIVE="$WORK_DIR/apk-tools-static-$APK_STATIC_VERSION.apk"
ROOTFS_SHA256=a641f002abf617db94379110ffa798e0ed9903dbcec2bb1caa79df1fc7af8e8c
APK_STATIC_SHA256=d78d2a9ebadc8179b3b9063c3316e948f594c833db51eaeabe67f1d044245527
ANDROID_BLOBS=${ANDROID_BLOBS:-"$PROJECT_DIR/on5-android-blobs/system"}
SSH_PUBLIC_KEY=${SSH_PUBLIC_KEY:-}
OUTPUT_ARCHIVE=${OUTPUT_ARCHIVE:-"$PROJECT_DIR/builds/zirarch-native-rootfs.tar.gz"}

rm -rf "$ROOT_DIR" "$TOOLS_DIR"
mkdir -p "$ROOT_DIR" "$TOOLS_DIR"

if [ ! -f "$ROOTFS_ARCHIVE" ]; then
	curl -fL --retry 2 -o "$ROOTFS_ARCHIVE" \
		"$MIRROR/v$ALPINE_VERSION/releases/armv7/alpine-minirootfs-$ALPINE_RELEASE-armv7.tar.gz"
fi
if [ ! -f "$APK_STATIC_ARCHIVE" ]; then
	curl -fL --retry 2 -o "$APK_STATIC_ARCHIVE" \
		"$MIRROR/v$ALPINE_VERSION/main/x86_64/apk-tools-static-$APK_STATIC_VERSION.apk"
fi

printf '%s  %s\n' "$ROOTFS_SHA256" "$ROOTFS_ARCHIVE" | sha256sum -c -
printf '%s  %s\n' "$APK_STATIC_SHA256" "$APK_STATIC_ARCHIVE" | sha256sum -c -
tar -xzf "$ROOTFS_ARCHIVE" -C "$ROOT_DIR"
tar -xf "$APK_STATIC_ARCHIVE" -C "$TOOLS_DIR"

mkdir -p "$ROOT_DIR/etc/apk" "$ROOT_DIR/etc/init.d" \
	"$ROOT_DIR/etc/local.d" "$ROOT_DIR/etc/runlevels" \
	"$ROOT_DIR/root/.ssh" "$ROOT_DIR/etc/X11/xorg.conf.d"
cat > "$ROOT_DIR/etc/apk/repositories" <<EOF
$MIRROR/v$ALPINE_VERSION/main
$MIRROR/v$ALPINE_VERSION/community
EOF

PACKAGES="alpine-baselayout apk-tools openrc openssh fastfetch \
	wpa_supplicant wpa_supplicant-openrc \
	ncurses-terminfo ncurses kbd htop nano evtest xterm \
	xorg-server xf86-video-fbdev xf86-input-evdev xinit \
	svkbd buffyboard xrandr mesa-dri-gallium dbus libcap-setcap \
	ttf-dejavu e2fsprogs \
	gcc musl-dev make pkgconf gtk4.0-dev libx11-dev \
	libxrandr-dev libxft-dev"

if ! "$TOOLS_DIR/sbin/apk.static" \
	--root "$ROOT_DIR" \
	--arch armv7 \
	--initdb \
	--no-scripts \
	--no-chown \
	--repositories-file "$ROOT_DIR/etc/apk/repositories" \
	add $PACKAGES
then
	echo "apk reported an unprivileged package-metadata error; verifying transaction" >&2
	for package in $PACKAGES; do
		"$TOOLS_DIR/sbin/apk.static" --root "$ROOT_DIR" \
			--no-cache info -e "$package" >/dev/null || {
			echo "required package failed to install: $package" >&2
			exit 1
		}
	done
fi

install -m 755 "$PROJECT_DIR/builds/zirarch-fbconsole" \
	"$ROOT_DIR/usr/bin/zirarch-fbconsole"
install -m 755 "$PROJECT_DIR/builds/zirarch-buffyboard" \
	"$ROOT_DIR/usr/bin/buffyboard"
install -m 755 "$KERNEL_DIR/wifi" "$ROOT_DIR/usr/bin/wifi"
install -m 755 "$PROJECT_DIR/FieldM/simple-desktop.armv7" \
	"$ROOT_DIR/usr/bin/simple-desktop"
install -m 755 "$PROJECT_DIR/FieldM/simple-wm.armv7" \
	"$ROOT_DIR/usr/bin/simple-wm"
if [ -f "$PROJECT_DIR/FieldM/fieldm-keys.armv7" ]; then
	install -m 755 "$PROJECT_DIR/FieldM/fieldm-keys.armv7" \
		"$ROOT_DIR/usr/bin/fieldm-keys"
fi
install -m 755 "$KERNEL_DIR/fbconsole/screen-console.initd" \
	"$ROOT_DIR/etc/init.d/screen-console"

mkdir -p "$ROOT_DIR/lib/firmware" "$ROOT_DIR/vendor/etc/wifi"
install -m 644 "$ANDROID_BLOBS/etc/wifi/bcmdhd_sta.bin" \
	"$ROOT_DIR/lib/firmware/bcmdhd_sta.bin"
install -m 644 "$ANDROID_BLOBS/etc/wifi/nvram_net.txt" \
	"$ROOT_DIR/lib/firmware/nvram_net.txt"
install -m 644 "$ANDROID_BLOBS/etc/wifi/bcmdhd_sta.bin" \
	"$ROOT_DIR/vendor/etc/wifi/bcmdhd_sta.bin"
install -m 644 "$ANDROID_BLOBS/etc/wifi/nvram_net.txt" \
	"$ROOT_DIR/vendor/etc/wifi/nvram_net.txt"

cat > "$ROOT_DIR/etc/hostname" <<EOF
on5-native
EOF
cat > "$ROOT_DIR/etc/hosts" <<EOF
127.0.0.1 localhost localhost.localdomain
127.0.1.1 on5-native
::1 localhost localhost.localdomain
EOF
cat > "$ROOT_DIR/etc/resolv.conf" <<EOF
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF
cat > "$ROOT_DIR/etc/fstab" <<EOF
# Root is mounted read-write by the initramfs switch_root before OpenRC starts.
/dev/mmcblk0p19 / ext4 rw,relatime 0 0
proc /proc proc defaults 0 0
sysfs /sys sysfs defaults 0 0
devpts /dev/pts devpts gid=5,mode=620 0 0
tmpfs /run tmpfs mode=755,nodev,nosuid 0 0
EOF
cat > "$ROOT_DIR/etc/inittab" <<EOF
::sysinit:/sbin/openrc sysinit
::sysinit:/sbin/openrc boot
::wait:/sbin/openrc default

ttySAC2::respawn:/sbin/getty -L 115200 ttySAC2 vt100

::ctrlaltdel:/sbin/reboot
::shutdown:/sbin/openrc shutdown
EOF
cat > "$ROOT_DIR/etc/network/interfaces" <<EOF
auto lo
iface lo inet loopback

auto rndis0
iface rndis0 inet static
	address 192.168.7.2
	netmask 255.255.255.0
	gateway 192.168.7.1

auto wlan0
iface wlan0 inet manual
EOF

mkdir -p "$ROOT_DIR/etc/wpa_supplicant"
cat > "$ROOT_DIR/etc/wpa_supplicant/wpa_supplicant.conf" <<EOF
ctrl_interface=/run/wpa_supplicant
ctrl_interface_group=0
update_config=1
EOF
chmod 600 "$ROOT_DIR/etc/wpa_supplicant/wpa_supplicant.conf"

cat > "$ROOT_DIR/etc/init.d/wifi-link" <<'EOF'
#!/sbin/openrc-run

description="Bring up the Broadcom Wi-Fi interface"

depend()
{
	need devfs
	before wpa_supplicant networking
}

start()
{
	[ -d /sys/class/net/wlan0 ] || return 1
	ip link set wlan0 up
}
EOF
chmod 755 "$ROOT_DIR/etc/init.d/wifi-link"

install -m 755 "$KERNEL_DIR/boot-mode/zirarch-boot-mode.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-boot-mode"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-boot-mode" \
	"$ROOT_DIR/usr/bin/zirarch-boot-mode"
install -m 755 "$PROJECT_DIR/builds/zirarch-x11-refresh" \
	"$ROOT_DIR/usr/bin/zirarch-x11-refresh"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-session" \
	"$ROOT_DIR/usr/bin/fieldm-session"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-desktop-launch" \
	"$ROOT_DIR/usr/bin/fieldm-desktop-launch"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-keyboard" \
	"$ROOT_DIR/usr/bin/fieldm-keyboard"
install -m 755 "$KERNEL_DIR/boot-mode/fieldm-terminal" \
	"$ROOT_DIR/usr/bin/fieldm-terminal"
install -m 644 "$PROJECT_DIR/FieldM/terminal.desktop" \
	"$ROOT_DIR/usr/share/applications/terminal.desktop"
install -m 755 "$KERNEL_DIR/boot-mode/buffyboard.initd" \
	"$ROOT_DIR/etc/init.d/buffyboard"
install -m 755 "$KERNEL_DIR/boot-mode/zirarch-x11-refresh.initd" \
	"$ROOT_DIR/etc/init.d/zirarch-x11-refresh"

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

cat > "$ROOT_DIR/etc/profile" <<'EOF'
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PAGER=less
export EDITOR=nano
export HISTSIZE=1000

case "$(tty 2>/dev/null)" in
	/dev/tty*)
		export TERM=linux
		if [ "$(tty 2>/dev/null)" = /dev/tty1 ] &&
		    [ -x /usr/sbin/setfont ]; then
			setfont /usr/share/consolefonts/Lat2-Terminus16.psfu.gz \
				-C /dev/tty1 2>/dev/null || true
		fi
		;;
	/dev/pts/*)
		export TERM=xterm-256color
		if [ -t 0 ] && [ -t 1 ] && [ -x /usr/bin/resize ]; then
			/usr/bin/resize >/dev/null 2>&1 || true
			set -- $(stty size 2>/dev/null || echo "24 80")
			LINES=$1
			COLUMNS=$2
			export LINES COLUMNS
		fi
		;;
	*)
		: "${TERM:=linux}"
		export TERM
		;;
esac
EOF
cat > "$ROOT_DIR/root/.profile" <<'EOF'
. /etc/profile
EOF
chmod 600 "$ROOT_DIR/root/.profile"

cat > "$ROOT_DIR/etc/local.d/usb-rndis.start" <<'EOF'
#!/bin/sh

gadget=/sys/class/android_usb/android0
[ -d "$gadget" ] || exit 1

echo 0 > "$gadget/enable"
echo 04e8 > "$gadget/idVendor"
echo 6863 > "$gadget/idProduct"
echo Samsung > "$gadget/f_rndis/manufacturer" 2>/dev/null || true
echo 04e8 > "$gadget/f_rndis/vendorID" 2>/dev/null || true
echo 1 > "$gadget/f_rndis/wceis" 2>/dev/null || true
echo rndis > "$gadget/functions"
echo 224 > "$gadget/bDeviceClass" 2>/dev/null || true
echo 1 > "$gadget/enable"

ip link set rndis0 up
ip addr show dev rndis0 | grep -q '192.168.7.2/24' ||
	ip addr add 192.168.7.2/24 dev rndis0
ip route replace default via 192.168.7.1 dev rndis0
ntpd -n -q -p time.cloudflare.com >/dev/null 2>&1 || true
hwclock -w >/dev/null 2>&1 || true
setcap cap_net_bind_service,cap_net_admin,cap_sys_nice=ep \
	/usr/libexec/gstreamer-1.0/gst-ptp-helper 2>/dev/null || true
ssh-keygen -A
/usr/sbin/sshd
EOF
chmod 755 "$ROOT_DIR/etc/local.d/usb-rndis.start"

mkdir -p "$ROOT_DIR/var/empty"
chmod 755 "$ROOT_DIR/var/empty"

for runlevel in sysinit boot default shutdown; do
	mkdir -p "$ROOT_DIR/etc/runlevels/$runlevel"
done
for service in devfs dmesg mdev procfs sysfs; do
	[ -e "$ROOT_DIR/etc/init.d/$service" ] &&
		ln -sfn "/etc/init.d/$service" "$ROOT_DIR/etc/runlevels/sysinit/$service"
done
for service in bootmisc hostname hwclock localmount modules root sysctl urandom; do
	[ -e "$ROOT_DIR/etc/init.d/$service" ] &&
		ln -sfn "/etc/init.d/$service" "$ROOT_DIR/etc/runlevels/boot/$service"
done
for service in loopback wifi-link wpa_supplicant zirarch-boot-mode \
	networking local; do
	[ -e "$ROOT_DIR/etc/init.d/$service" ] &&
		ln -sfn "/etc/init.d/$service" "$ROOT_DIR/etc/runlevels/default/$service"
done
for service in killprocs mount-ro savecache; do
	[ -e "$ROOT_DIR/etc/init.d/$service" ] &&
		ln -sfn "/etc/init.d/$service" "$ROOT_DIR/etc/runlevels/shutdown/$service"
done

[ -e "$ROOT_DIR/etc/init.d/fsck" ] &&
	rm -f "$ROOT_DIR/etc/runlevels/boot/fsck"

sed -i 's/^root:\*:/root::/' "$ROOT_DIR/etc/shadow"
if [ -n "$SSH_PUBLIC_KEY" ]; then
	[ -s "$SSH_PUBLIC_KEY" ] || {
		echo "missing SSH public key: $SSH_PUBLIC_KEY" >&2
		exit 1
	}
	install -m 600 "$SSH_PUBLIC_KEY" "$ROOT_DIR/root/.ssh/authorized_keys"
fi

cat > "$ROOT_DIR/etc/motd" <<EOF
Zirarch native Alpine Linux

SSH: root@192.168.7.2
Wi-Fi: wifi scan / wifi connect "SSID"
EOF

tar --numeric-owner --owner=0 --group=0 \
	-C "$ROOT_DIR" -czf "$OUTPUT_ARCHIVE" .
echo "native rootfs: $OUTPUT_ARCHIVE"
du -h "$OUTPUT_ARCHIVE"
