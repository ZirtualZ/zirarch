# FieldM known bugs

1. Live confirmed devices: `sec_touchscreen:event1`, `gpio_keys.22:event5`; `KEY_BACK=158` press/long-toggle logging is confirmed. Accept `KEY_MENU=139`, `KEY_RECENT=254`, `KEY_APPSELECT=580`, `KEY_HOME=102`, `KEY_HOMEPAGE=172`.
2. Browser: Back sends Alt+Left; Home shows desktop; Recents opens shell Recents. NetSurf text-entry/zoom real-touch remains.
3. Recents switch and touch testing remain; `_NET_CLIENT_LIST` and `_NET_ACTIVE_WINDOW` are implemented.
4. `_NET_CLOSE_WINDOW` fallback needs testing for apps that ignore WM_DELETE_WINDOW.
5. Matchbox dock/strut behavior is verified; `_NET_WORKAREA` shrinks to `720x960`.
6. AT-SPI fieldm-imd is installed and debounced; manual keyboard toggle is verified, with Browser text-focus site testing remaining.
7. Terminal: Back returns Home, physical keyboard toggle remains supported, Ctrl+C/typing live tests pass.
8. Settings/Packages: Back follows shell/escape table; Wi-Fi low-memory pass remains.
9. Long-term open/close/switch stress beyond the first 10-app pass remains.
10. Browser and Recents screenshots are recorded in `screenshots/browser-keyboard.png` and `screenshots/recents-over-app.png`.
