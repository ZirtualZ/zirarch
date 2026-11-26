# Zirarch SM-G550T knowledge base

## Project summary

Zirarch is a reversible Linux port for the T-Mobile Samsung Galaxy On5,
model `SM-G550T`, codename `on5ltetmo`.

The working system uses:

- The Samsung downstream Exynos 3475 Linux 3.10 kernel.
- A 32-bit ARM (`armv7` / hard-float ABI) Alpine Linux userspace.
- OpenRC instead of systemd.
- The phone's existing Android partition table.
- TWRP as the recovery and rescue environment.
- A small initramfs that mounts Android `USERDATA` and chroots into
  `/data/zirarch-root`.
- A direct fbdev console with an on-screen QWERTY/symbol keyboard.

This is a proven downstream-kernel port and postmarketOS-oriented Alpine
prototype, not a mainline kernel port and not yet a complete postmarketOS
image.

## Hardware and firmware

Observed device state:

| Item | Value |
| --- | --- |
| Model | `SM-G550T` |
| Codename | `on5ltetmo` / `on5lte` |
| SoC | Exynos 3475 (`universal3475`) |
| CPU ABI | `armeabi-v7a`, 32-bit ARM hard-float |
| RAM | About 1.5 GiB |
| Kernel | Samsung/Linux 3.10.108 downstream kernel |
| GPU | Mali-T720 |
| Display | 720x1280, 32-bit fbdev, `decon_3475` |
| Touch | `sec_touchscreen`, `/dev/input/event1` |
| Recovery | TWRP 3.7.0_9-0 |
| Host | Arch Linux x86_64 with ADB, USB ACM, and RNDIS |

The touchscreen reports `sec_touchscreen`, event `event1`. The live Linux
system has these relevant descriptors:

- `/dev/fb0`: frame 720x1280, 32 bpp, stride 2880.
- `/dev/input/event1`: touchscreen.
- `/dev/ttyGS0`: Samsung ACM gadget serial device.
- `/dev/mmcblk0p9`: boot partition.
- `/dev/mmcblk0p10`: recovery partition.
- `/dev/mmcblk0p20`: cache.
- `/dev/mmcblk0p21`: Android data/userdata.

## Partition layout

No repartitioning is used. The relevant Android/T-Mobile partition names
are:

| Partition | Node | Approx. size | Purpose in Zirarch |
| --- | --- | --- | --- |
| BOOT | `mmcblk0p9` | 13 MiB | Linux boot image |
| RECOVERY | `mmcblk0p10` | 15 MiB | TWRP, untouched |
| SYSTEM | `mmcblk0p19` | 2.8 GiB | Android, preserved |
| CACHE | `mmcblk0p20` | 290 MiB | init logs and TWRP compatibility |
| USERDATA | `mmcblk0p21` | 4.0 GiB | Android data plus `/data/zirarch-root` |
| EFS | `mmcblk0p3` | 15 MiB | Cellular identity data, never modified |
| CPEFS | `mmcblk0p4` | 3 MiB | Samsung firmware data, never modified |
| RADIO | `mmcblk0p13` | 40 MiB | Modem, never modified |

The live root filesystem is a directory inside Android userdata:

```text
/data/zirarch-root
```

This is deliberately not a destructive whole-device Linux installation.
It preserves Android, EFS, CPEFS, modem, TWRP, and all existing userdata.

## Samsung boot image format

Samsung Exynos boot handling differs from a generic Android `boot.img`.
The working format is:

- Android boot magic `ANDROID!`.
- 2048-byte pages.
- Kernel base `0x10000000`.
- Kernel offset `0x00008000`.
- Ramdisk offset `0x01000000`.
- Tags offset `0x00000100`.
- Board string `SRPPB24A001KU`.
- Samsung DTBH container after the ramdisk.
- Legacy header DT size field populated.
- `SEANDROIDENFORCE` trailer.

Boot payloads and tools are built by:

```text
kernel/build-on5-boot.sh
kernel/build-on5-persistent.sh
kernel/build-on5-minimal.sh
```

`build-on5-boot.sh` extracts the DTBH from the saved stock boot image and
reuses it rather than inventing a DT container. This was essential to
producing images accepted by the Samsung bootloader.

The first proof of boot used an entirely RAM-only BusyBox initramfs and
did not modify Android partitions. It demonstrated that:

- The stock kernel could run custom PID 1.
- `/dev/fb0` existed.
- The Samsung panel and backlight could be addressed.
- `/cache/zirarch-init.log` could be written.

## Reversible Linux installation

The persistent initramfs is:

```text
kernel/initramfs-on5/init-persistent
```

Its responsibilities are:

1. Mount `proc`, `sysfs`, `devtmpfs`, and `devpts`.
2. Attempt to leave SELinux non-enforcing.
3. Mount `CACHE` for persistent early-boot diagnostics.
4. Mount `USERDATA` at `/data`.
5. Validate `/data/zirarch-root/sbin/init`.
6. Bind `/dev`, `/proc`, and `/sys` into the root tree.
7. `chroot` to the Alpine/OpenRC tree and execute `/sbin/init`.
8. Enter an explicit shell on a missing mount or invalid rootfs rather
   than silently falling through.

The initramfs deliberately invokes the bundled BusyBox by absolute path.
An earlier version relied on an uninstalled applet symlink in `/sbin`;
that produced a blank rescue shell and no boot log. Using
`/bin/busybox mount`, `mkdir`, `chmod`, `chroot`, and related applets
fixed that failure.

## Alpine/OpenRC userspace

The userspace is built from Alpine 3.22.8 `armv7` mini-rootfs plus:

- `openrc`
- `alpine-baselayout`
- `dropbear`
- `dropbear-openrc`

The rootfs build is scripted in:

```text
kernel/prepare-on5-rootfs.sh
```

It installs packages into a host-side directory with Alpine's static
`apk` while specifying the target architecture. It avoids requiring a
privileged loop mount or `kpartx`.

The current OpenRC services include:

- `devfs`
- `dmesg`
- `procfs`
- `sysfs`
- `modules`
- `hwclock`
- `root`
- `localmount`
- `sysctl`
- `bootmisc`
- `hostname`
- `loopback`
- `usb-gadget`
- `screen-console`
- `networking`
- `dropbear`

The rootfs contains no Android system daemon. It does not repartition or
mount Android `/system` as the active root.

## USB, serial, SSH, and RNDIS

The custom `usb-gadget` OpenRC service uses Samsung's legacy Android USB
gadget sysfs:

```text
/sys/class/android_usb/android0
```

It configures:

```text
rndis,acm
```

and exposes:

- Phone RNDIS address: `192.168.7.2/24`.
- Typical host address: `192.168.7.1/24`.
- ACM serial: host `/dev/ttyACM0`.
- Gadget serial: phone `/dev/ttyGS0`.
- Dropbear SSH on the RNDIS address.

The phone's old kernel/userspace PTY support has compatibility quirks:

- OpenSSH may report `PTY allocation request failed`.
- A command channel (`ssh -T`) works.
- UNIX98 PTY allocation returns a PTY number but the old kernel does not
  automatically materialize `/dev/pts/N`.
- Legacy BSD PTYs work with master major 2 and slave major 3.

The on-screen console therefore creates a standard PTY first and falls
back to `/dev/pty0`/`/dev/ttyp0` if devpts cannot open the allocated slave.

## Display problem and root cause

The Exynos 3475 display uses the `decon_3475` downstream driver and
legacy fbdev. Mainline DRM/KMS is not available and is not the solution.

Two independent bugs/requirements prevented a plain framebuffer console:

### 1. Bootloader `DECON_STATE_INIT` lock

At boot, the bootloader can leave DECON running with:

```text
decon->state == DECON_STATE_INIT
```

The original `decon_enable()` handled an unblank while already in this
state by changing to `DECON_STATE_ON` but returning `-EBUSY`. A direct
unblank could therefore fail even though the display hardware was live.

The kernel fix changes this transition to a successful handoff:

```text
DECON_STATE_INIT -> DECON_STATE_ON
```

The userspace service still follows the proven robust transition:

```sh
echo 4 > /sys/class/graphics/fb0/blank
sleep 1
echo 0 > /sys/class/graphics/fb0/blank
```

Backlight brightness is then enabled from:

```text
/sys/devices/14800000.dsim/backlight/panel/brightness
```

### 2. Window 0 was not fully configured or started

The original `decon_pan_display()`:

- Reused `decon->win_regs`.
- Did not rebuild them for framebuffer userspace writes.
- Wrote only an updated start address in one case.
- Activated the window but did not perform the full DECON start/update
  path equivalent to an Android `S3CFB_WIN_CONFIG` submission.

The patched path now:

1. Refuses OFF/invalid state.
2. Calls `decon_set_par()` for Window 0.
3. Gets valid 32-bit Window 0 parameters:
   - `WINCON_ENWIN`.
   - ARGB/XRGB 8888 mode.
   - VIDOSD A/B covering `(0,0)` through `(719,1279)`.
   - Alpha fields.
   - Window 0 DMA type.
4. Applies `xoffset`/`yoffset` and virtual geometry.
5. Protects Window 0 shadow registers.
6. Writes the complete window register set.
7. Writes `VIDW_ADD0` from
   `info->fix.smem_start + yoffset/xoffset byte offset`.
8. Releases shadow protection.
9. Activates Window 0.
10. Calls `decon_reg_start()` with the device's DSI mode and PSR
    configuration, causing the standalone register update/frame start.
11. Enables the hardware trigger when required.
12. Waits for vsync and balances the trigger state.

The relevant patched files are:

```text
drivers/video/exynos/decon_3475/decon_core.c
drivers/video/exynos/decon_3475/decon-int_drv.c
```

## Raw framebuffer details

The userspace renderer is:

```text
kernel/fbconsole/fbconsole.c
```

It directly memory-maps `/dev/fb0`, using:

- Width 720.
- Height 1280.
- Bytes per pixel 4.
- Actual line stride from `FBIOGET_FSCREENINFO`, not a hard-coded stride.
- Samsung BGRA/XRGB-style layout: blue, green, red, alpha bytes.

Drawing into `/dev/fb0` alone is insufficient for this display. After
changing a frame, the renderer issues:

```text
FBIOPAN_DISPLAY
```

This ioctl is what drives the patched DECON Window 0 scanout path.

The console font is an embedded 8x16 PSF console font. It is converted to
a C header during:

```text
kernel/build-on5-fbconsole.sh
```

Text is rendered with 2x scaling. Keyboard labels also use 2x scaling.

## Framebuffer console architecture

The framebuffer console is a complete small terminal rather than a
kernel `fbcon` dependency:

- It opens `/dev/fb0`.
- It allocates a login/shell PTY.
- It starts a shell as root.
- It reads shell output from the PTY.
- It parses terminal text and common ANSI CSI sequences.
- It writes rendered pixels to the framebuffer and pans the display.
- It copies the PTY stream to `/dev/ttyGS0` as a USB serial mirror.
- It accepts bytes from `/dev/ttyGS0` and injects them into the PTY.
- It reads `/dev/input/event1` and injects touchscreen keyboard taps.

The initial version launched `/bin/login`. That timed out while the
onscreen keyboard was being used. At the user's request, the screen
console now starts `/bin/sh -l` directly as root.

The process is managed by:

```text
kernel/fbconsole/screen-console.initd
```

Its `start_pre()` performs the required DECON 4 -> 0 blank transition
before setting panel brightness.

## ANSI terminal parser

Early touch tests exposed strings such as:

```text
6n
J
```

They were not rogue touchscreen events. BusyBox shell/readline emitted
ANSI Device Status Report/cursor control sequences, and the first parser
consumed only the `ESC[` prefix, leaving the final bytes visible.

The parser now consumes CSI sequences, including:

- Cursor absolute position (`H`, `f`).
- Clear display (`J`).
- Clear to end of line (`K`).
- Cursor up/down/left/right (`A`, `B`, `C`, `D`).
- Device status/cursor report and other unsupported complete CSI
  sequences without printing them.

Backspace now performs both terminal-state and immediate screen updates:

1. Move cursor left.
2. Replace the prior cell with a space.
3. Redraw that cell immediately.

This removed a visible stale-character problem.

## On-screen keyboard

The touchscreen keyboard occupies the lower 380 pixels of the display:

- Five rows.
- About 76 pixels per row.
- QWERTY letters.
- Dedicated number row.
- Shift.
- Space.
- Enter.
- Backspace.
- `SYM` and `ABC` layers.
- `CTRL` toggle for one-shot control-key input.

The terminal area is reduced above the keyboard, and the shell PTY window
is sized to that terminal region rather than the full physical display.

`SYM` exposes punctuation and common shell characters. `ABC` returns to
the alphabetic layout. `CTRL` is a one-shot modifier: tap it and then a
letter to send ASCII control codes (for example, `CTRL` then `C` sends
SIGINT to a terminal process). The modifier resets after one key.

Touch handling:

- Uses Linux input event ABI.
- Tracks `ABS_MT_POSITION_X` and `ABS_MT_POSITION_Y`.
- Reads axis limits with `EVIOCGABS`.
- Maps touch coordinates to 720x1280 screen coordinates.
- Ignores multitouch slots other than slot 0.
- Marks each release before processing it, so one tap produces one key.
- Handles `BTN_TOUCH` fallback.
- Writes the selected key directly to the master PTY.

While the display is awake, `zirarch-fbconsole` also discovers the input
event node that advertises `KEY_POWER` (code 116), preferring the
hardware `gpio-keys` node when multiple devices support the code.

A key-down event toggles application-managed display sleep:

1. Awake -> asleep writes backlight brightness `0`, writes framebuffer
   blank value `4`, and closes the touchscreen fd so queued and pocket
   touches cannot enter the terminal.
2. Asleep -> awake repeats the required `4 -> 0` blank unlock sequence,
   restores brightness `255`, redraws the retained terminal and keyboard
   state, pans Window 0, and reopens the touchscreen with fresh input state.

USB serial, SSH, the shell, and the kernel continue running while the
screen is off; this does not suspend the complete system.

## Boot splash

Immediately after `/dev/fb0` is mapped and the initial framebuffer has
been cleared, `zirarch-fbconsole` draws a centered, pixel-font banner:

```text
ZIRARCH // SM-G550T
+ Alpine armv7
```

The banner is refreshed through `FBIOPAN_DISPLAY`, held for one second,
then replaced by the interactive terminal and on-screen keyboard.

## Broadcom Wi-Fi firmware and userspace

The active kernel configuration selects the built-in BCM4343
`bcmdhd_1_77` driver and configures these absolute firmware paths:

```text
/vendor/etc/wifi/bcmdhd_sta.bin
/vendor/etc/wifi/nvram_net.txt
```

The Exynos 3475 On5 device tree enables the Samsung Broadcom Wi-Fi
platform control and the SDIO-1 4-bit slot. `bcmdhd` reads the firmware
and board-calibration NVRAM directly from those paths. Missing blobs
must not be substituted with firmware from another device.

The stock Android SYSTEM partition `/dev/mmcblk0p19` contains these
files below `/system/etc/wifi`. Extract the exact files read-only with:

```sh
SSH_IDENTITY=/tmp/zirarch_key \
SSH_TARGET=root@192.168.7.2 \
  kernel/extract-on5-wifi.sh
```

The helper stages them in `kernel/firmware/on5/`.
The extracted SM-G550T files have SHA-256 values:

```text
bcmdhd_sta.bin 343b4e231ba68b48bf85f2a8ce0459b72a765fcc270b9fce2cec9c6d9d9d4802
nvram_net.txt  d009fea2e129b80a1fbeadc2209aee8e88bfd5d6c49c670c7bcde70fc666acd4
```

`prepare-on5-rootfs.sh` refuses to continue if either file is absent,
then copies the staged blobs to the paths expected by the kernel.
`build-on5-persistent.sh` verifies both files are in the generated
rootfs archive and embeds the same files in the early initramfs.

The Alpine rootfs installs `wpa_supplicant`, its OpenRC service, and
BusyBox `udhcpc`. It creates a control-socket configuration, keeps the
`wlan0` interface entry manual, and starts a `wifi-link` service to
bring `wlan0` up before `wpa_supplicant`. After credentials are added to
`/etc/wpa_supplicant/wpa_supplicant.conf`, `udhcpc -i wlan0` requests the
IPv4 lease.

The simple userspace wrapper is `wifi`:

```sh
wifi scan
wifi connect "My network"
wifi connect --open "Open network"
wifi status
```

The first form hides the password while prompting; a password may also
be supplied after the SSID for automation, although command-line
arguments are visible to process inspection. `wifi connect` configures
and saves the network through `wpa_cli`, waits for WPA association, and
runs BusyBox `udhcpc`. An empty password line cancels safely, and
`wifi scan` prints nearby signal, security type, and SSID.

The Samsung kernel marks bcmdhd as a deferred initcall. Android normally
releases that class of initcalls by reading `/proc/deferred_initcalls`;
`init-persistent` performs the same one-shot read after mounting
`proc`, `sys`, and `devtmpfs`, with the firmware already present in the
early initramfs. Without this read, the Wi-Fi platform data and SDIO
driver exist in the kernel but the chip is never enumerated and `wlan0`
does not appear.

The display and touch behavior were verified physically:

- Root prompt visible.
- Keyboard visible and readable.
- `uname -a` entered with taps and displayed correctly.
- Backspace visibly erased characters.
- Symbol layer produced punctuation.
- ABC returned to letters.
- No `6n`, `J`, or rogue control-sequence glyphs remained.

The power-button upgrade was also verified through the live console log:
multiple `display-sleep` and `display-wake` transitions occurred while
the process held the touch and GPIO key event nodes.

The final Wi-Fi boot verifies:

- `/proc/deferred_initcalls` runs the Samsung bcmdhd initializer.
- SDIO `mmc1:0001:1/2` and `p2p0`/`wlan0` enumerate.
- The exact stock firmware and NVRAM paths are used at download time.
- Firmware `7.10.323.73` comes up and reports the calibrated Broadcom MAC.
- `wifi-link` and `wpa_supplicant` are started by OpenRC.
- `wpa_cli -i wlan0 scan` returns nearby access points.
- `udhcpc` runs and sends a discover; an actual lease requires configured
  credentials and association to an access point.

The DHD_DEBUG build emits a `Downloaded NVRAM image is corrupted`
read-back comparison on this chip even with the exact stock file. The
download continues, firmware comes up, and scanning works; this driver
diagnostic is not a failed firmware download.

## Build flow

Build the framebuffer console:

```sh
kernel/build-on5-fbconsole.sh
```

This downloads/reuses Alpine ARM musl development and compiler runtime
packages and cross-compiles a 32-bit PIE ARM binary linked for musl.
The generated executable is:

```text
builds/zirarch-fbconsole
```

Build the Alpine rootfs:

```sh
SSH_IDENTITY=/tmp/zirarch_key \
SSH_TARGET=root@192.168.7.2 \
  kernel/extract-on5-wifi.sh
kernel/prepare-on5-rootfs.sh
```

The generated archive is:

```text
builds/zirarch-rootfs.tar.gz
```

Build the kernel:

```sh
make -C kernel \
  ARCH=arm \
  CROSS_COMPILE=/home/marshal/toolchains/arm-eabi-4.8/bin/arm-eabi- \
  -j4 zImage
```

Build the final Samsung boot image:

```sh
STOCK_KERNEL="$PWD/kernel/arch/arm/boot/zImage" \
DEBUG_CONSOLE=1 \
OUTPUT="$PWD/builds/zirarch-screen-boot.img" \
  kernel/build-on5-persistent.sh
```

`DEBUG_CONSOLE=1` adds the serial-friendly bootargs and patches the DTB
for console output while retaining the stock DTBH format.

## Native Alpine on SYSTEM

A separate build path installs Alpine directly on Android's `SYSTEM`
partition while preserving the reversible `/data/zirarch-root` backups.

Kernel prerequisites are:

```text
# CONFIG_ANDROID_PARANOID_NETWORK is not set
CONFIG_DEVTMPFS=y
CONFIG_DEVTMPFS_MOUNT=y
```

The native ramdisk is `initramfs-on5/init-native`. It mounts proc, sysfs,
and devtmpfs; initializes Samsung's legacy RNDIS gadget at
`/sys/class/android_usb/android0`; configures `192.168.7.2/24`; releases
Samsung deferred initcalls; mounts
`/dev/block/platform/13540000.dwmmc0/by-name/SYSTEM` as ext4 with
`rw,relatime`; moves `/dev`, `/proc`, and `/sys` into `/newroot`; and runs
BusyBox `switch_root /newroot /sbin/init`.

Build with:

```sh
make -C kernel ARCH=arm \
  CROSS_COMPILE=/home/marshal/toolchains/arm-eabi-4.8/bin/arm-eabi- \
  -j4 zImage

SSH_PUBLIC_KEY=/path/to/id.pub kernel/prepare-on5-native-rootfs.sh
kernel/build-on5-native-system.sh
kernel/build-on5-native-boot.sh
```

The ext4 image is exactly the `SM-G550T` SYSTEM partition size,
`3061841920` bytes. The 64-bit and metadata-checksum ext4 features are
disabled for Linux 3.10 compatibility. Because the root tree is prepared
unprivileged, `build-on5-native-system.sh` normalizes every image inode
to UID/GID 0 with `debugfs`.

The rootfs includes Alpine 3.22.8 armv7 OpenRC, `apk-tools`, OpenSSH,
`fastfetch`, `wpa_supplicant`, `wifi`, and the framebuffer console. Stock
BCM4343 blobs are installed in both `/lib/firmware` and the absolute
`/vendor/etc/wifi` paths expected by bcmdhd.

`/etc/local.d/usb-rndis.start` uses the stock On5 USB values (Samsung
vendor `04e8`, RNDIS product `6863`, RNDIS function), configures
`192.168.7.2/24` and gateway `192.168.7.1`, synchronizes the RTC through
BusyBox NTP, generates SSH host keys, and starts OpenSSH.

The live native system reports Alpine 3.22.8 on
`3.10.108-zirarch`, mounts `/dev/mmcblk0p19` at `/` as
`rw,relatime,data=ordered`, and runs `wifi-link`, `wpa_supplicant`,
the boot-mode selector, `networking`, and `local`. `apk add --no-cache
fastfetch` and `fastfetch` complete as root.

## Native TTY, X11 touch, boot selection, and FieldM

The native image includes the TUI tools, console terminfo/fonts, Xorg
fbdev and evdev drivers, buffyboard/svkbd, and the FieldM build
dependencies (`gcc`, `musl-dev`, `make`, `pkgconf`, GTK4 and X11
development packages).

`/etc/profile` selects `TERM=linux` for `/dev/tty*` and
`xterm-256color` for `/dev/pts/*`. PTY login runs `resize`, then derives
`LINES` and `COLUMNS` from `stty size`; an interactive login was verified
as 80x24. The tty1 font is
`/usr/share/consolefonts/Lat2-Terminus16.psfu.gz`. `htop`, `nano`, and
BusyBox `top` operate correctly.

The framebuffer is 720x1280, 32 bits per pixel. Xorg fbdev uses depth 24
with a shadow framebuffer, and evtest identifies the direct
`sec_touchscreen` device with absolute multitouch X/Y limits 720x1280.
Xorg ignores relative axes and initializes the touchscreen's absolute
axes. Input event numbers are discovered by sysfs name rather than
hard-coded: this is done by `fbconsole.c` and `fieldm-session`.

Samsung's Melfas driver normally refuses to probe when
`androidboot.mode=charger` sets `lpcharge=1`. Zirarch's native kernel
keeps the touchscreen active in that state so charging/recovery reboots
do not disable touch.

`/usr/bin/zirarch-fbconsole --boot-menu` displays a 5-second selector:

- Volume Down or tap the top half: TTY/buffyboard.
- Volume Up or tap the bottom half: X11/FieldM.
- Timeout: TTY.

TTY mode starts a real tty1 getty and buffyboard in the lower
framebuffer. Alpine's buffyboard 3.2.0 expects the newer `UI_DEV_SETUP`
ioctl. `kernel/build-on5-buffyboard.sh` applies
`buffyboard/legacy-uinput.patch`, writes the legacy `uinput_user_dev`
structure, and builds an ARM musl binary which successfully creates a
`buffyboard` input device on Linux 3.10.

Host-side unprivileged apk extraction cannot apply the file capability
on GStreamer's PTP helper. The installed `libcap-setcap` tool repairs the
packaged `cap_net_bind_service,cap_net_admin,cap_sys_nice=ep` value from
`usb-rndis.start`, so later `apk add` transactions complete normally.

GUI mode starts FieldM under `startx`. The session passes
`--x-session` so `simple-desktop` starts its sibling `simple-wm`, uses a
session D-Bus, and requests Cairo/GSK software rendering.

Xorg uses a shadow framebuffer on this DECON, so a native
`zirarch-x11-refresh` helper calls `FBIOPAN_DISPLAY` continuously at
about 60 Hz while FieldM is active. Without this DECON update, X draws
correctly in framebuffer RAM but the physical panel remains stale or
blank. The refresh service is started and stopped with the X session.

FieldM is bundled directly in the tracked `FieldM/` directory. Its
source is part of the Zirarch repository rather than a bootstrapped
checkout.

The fork removes desktop-item and multi-monitor modules and introduces
single-screen `screen_layout`. It targets one 720x1280 portrait screen,
maximizes managed windows, uses a 64-pixel titlebar, 52-pixel close
target, 88-pixel taskbar, enlarged GTK controls, touch drag handling,
and a Keyboard button that toggles `svkbd-mobile-intl`.

The mobile shell uses three equally sized 56px top-panel buttons
(`Applications`, `Keyboard`, `Settings`) and a deterministic 56px
letter badge for every launcher row, so missing icon themes still
produce clear, consistent targets. The launcher search and a managed
terminal each acquire an automatic keyboard lease through
`fieldm-keyboard`. A terminal gets 708x816 of usable client area,
`svkbd` is unmanaged and pinned to 720x300 at y=892, and the 88px
taskbar remains visible at y=1192. Closing the launcher or last terminal
releases its lease and hides svkbd. A manual Keyboard toggle is an
independent lease and therefore remains visible until toggled off.

`fieldm-terminal` wraps xterm with DejaVu Sans Mono and the mobile
colors/font size. The corresponding `terminal.desktop` entry is used by
the launcher. Desktop-level Back/Escape closes a transient UI when one
is open but never terminates the whole desktop; the outer boot-mode
selector also restarts a GUI session if it exits unexpectedly.

## Artifacts and checksums

Important final artifacts include:

| Artifact | SHA-256 | Purpose |
| --- | --- | --- |
| `builds/zirarch-screen-boot.img` | `aeb122936a73368505e13d519120fe7ed16195561cfde4a348c993f52fa9c3f0` | Current display/power/Wi-Fi boot |
| `builds/zirarch-fbconsole` | `cf47810a79eaa27d466fe50c83fec03caba5c9080f08a6e167f709c1c9ead547` | Dynamic touch, power, and selector console |
| `builds/zirarch-rootfs.tar.gz` | `b1ffa01c77ff73f1f4af9ac7cbf552792ff7ae9e3c909792940478e39bddb3d3` | Wi-Fi-enabled Alpine/OpenRC rootfs |
| `backups/zirarch-screen-boot.img` | `8f7c9b4fd1a2ec4c6955e79482bb8948477a8651e3e7d553a0d776db5951f9d9` | Known-good boot before the current upgrade |
| `backups/live-pre-upgrade-boot.img` | `1c62e9e9efa20423d208aa09cf6c956031e84a99f2e314aa68e64643f0d74661` | Exact live BOOT partition dump before flashing |
| `builds/pre-zirarch-boot.img` | `4d29be356cef25bd33a94c3c92818e4a61e8192ebe847a18831540cf618c9dde` | Exact original pre-Zirarch boot backup |
| `builds/pre-screen-fix-boot.img` | `237aad1cd22ccbb26df6a9571e21e709bdd3a95432622f2c0e4636d18eea838c` | Backup immediately before DECON-patched boot |
| `backups/zirarch-boot-pre-native.img` | `1812bfd15bbc4adb248563db8a0f8d5d7a94b92686a9565f6bea70d03f689d0b` | Previous `zirarch-boot.img` before native migration |
| `backups/zirarch-pre-native-boot.img` | `aeb122936a73368505e13d519120fe7ed16195561cfde4a348c993f52fa9c3f0` | Last live persistent-USERDATA boot |
| `backups/twrp-pre-native-live-boot.img` | `3120d7c8dad85f7dbc050e82cd578f2f23abd114fac98a456660f7ac4b474715` | Exact live BOOT dump before native flash |
| `builds/zirarch-boot.img` | `54ee694ee8bb3a30f4569e1a5a808f1911dea17d320c30a079b7762d0cec02df` | Native boot with charger-mode touch fix |
| `builds/zirarch-native-rootfs.tar.gz` | `9518e6d02a35d5ff234ee2fde4a0bcb00c3a053c0807c87d671d7f9718879e9e` | TTY/X11/mobile-FieldM native rootfs |
| `builds/zirarch-alpine-system.img` | `4f95d7e2f6d5bc9ceb51c344082e43bb4e4adafc1882575acaca74c119fb354b` | Exact-size TTY/X11/mobile-FieldM SYSTEM image |
| `builds/zirarch-buffyboard` | `cc9f04d6622fc1694eb992e5fc624a4ecd12cf3e42eb05025ede347cc18d4e83` | Linux-3.10-compatible buffyboard |
| `FieldM/simple-desktop.armv7` | `b543ca51185740d82e6eb0d1fddfa249e4bb9fb041c07b63098396da29c88033` | ARM mobile FieldM shell |
| `FieldM/simple-wm.armv7` | `0516ce4f01d6d6be46516b7a15d1eb2bcf691b3bd895b5733deea6ef7aee637e` | ARM keyboard-aware FieldM WM |
| `builds/zirarch-x11-refresh` | `ec6ccee545078e9d675ac309f939b3fcebc3a9cb2af3025c68b0676ffd5c47e9` | DECON pan refresh helper |
| `backups/zirarch-boot-pre-touch-lpm-fix.img` | `983519febd7a861c1a75e13cfb595bc4da5989295b5a3973d85eebe0850e48a5` | Live BOOT before charger-touch kernel fix |

The TWRP recovery image was also dumped and compared against the live
15 MiB recovery partition with a matching checksum.

## Verified live state

The patched phone boots:

```text
Linux on5-pmos 3.10.108-zirarch ... armv7l Linux
```

OpenRC reports `screen-console` as started. Its process has:

- fd 3 -> `/dev/fb0`
- fd 4 -> `/dev/ttyGS0`
- fd 5 -> legacy PTY master
- fd 6 -> `/dev/input/event1`

The shell attaches to the legacy PTY slave and is rendered on the phone
screen.

The current screen console auto-logs in as root by explicit project
choice. This is a local development/test system, not a hardened phone.
It should not be exposed to an untrusted USB host or network.

## postmarketOS port definitions

Initial pmaports recipes exist at:

```text
pmaports/device/testing/device-samsung-on5ltetmo
pmaports/device/testing/linux-samsung-on5ltetmo
```

They define:

- Device name and manufacturer.
- `armv7`.
- Handset chassis.
- External storage.
- 720x1280 screen.
- Exynos DTBH/qcdt generation.
- Samsung boot offsets and board format.
- `SEANDROIDENFORCE` append.
- `heimdall-bootimg` flash metadata.
- Kernel package and GCC 4 build metadata.

The APKBUILD parser accepts the recipes. A complete `pmbootstrap install`
was not run because the host's privileged `kpartx`/mount environment was
not available, so the current boot was assembled with the local scripts.

## Recovery procedure

From TWRP:

```sh
adb push builds/pre-zirarch-boot.img /data/pre-zirarch-boot.img
adb shell \
  'dd if=/data/pre-zirarch-boot.img \
      of=/dev/block/mmcblk0p9 bs=1M conv=fsync; sync'
adb reboot
```

To restore the known-good Linux boot preceding the display fix:

```sh
adb push builds/pre-screen-fix-boot.img /data/pre-screen-fix-boot.img
adb shell \
  'dd if=/data/pre-screen-fix-boot.img \
      of=/dev/block/mmcblk0p9 bs=1M conv=fsync; sync'
adb reboot
```

If boot is unresponsive:

1. Hold Power + Volume Down to force power off.
2. Hold Volume Up + Home + Power.
3. Enter the untouched TWRP recovery.
4. Restore `BOOT` from one of the saved images.

## What still needs porting

The following hardware/userspace areas are not yet integrated:

- Configuring a production Wi-Fi network and obtaining a DHCP lease from
  a selected access point.
- Audio routing and modem/telephony.
- Sensors exposed through normal Linux services.
- GPU userspace acceleration and a graphical compositor.
- Power management and suspend/resume behavior.
- Battery/charging integration in Alpine/OpenRC.
- Camera.
- Bluetooth.
- Touch calibration edge cases beyond the tested full-screen keyboard.
- Full terminal emulation for applications using unsupported CSI/OSC
  sequences.
- A fully generated postmarketOS filesystem and package build.
- Secure account setup; the current framebuffer console is intentionally
  auto-login root for development.

## Critical lessons

1. Do not use a generic Android boot-image packer for this phone. The
   DTBH, Samsung board string, header size, and `SEANDROIDENFORCE` trailer
   matter.
2. Do not overwrite RECOVERY, EFS, CPEFS, modem, or bootloader
   partitions.
3. Keep an exact `BOOT` dump before every experimental kernel flash.
4. `decon_3475` requires the 4 -> 0 blank sequence for reliable panel
   ownership handoff.
5. Raw framebuffer writes require `FBIOPAN_DISPLAY`; mmap plus `write`
   is not enough.
6. `decon_pan_display()` must configure and enable Window 0, release
   shadow protection, and start DECON, not merely change an address.
7. Do not assume devpts materializes a node on this old kernel. The PTY
   legacy fallback is required on the current firmware.
8. Do not interpret shell control bytes as touch bugs. ANSI sequences
   must be fully consumed.
9. Keep the recovery and USB ACM paths available while testing.
10. Keep a known-good boot image and fbconsole source tree in `backups/`
    before changing display, input, firmware, or boot behavior.
11. `prompt.md` is the authoritative task statement for the completed
    DECON, touchscreen-console, sleep/wake, and splash implementation.
