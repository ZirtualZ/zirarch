# FieldM known work
1. Hardware buttons tested on-device: Power 116, Home 172, Back 158, Recents 254, Volume Up 115 and Volume Down 114; `fieldm-keys` is running.
2. Keyboard stability passed 20 entry/leave/terminal cycles with 0 failures and at most one live `fieldm-kbd`.
3. Real Power blank/wake and lock/Home unlock passed; lock stays above and restores the previous app. Screenshot: `screenshots/lock-screen.png`.
4. Battery modes tested live through sysfs: Normal 0-2, Medium 0-1, Extreme 0; Medium/Extreme use `userspace` fallback because 3.10 lacks conservative/powersave.
5. Settings > Battery buttons and live readback table are implemented; screenshot: `screenshots/battery-modes.png`.
6. Quick settings power tile is implemented and screenshot: `screenshots/quick-settings.png`; automated click cycling of medium->extreme remained unverified.
7. Browser/lock stale-process cause was identified and test PIDs removed; no image or partition write was performed.
8. Remaining hardware-interactive checks: audio, vibration, sensors, cameras and 30-minute drain samples are untested.
9. A cold-boot timing sample and rebuilt-image boot test are untested; no log_buf_len or cursor cmdline change was applied.
10. Boot lean: FieldM measured at 23 s, boot-mode at 15 s; SSH, Wi-Fi, clock, keyboard and lock remain live. Suspend stays off/untested, alarm RTC setup untested in suspended state, MPv/ALSA package install untested, and no kernel/boot image flash.
