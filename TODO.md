# FieldM known bugs

1. Physical key repeat/hold/blank-screen live matrix remains; Home, Back, and Recents now use persistent X/XTest.
2. NetSurf text-entry, navigation, scrolling, and zoom need real-touch verification.
3. Recents close-one, switch, and Clear-all touch testing remains.
4. `_NET_CLOSE_WINDOW` fallback needs testing for apps that ignore WM_DELETE_WINDOW.
5. Matchbox dock/strut behavior is verified; `_NET_WORKAREA` shrinks to `720x960`.
6. AT-SPI fieldm-imd is installed and debounced; GTK/Browser text-focus behavior needs live site-specific verification.
7. Terminal Ctrl+C, arrows, Backspace, repeat, and zoom need live tests.
8. Wi-Fi and Settings need a final low-memory/recovery pass.
9. Long-term open/close/switch stress beyond the first 10-app pass remains.
10. Browser and Recents screenshots are recorded in `screenshots/browser-keyboard.png` and `screenshots/recents-over-app.png`.
