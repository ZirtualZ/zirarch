# Zirarch Debian On5 - current SSD state

## Current target

This is the Debian/OpenRC build. It is not the Alpine native rootfs.

The root filesystem is mounted read-write and `/etc/fstab` uses pass number
`0`, so OpenRC does not run `fsck.ext4` on the already-mounted SYSTEM partition.
The `fsck` service is removed from every OpenRC runlevel.

FieldM is the default graphical boot mode. `/run/zirarch-boot-mode` is created
with `gui` unless it already contains an explicit boot mode.

## RNDIS SSH

Root SSH allows public-key login and password login.

On the host:

```sh
sudo ip addr replace 192.168.7.1/24 dev enp0s20f0u9
sudo ip link set enp0s20f0u9 up
ping 192.168.7.2
ssh root@192.168.7.2
```

Use the RNDIS interface name shown by `ip -brief link`; it may differ between
host boots.

## Rebuild

From `/mnt/zirarch/zirarch`:

```sh
sudo env ZIRARCH_ROOT_PASSWORD=<root-password> ./build-debian-on5.sh
```

This rebuilds the Debian rootfs, SYSTEM image, BOOT image, and kernel artifacts.

## Flash

Boot the phone into TWRP, confirm `adb devices -l` reports `recovery`, and run:

```sh
cd /mnt/zirarch/zirarch
./flash.sh
```

`flash.sh` writes only BOOT (`mmcblk0p9`) and SYSTEM (`mmcblk0p19`) and reboots
the phone. Live backups are stored under `backups/`.

## Live checks

```sh
uname -a
rc-status default
cat /run/zirarch-boot-mode
pgrep -a -x simple-desktop
pgrep -a -x simple-wm
DISPLAY=:0 xrandr -q
cat /etc/fstab
rc-update show boot
```
