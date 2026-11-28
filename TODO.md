# FieldM known bugs

1. Physical-key behavior still needs live matrix verification for repeat, hold, and blank-screen states.
2. NetSurf text-entry, navigation, scrolling, and zoom need real-touch verification.
3. Recents needs real close-one, switch, and Clear-all touch testing.
4. `_NET_CLOSE_WINDOW` close fallback should be tested for apps that ignore WM_DELETE_WINDOW.
5. Matchbox dock/strut behavior is verified: `_NET_WORKAREA` shrinks to `720x960` when the 320px dock is visible.
6. The keyboard should only open for real text focus, not every app tap.
7. Terminal Ctrl+C, arrows, Backspace, repeat, and zoom need live tests.
8. Wi-Fi and Settings need a final low-memory/recovery pass.
9. Long-term open/close/switch stress test beyond the first 10-app pass.
10. Recorded `screenshots/home.png`, `screenshots/terminal.png`, and `screenshots/recents.png`; add Settings/Browser screenshots later.
