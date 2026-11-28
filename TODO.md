# FieldM known work

1. Hardware button aliases 139/254/580, 102/172 and 158 are implemented and live-confirmed; contextual Back and the Recents-over-app path are implemented.
2. Vimb URL-bar focus opens the keyboard and retains the entry lease; a real DuckDuckGo page focus test still needs a reachable network/content run.
3. Browser cold-start/RAM and network clock sync are recorded; no further browser work is blocking.
4. Early-boot entropy, firmware timeout, framebuffer reset and splash are live-verified; a measured cold power-on timing sample is still needed.
5. `log_buf_len` and `vt.global_cursor_default=0` cmdline proposals remain intentionally unapplied pending approval.
6. Light/dark switching is implemented for FieldM, keyboard, Packages, browser and xterm; the POST renderer uses the saved palette.
7. POST is framebuffer-native, first-boot gated, input-skippable and hands display ownership to FieldM; Volume Down hold testing remains hardware-interactive.
8. Hardware Test automatic probes, timeout isolation, logs, export, details and screenshots are complete; interactive display/touch/audio/vibration/sensor/camera checks remain user-driven.
9. A full cold boot on a rebuilt image is intentionally untested; no images were rebuilt, flashed or partition-written.
10. Keep new diagnostic screenshots under `screenshots/`; do not flash EFS, CPEFS, RADIO, RECOVERY or bootloader partitions.
