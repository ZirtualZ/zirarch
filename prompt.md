You're working on Zirarch: Debian trixie armhf + OpenRC on a Samsung Galaxy
On5 SM-G550T (3.10.108 kernel, 720x1280 portrait touchscreen, X11 fbdev,
SOFTWARE RENDERING, ~1.5 GiB RAM, slow Cortex-A7 CPU). Read CURRENT.md and
FieldM/README.md first. FieldM = GTK4 shell (FieldM/src/shell/) +
Xlib WM (FieldM/src/wm.c).

## Access & workflow: LIVE UPDATES ONLY
    ssh -i /home/marshal/.ssh/zirarch_ed25519 root@192.168.7.2
- First, snapshot the current working state: `git stash`-free approach:
  create a local branch/tag `fieldm-pre-redesign` (commit locally, don't
  push) so we can roll back.
- Edit on the host, rsync FieldM/ to /root/FieldM on the phone, build there
  with `make && make check-applications`, install, restart the session
  (pkill -x simple-desktop / simple-wm). GUI commands need DISPLAY=:0.
- Never rebuild images, flash, or write to partitions.
- Work in stages (below). After each stage, deploy, take a screenshot if
  possible (e.g. `DISPLAY=:0 import -window root` or xwd), and tell me what
  to check on the phone. Backport into build scripts at the end.

## Design goal
Redesign FieldM into a unique touch-first "cyber-minimalist" mobile shell.
Palette: status bar/WM chrome #0D1015, canvas #11151C, text #E6EDF3,
borders #21262D, accent #00E5A8 (mint/cyan) for press/focus.
Performance rules (software rendering): no blur, no shadows, no
animations/transitions, no transparency. Flat colors and 1-2px borders only.

## Sizing rule (important)
ALL pixel sizes in this spec are PHYSICAL screen pixels on the 720x1280
display. Do NOT use GDK_SCALE=2 (that halves the logical screen to
360x640 and doubles every size). Instead scale text only: set Xft.dpi
(~168) via xrdb in the session and/or GDK_DPI_SCALE, and size widgets in
CSS with physical px. simple-wm is Xlib and ignores GTK scaling: set its
title bar, fonts, and hit targets explicitly in wm.c/appearance.

## Stage 1: Text scaling + palette
Readable fonts across shell, launcher, settings, and WM titles; touch
targets >= 48px everywhere. Apply the dark palette globally in the GTK CSS
(keep the readability/contrast fix that already exists).

## Stage 2: Top status bar (44-48px, pinned to top)
- #0D1015 background, #E6EDF3 text, 1px #21262D bottom border.
- Left: time HH:MM + Wi-Fi state (from the existing wifi helper /
  wpa_cli status, read async; never block the UI).
- Right: live telemetry refreshed every 3 s via a GLib timeout:
  CPU % (delta between two /proc/stat samples), RAM % (/proc/meminfo:
  (MemTotal-MemAvailable)/MemTotal), root disk used % (statvfs("/")),
  battery % + charging (/sys/class/power_supply/*/ with type Battery;
  on this device likely .../battery/capacity and status).
- Format: "CPU 12% RAM 48% / 61% BAT 84%" (+ a charging mark). Measure it
  at the chosen font size; if it doesn't fit in 720px with the clock, use a
  compact form ("C12 R48 D61 B84%") rather than truncating.
- Missing data (e.g. no battery node) shows "--", never crashes.

## Stage 3: Home screen
- Canvas #11151C with subtle geometric accent styling (static, cheap to
  draw: thin lines/corner marks) and a "ZIRARCH" header/watermark badge.
- Centered 3-column grid of rounded square cards, 110x110px, with label
  underneath; icon or letter badge centered; #00E5A8 border on
  press/focus. Shortcuts: Terminal, Files, Browser, Settings, Packages,
  plus empty quick-launch slots the user can fill later (or omit if that
  adds too much complexity). Remove the "Apps" tile (Apps lives in the
  dock).

## Stage 4: Bottom thumb dock (68px) + WM
- Dock pinned to the bottom with: Apps (launcher), Keyboard (fieldm-keyboard
  manual toggle), and a Windows button (switcher listing running apps, each
  with a large close target).
- The status bar and dock must RESERVE screen space: set _NET_WM_STRUT /
  _NET_WM_STRUT_PARTIAL on their windows and make simple-wm honor struts
  when maximizing, so app windows fit between them. The on-screen keyboard
  (720x300) must appear ABOVE the dock and shrink the app area while
  visible, not cover the dock or be covered by apps.
- Exclude the FieldM shell/root window ("Field Desktop") from the
  taskbar/switcher; only real application windows appear.
- simple-wm title bars >= 48px with a >= 48x48 close (X) target, colors
  #0D1015 / #E6EDF3 / #00E5A8 accent for the focused window.

## Must keep working
fieldm-keyboard lease toggling (manual/entry/terminal), Alt+Space
launcher, Alt+Tab, hardware Back (Escape) behavior, the Wi-Fi settings
page, the Packages app, and `make` / `make check-applications` passing.
Report any of these that you had to change and why.
