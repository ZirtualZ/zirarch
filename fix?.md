Debian boots (sysvinit 3.14 + OpenRC 0.56) but stalls/loops. Fix:
1. Our OpenRC services (screen-console, zirarch-boot-mode,
   zirarch-x11-refresh) "need devfs", which doesn't exist on Debian's
   OpenRC. /dev is already devtmpfs from the initramfs. Remove the devfs
   dependency (use "after localmount" if ordering is needed).
2. Remove udev from the boot entirely: Debian's udev is systemd-udevd and
   doesn't support kernel 3.10, and its LSB init script stalls 60s under
   OpenRC ("run from an interactive shell"). Disable udev/udev-trigger in
   all runlevels. Configure Xorg without udev: AutoAddDevices false, explicit
   evdev InputDevice sections for /dev/input/event1 (sec_touchscreen) and
   any keyboard devices, mirroring how the Alpine build handled input.
3. Port the Alpine RTC/time sync from usb-rndis.start, and add OpenRC's
   swclock (or fake-hwclock) so time never goes backwards across boots.
4. Find what is looping: check rc-status and /var/log after boot (SSH over
   RNDIS if it comes up). Make sure the RNDIS + sshd service has no
   dependency on udev or devfs so we can always get in.
Rebuild the image and flash it over adb
