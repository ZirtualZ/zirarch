# Hardware follow-ups

## Bluetooth

No `/sys/class/bluetooth/hci0` was present and no `hciattach`/`btattach`
binary was installed. Broadcom firmware data exists for Wi-Fi
`/lib/firmware/bcmdhd_sta.bin`, but no confirmed Bluetooth firmware or
Bluetooth controller attach tool was found. Status: NOT FOUND, do not
guess at a firmware write.

## Camera

Many `/dev/video*` nodes are present. `v4l2-ctl` and `ffmpeg` were not
available on the live image; a live launcher probe reported no usable
capture tool/frame. Status: NEEDS CHECK.

## GPS

`/sys/class/sec/gps` exists. No GNSS node or GPS control tool was
identified; status: PROBE ONLY.

## Suspend

`/sys/power/state` supports `freeze mem`. Real suspend test was declined.
Suspend remains disabled by default behind `/var/lib/zirarch/suspend-enabled`.
The software hook restores the current CPU/power mode and Wi-Fi reconnect
can be started after wake.

## Standby drain

Not measured. One-hour screen-off/suspend capacity and Wi-Fi/touch resume
tests remain hardware-interactive.
