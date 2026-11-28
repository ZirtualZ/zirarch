# Zirarch boot timing

Measured on `on5-debian` with `/var/log/zirarch-boot-timing.log`, `/var/log/rc.log`,
and `/var/log/boot-dmesg.log`.

| Phase | Baseline reboot 1 | Baseline reboot 2 | Lean reboot 1 | Lean reboot 2 | Lean reboot 3 | Lean reboot 4 | Lean reboot 5 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Kernel/freeing | ~11.253 s | ~11.253 s | ~11.253 s | ~11.253 s | ~11.253 s | ~11.252 s | ~11.254 s |
| boot_mode_start | 26 s | 25 s | 25 s | 19 s | 19 s | 16 s | 15 s |
| FieldM first shell | 33 s | 33 s | 33 s | 27 s | 27 s | 24 s | 23 s |

The measured boot improvement is 10 s at `boot_mode_start` and 10 s at
FieldM start relative to baseline. The target of a 15 s FieldM frame was not
reached; first-frame work remains about 23 s after boot.

## Removed or deferred in lean reboots

- `networking`/ifupdown boot path; RNDIS is now a static recovery service.
- `kmod` and module loading: live `lsmod` reported 0 modules.
- `fake-hwclock`: RTC `/dev/rtc0` is readable, so `hwclock.sh` remains.
- `bootmisc.sh`, `checkfs.sh`, `mountnfs.sh` and their bootclean helpers.
- `mountall.sh`, `mountall-bootclean.sh`, `checkroot.sh` and
  `checkroot-bootclean.sh`: initramfs has already mounted the root.
- `lm-sensors`, `cgroups`, `procps`, `urandom`, `mount-configfs`,
  `mountdevsubfs.sh`, `x11-common`, `brightness`, `hostname.sh`,
  `rc.local`, `rmnologin` and `bootlogs` where not required by the FieldM
  path.
- `elogind` and `zirarch-x11-refresh`: FieldM/X were tested without them.
- `buffyboard`: the tty recovery path uses getty directly.

## Default services remaining

| Service | Reason |
|---|---|
| dbus | Required by GTK/AT-SPI/FieldM |
| fieldm-bind-mounts | `/data` mounts and SSH guard |
| fieldm-logs | RAM-backed FieldM logs |
| zirarch-boot-mode | Starts TTY or FieldM |
| fieldm-keys | Physical hardware keys/screen control |
| zirarch-rndis | Recovery SSH over USB |
| zirarch-sshd | Recovery SSH |
| wifi-link / wpa-supplicant-zirarch | Wi-Fi after FieldM |
| zirarch-clock / zirarch-clock-sync | Clock restore/sync after FieldM |
| zirarch-haveged | Entropy after FieldM |
| zramswap | Optional memory compression after FieldM |
| zirarch-battery | Battery notices/power |
| zirarch-suspend | Screen suspend toggle (default off) |
| zirarch-alarm | Next RTC alarm setup |

## SSH recovery

Both lean reboots returned RNDIS SSH. `/root/.ssh/authorized_keys` remained
present and `/data` bind fallback was guarded.
