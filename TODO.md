# FieldM known work

1. Keyboard locking and live entry-lease clearing are implemented; Recents and app-close stress remain hardware-interactive.
2. Recents rebuilds and PID/close fallback are implemented; stale-window and empty-state checks need a longer live app pass.
3. Vimb is removed and `fieldm-browser` is live ARM-compiled with URL/search, menu, zoom and downloads; AT-SPI search-box validation remains.
4. Home Pinned duplication is removed and Browser focus CSS is corrected; new home screenshots are captured.
5. Wi-Fi saved-network reuse, duplicate cleanup, Forget, 64-character PSK, dhclient fallback and saved reconnect are implemented.
6. Screenshot shortcut, lock screen, notifications, RAM log tmpfs, battery daemon and power presets are implemented; mode drain samples remain.
7. Quick settings includes power cycling and notification text; flashlight, rotation lock and notification clearing need remaining hardware integration.
8. PCManFM, xclip and browser file launch support are installed/prepared; auto-rotate remains conditional on sensor stability.
9. Hardware Test and framebuffer POST are live-polished; interactive hardware tests and the cold-power timing sample remain user-driven.
10. No image rebuild or flash was performed. `log_buf_len` and `vt.global_cursor_default=0` remain unapplied pending approval.
