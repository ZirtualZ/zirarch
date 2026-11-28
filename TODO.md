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
10. Browser: singleton lock prevents duplicate launches; Menu → Close browser and Alt+Left on no history exit the PID. Recents close uses SIGTERM then SIGKILL after 1 s. PCManFM/Thunar/Nemo Back closes the active file-manager window and kills its PID; live PCManFM PID 5934 verified closed.
