# Zirarch / FieldM stability and UX roadmap

This roadmap describes what to harden, test, and improve before FieldM can be
treated as a usable daily interface. It is deliberately ordered:

1. make the device recoverable from every UI failure;
2. eliminate crashes, hangs, blocked input, and resource leaks;
3. repair window management, keyboard, app, and shell behavior;
4. add repeatable host and live-device tests;
5. improve information architecture and visual design;
6. modernize and package the system for a maintainable release.

## Implementation status — 2026-11-27

- **Completed:** Phase 0.1–0.3 and Phase 1.1–1.4 implementation work is in
  place, with live ARM binaries and supervisor scripts deployed to the
  SM-G550T for smoke testing.
- **Live baseline:** the device is in GUI mode with Xorg, `simple-wm`,
  `simple-desktop`, `fieldm-kbd`, and `fieldm-keys` running.
- **Live deployment:** `scripts/live-deploy.sh` builds the FieldM C binaries
  directly on the ARM phone, preserves target-side backups, verifies SHA-256
  checksums, replaces only live FieldM binaries/scripts, and supports
  `--rollback`. It never writes BOOT or SYSTEM partitions.
- **Remaining:** hardware acceptance tests, repeated kill/restart tests,
  100-event hardware-key matrices, touchscreen real-tap tests, and any
  follow-up fixes found by live use remain open.

Do not skip directly to visual polish. A redesigned unstable system is still
unstable. Each phase should end with a short live-device check and a rollback
path. Partition flashing is not part of routine implementation or testing.
Only BOOT and SYSTEM may be written, and only after an explicit TWRP and backup
procedure.

## Definition of a stable FieldM

A stable build should meet all of these conditions:

- it boots to the GUI or an accessible console without manual repair;
- a frozen desktop or app can always be recovered with hardware keys;
- touching and typing do not leak events or leave stale focus;
- opening, switching, minimizing, maximizing, resizing, and closing apps is
  predictable;
- Recents contains only real app windows and can close one or all windows;
- the keyboard never covers controls it should not cover and never traps focus;
- Wi-Fi, battery, clock, settings, package operations, and app launching report
  real state instead of silently failing;
- the same source build passes host checks and a documented Xephyr/X11 check;
- live smoke tests leave the system usable afterward;
- no known process loop, zombie buildup, memory growth, or fixed-size collection
  silently strands the user.

## Current known-state summary

The project already has useful pieces:

- a native 720x1280 GTK 4 shell;
- a custom C X11 window manager;
- native and fallback on-screen keyboard support;
- hardware-key handling and emergency FieldM restart;
- terminal, Recents, drawer, settings, Wi-Fi, status, and package surfaces;
- Alpine and Debian boot/rootfs build paths;
- framebuffer, RNDIS, OpenRC, boot-selector, and recovery documentation.

The largest instability risks are currently architectural rather than cosmetic:

- `MAX_CLIENTS = 128` and application capacity `512` are fixed and can silently
  fail;
- application child processes are forked without a reaping lifecycle;
- process and X event handling is distributed across shell, WM, keyboard, and
  hardware-daemon code;
- close, focus, fullscreen, keyboard, title-bar, strut, and Recents behavior
  are coupled to several independent X properties;
- preference writes are not atomic or uniformly XDG-compliant;
- live testing has not demonstrated every keyboard and Recents action;
- host tests cover only one desktop-entry fixture;
- two boot modes and several duplicated/legacy overlays make image behavior
  harder to reason about;
- the Debian build enables root password login and the native build has
  inconsistent keyboard/install coverage.

---

# Phase 0: freeze scope and establish a recovery baseline

## Step 0.1: Establish one supported target for this stabilization cycle

- Choose Debian/OpenRC or Alpine as the primary target.
- Preserve the other target as a documented compatibility build, not a
  co-equal source of truth.
- Record the exact kernel commit, toolchain, rootfs, FieldM binary versions,
  SCREEN geometry, and connected-device serial in a generated build manifest.
- Preserve a known-live BOOT backup and SYSTEM backup before any later live
  deployment.
- Establish a one-command "recover live binaries without flashing" procedure.
- Define a console fallback that can always re-enter the GUI.

**Acceptance:** after an SSH deploy, a bad FieldM binary can be replaced and
restarted within minutes without reflashing.

**Status:** Implemented for live binaries. The deployment script builds ARM
code on the phone and uses target-side backups and checksum verification.

## Step 0.2: Add a deployment manifest

- Generate host-side SHA-256 values for every deployed binary and script.
- Generate the target-side version and SHA-256 values after deployment.
- Include `uname -a`, `/proc/cmdline`, package versions, git revision, build
  timestamp, and service status.
- Store non-sensitive deployment logs outside the repository or in a clearly
  ignored directory.
- Add a rollback manifest naming the previous working binaries and scripts.

**Acceptance:** every live issue can be correlated with an exact deployed
build and an exact previous build.

**Status:** Implemented in `scripts/live-deploy.sh`; host and target SHA-256
data is written under ignored `builds/live-deploy/` and rollback backups are
stored outside the repository on the phone.

## Step 0.3: Define explicit hardware recovery

- Document Power, Back, Home, Recents/Menu, Volume Up, and Volume Down behavior.
- Document the Power+Volume Up five-second emergency restart.
- Ensure hardware-key handling starts before X and survives X/session restarts.
- Remove or clearly name any duplicate key handling in overlays and init scripts.
- Add a visible, quiet, or short feedback mode for blank, restart, and recovery.

**Acceptance:** three emergency restart tests pass while an app is open, while
the keyboard is visible, and while the device is blanked.

**Status:** Hardware handling, monotonic chord timing, blanking, and emergency
session restart are implemented. Repeated live acceptance tests remain open.

---

# Phase 1: emergency recovery and process supervision

## Step 1.1: Make the session supervisor real

Replace "start and hope" behavior in `fieldm-session`,
`zirarch-boot-mode`, and `desktop.c` with a bounded supervisor state machine:

- detect missing X, dead WM, dead shell, dead keyboard, or dead hardware daemon;
- restart only the failed component;
- rate-limit restart loops;
- enter recovery mode after repeated failures;
- never restart an app process that was intentionally closed;
- log PID, exit status, signal, and elapsed runtime;
- expose current supervisor state for SSH and the framebuffer console.

**Acceptance:** a killed `simple-wm`, `simple-desktop`, `fieldm-kbd`, or Xorg
process is recovered or clearly reported instead of entering a hidden reboot
loop.

**Status:** Implemented bounded restart, recovery state, component-specific
restart, PID/exit logging, and 5-failure/300-second limits. Live fault
injection remains open.

## Step 1.2: Harden `fieldm-keys`

Audit `fieldm-keys.c` against these invariants:

- Power actions occur only on release;
- short Power release blanks/unblanks only when no other key is participating;
- Volume Up is released-only and suppressed in the restart chord;
- Power+Volume Up timing is based on monotonic time;
- a key repeat cannot keep a chord alive;
- stale held-key state is corrected after device reopen or process restart;
- child processes from Power commands are reaped;
- a touchscreen EVIOCGRAB is held only while blanked;
- EVIOCGRAB failures are logged;
- unblanking restores the prior brightness and framebuffer state;
- all key devices are reopened when hotplugged or disappear;
- daemon logs have bounded size or rotation.

**Acceptance:** hold/release combinations produce deterministic actions in a
repeatable 100-event test matrix.

**Status:** Implemented release-only Power/Volume behavior, monotonic timing,
repeat suppression, hotplug reopening, child reaping, bounded logging,
touchscreen grab lifecycle, and blank-state brightness restoration.
The 100-event matrix remains open.

## Step 1.3: Repair touchscreen blanking

- Confirm `/dev/input/event1` is the same physical touch event used by Xorg.
- Request EVIOCGRAB from `fieldm-keys` only after each blank state is committed.
- Release EVIOCGRAB before restoring the screen.
- Ignore stale queued touch events after unblank.
- Make Home on a blank screen unblank, or define that behavior explicitly.
- Test with real touch taps rather than XSendEvent/XTest.

**Acceptance:** no app or shell control is activated while the screen is
blanked.

**Status:** Implemented release-before-unblank, stale-touch draining, and
touchscreen `EVIOCGRAB` scope. Real-tap confirmation remains open.

## Step 1.4: Make emergency restart atomic enough to recover

- Stop lease holders and clear leases before starting a new session.
- Terminate descendants, not only visible top-level PIDs.
- Send SIGTERM, wait a bounded interval, then SIGKILL.
- Verify the old X server is gone before starting a new one.
- Restart through the same boot-mode/OpenRC path used at boot.
- Add log checkpoints before, during, and after restart.
- Never let `fieldm-keys` be killed or restarted as part of the GUI restart.

**Acceptance:** the emergency chord passes repeated app-open and keyboard-open
tests and leaves no stale Xorg/WM/processes.

**Status:** Implemented lease clearing, descendant termination,
SIGTERM/SIGKILL bounds, Xorg verification, boot-mode restart, and
`fieldm-keys` process preservation. Repeated live acceptance tests remain open.

---

# Phase 2: window manager correctness

## Step 2.1: Remove fixed and silent client limits

Convert `clients[MAX_CLIENTS]` and pending-close storage from fixed arrays to
dynamic storage, or choose a documented overflow policy that never leaves a
window invisible.

- dynamically grow active-client storage;
- dynamically grow pending-close storage;
- return a visible error or intentionally reject a client on catastrophic
  allocation failure;
- add a test that opens and closes more than 128 windows;
- add a test that fills and frees the collection repeatedly.

**Acceptance:** opening windows cannot leave a successfully mapped X client
stranded and memory remains bounded over repeated open/close cycles.

**Status:** Implemented dynamic managed-client and pending-close
collections with growth, shrink, and allocation/rejection paths. Live
SM-G550T deployment and source invariant checks pass; open/close stress beyond
128 windows remains to be tested.

## Step 2.2: Centralize managed-window invariants

Create named WM helpers for:

- desktop/shell/dock/keyboard classification;
- named, unnamed, and fallback title lookup;
- dialog detection;
- terminal detection;
- fullscreen detection;
- close support;
- focus and raise operations;
- work-area and keyboard geometry;
- safe frame resize and arrange operations.

Then reduce direct state mutation spread through `manage_window`,
`maximize_client`, `arrange_client`, `focus_client`, and event handlers.

**Acceptance:** geometry and focus transitions remain correct after an app
fullscreen toggle, keyboard resize, screen configure, close, or recents action.

**Status:** Partial. Named geometry, work-area, fullscreen, and keyboard
clamping helpers are being consolidated; rapid live geometry/focus tests remain.

## Step 2.3: Implement close as a durable protocol

For every close request:

- resolve a frame or client to one specific managed client;
- send `_NET_CLOSE_WINDOW` from Recents or another external surface;
- let `simple-wm` locate and close that exact client;
- send `WM_DELETE_WINDOW` only if the client advertises it;
- schedule and track a per-client close deadline;
- use `XKillClient` only after the deadline or when no close protocol exists;
- cancel pending-close tracking on destroy/unmap;
- remove stale clients from Recents immediately;
- refresh Recents after close and close-all.

Test Terminal, htop, BTOP++, plain Xterm, a dialog, and an app that ignores
`WM_DELETE_WINDOW`.

**Acceptance:** close works for supporting and non-supporting apps, Clear all
closes every real app, and no WM pending-close entry survives forever.

**Status:** Partial. `_NET_CLOSE_WINDOW`, `WM_DELETE_WINDOW`, pending deadlines,
and `XKillClient` fallback are implemented; live protocol tests remain.

## Step 2.4: Remove title bars and chrome correctly

- Implement fullscreen by removing both border and title decorations.
- Ensure `_NET_WM_STATE_FULLSCREEN` and app-requested fullscreen are consistent.
- Keep title bars and close controls for normal portable windows.
- Keep Settings as a shell-managed surface with its own Back header.
- Remove ZIRARCH shell frames from Recents and taskbar management.
- Prevent content from disappearing underneath frame chrome or keyboard struts.

**Acceptance:** fullscreen terminal/video/app content reaches the physical
screen edge and normal windows remain distinguishable and closeable.

**Status:** Partial. Fullscreen and normal frame state are implemented; live
fullscreen/chrome verification remains.

## Step 2.5: Improve geometry and struts

- model status bar, taskbar, keyboard, and dock as one work-area API;
- never place a window outside 0,0 / 720,1280;
- clamp width and height to positive usable bounds;
- preserve dialog size but center it in the usable region;
- relayout every client on keyboard map/unmap/resize and screen configure;
- subtract keyboard height only from the focused terminal or text-entry context;
- verify `_NET_WORKAREA`, `_NET_WM_STRUT`, and `_NET_WM_STRUT_PARTIAL`;
- decide whether keyboard remains override-redirect or becomes a managed dock.

**Acceptance:** no app is covered by the keyboard, no app becomes permanently
off-screen, and taskbar/status geometry survives rotation-independent 720x1280
changes.

**Status:** Partial. `_NET_WORKAREA`, keyboard-aware work area, and clamp
helpers are implemented; live 720x1280/keyboard relayout tests remain.

## Step 2.6: Repair focus and switcher behavior

- focus exactly one managed client;
- remove stale `focused` indices after destroy;
- make switcher navigation wrap safely;
- close switcher on outside tap and Esc;
- forward Recents selection to X with `_NET_ACTIVE_WINDOW`;
- ensure keyboard focus and X input focus differ intentionally;
- test rapid Alt+Tab and direct touchscreen switching.

**Acceptance:** no close/focus action targets the wrong window after 100 open,
switch, and close operations.

**Status:** Partial. Focus indices are repaired on destroy and Recents uses
`_NET_ACTIVE_WINDOW`; rapid 100-action stress remains.

---

# Phase 3: on-screen keyboard

## Step 3.1: Verify and repair keyboard event dispatch

- process one X event or, preferably, drain every pending X event;
- continue handling events while `Xlib` already has data buffered;
- do not call `XSync` in ordinary press/release paths;
- flush only when an XTest key event or state change needs it;
- redraw only the pressed/released key;
- keep full redraw limited to Expose and layout/state changes;
- add event counters and latency logging.

**Acceptance:** 40+ character rapid typing and page switching show no character
drops or frozen UI.

## Step 3.2: Make key activation predictable

Choose and document one activation model:

- activate on release for normal keys;
- identify the originally pressed key, not the release-under-finger key;
- tolerate finger drift up to 24 px;
- cancel activation beyond the drift threshold;
- commit page and modifier actions on press or release consistently;
- retain long-press alternate and repeat behavior without duplicate activation.

**Acceptance:** the letters, symbols, terminal, and close controls switch and
activate only when the user intends.

## Step 3.3: Fix layout and font sizing

- compute font size from key height and measured label extents;
- remove fixed `Xft.dpi` assumptions for keyboard label clipping;
- use symbols for Backspace, Enter, Shift, page switches, terminal toggle, close,
  and zoom;
- add safe fallback fonts for missing Unicode glyphs;
- test labels in letters, symbols, terminal, terminal+symbols, 56 px, 64 px,
  and 72 px heights;
- replace `A-` ambiguity with an explicit, accessible zoom icon and tooltip/log
  text.

**Acceptance:** no key label clips and every key remains usable at every setting.

## Step 3.4: Integrate keyboard visibility and geometry

- make lease updates synchronous and confirmed;
- always signal or watch the keyboard after manual, entry, and terminal leases;
- add inotify or one small Unix state socket instead of relying only on SIGHUP;
- stop stale keyboard processes and stale lease files;
- prefer a managed dock window over override-redirect;
- if override-redirect remains, have the WM honor its struts explicitly;
- ensure the keyboard never accepts input focus;
- ensure the keyboard is always raised without covering the status bar.

**Acceptance:** opening a text field shows the keyboard, closing all text fields
hides it, and switching app context updates terminal-row state immediately.

## Step 3.5: Verify terminal controls

- confirm Shift+KeypadSubtract is the correct xterm zoom-out chord in all
  terminal applications;
- add alternative escape/control sequences where needed;
- test Ctrl, Esc, Tab, arrows, Backspace hold, repeat, and Enter;
- test drawer search and terminal at 40+ characters per rapid sequence;
- measure keyboard press-to-XTest event latency.

**Acceptance:** repeated terminal typing and zoom tests pass and character
latency is below a documented target.

---

# Phase 4: app launch and shell lifecycle

## Step 4.1: Reap and supervise child processes

Fix `launch_arguments()` and every `fork()`/`g_spawn` launch path:

- set the desktop to reap short-lived processes safely;
- distinguish process launches from shell session ownership;
- close unused descriptors;
- consume or log child errors;
- prevent zombie buildup;
- track terminal and package process lifetime;
- do not make shell exit depend on a child app.

**Acceptance:** repeated launches leave no zombies and shell RSS is stable.

## Step 4.2: Complete desktop-entry support

Expand `read_desktop_entry()` to handle:

- exact and localized `Name` and `Comment`;
- `TryExec`, `Hidden`, `NoDisplay`, `OnlyShowIn`, and `NotShowIn`;
- categories and duplicate desktop IDs;
- quoted Exec fields;
- field codes `%f`, `%F`, `%u`, `%U`, `%i`, `%c`, `%k`, and `%%`;
- terminal entries;
- malformed and duplicate entries;
- environment and working-directory constraints.

Add fixtures for visible, hidden, terminal, localized, quoted, and unsupported
entries.

**Acceptance:** app discovery is deterministic and matches FreeDesktop desktop
entry visibility rules that the project claims to support.

## Step 4.3: Remove fixed application and app-icon constraints

- replace the 512-app array with dynamic storage or a clearly enforced overflow
  policy;
- remove duplicate Terminal aliases intentionally;
- add missing browser and file-manager fallbacks;
- stop silently discarding entries after capacity;
- implement real icon lookup rather than treating `icon` as a discard-only field;
- cache icon lookup and handle theme changes;
- add fallbacks for missing icon themes.

**Acceptance:** installing/uninstalling apps updates the drawer and no valid app
is silently omitted.

## Step 4.4: Make shell state resilient

Audit `gtk_shell.c`, `desktop_view.c`, `launcher.c`, and `settings.c`:

- close stale surfaces instead of only hiding them;
- centralize active-shell-page state;
- add shell-only test mode for dialogs and WM unavailable states;
- log X connection loss and WM exit as fatal session errors;
- clean GSources and GSubprocess objects on shutdown;
- make application launch failure show a useful error;
- stop shell exit from being dependent on a child.

**Acceptance:** closing every surface and relaunching apps over 100 cycles
doesn't leak GTK windows or X sources.

## Step 4.5: Atomic preference handling

Make preference writes consistent and safe:

- honor `XDG_CONFIG_HOME` everywhere;
- use `HOME/.config` only as the default;
- write temp files in the destination directory;
- fsync temp file and rename atomically;
- keep the previous preference on write failure;
- validate values before save;
- centralize paths in one config module;
- add concurrent/rapid write tests.

Cover:

- feature settings;
- pinned apps;
- app display sizes;
- keyboard settings;
- appearance/theme/wallpaper settings;
- package and Wi-Fi state that belongs outside system files.

**Acceptance:** interrupted writes preserve prior preferences and custom
`XDG_CONFIG_HOME` is honored.

---

# Phase 5: Recents, navigation, and app-closing UX

## Step 5.1: Remove blank and fake Recents cards

Filter and name cards using one shared client predicate:

- exclude shell, desktop, dock, switcher, keyboard, unnamed, override-redirect,
  desktop, and utility-only windows;
- use `_NET_WM_NAME`, then `WM_NAME`, then XClassHint as fallback;
- add per-app stable IDs and names;
- refresh on map, unmap, destroy, title/property, and close;
- keep at least two real app windows visible in the live screenshot.

**Acceptance:** Recents contains only closable app windows and never blank cards.

## Step 5.2: Close one and Clear all

- close buttons send a close request for their exact window;
- remove the card immediately or update it with a closing state;
- Refresh after close and after the WM confirms destruction;
- Clear all iterates real app clients only;
- prevent repeat taps from duplicate close requests;
- report if a window cannot close in three seconds.

**Acceptance:** Terminal, htop, BTOP++, packages, settings, and plain xterms
close one-by-one and with Clear all.

## Step 5.3: Navigation model

- define Back/Home/Recents as hardware navigation gestures consistently;
- let Back go back inside Settings and then close its current page;
- ensure Back on an empty terminal history does not kill the terminal;
- make Recents reachable even when the keyboard or settings is open;
- make Home return to the shell without accidentally launching apps;
- keep app close out of the title bar as a primary navigation action.

**Acceptance:** hardware and touchscreen navigation do not become trapped in
launcher/settings/keyboard/Recents combinations.

---

# Phase 6: live tests and automation

## Step 6.1: Replace weak host smoke tests

Add unit or fixture tests for:

- application discovery and desktop entries;
- Exec parsing and field codes;
- terminal launch argument construction;
- XDG config paths;
- preference save/restore;
- window geometry calculations;
- key layout and font fitting;
- Recents filtering;
- close protocol support;
- hardware key state machine;
- supervisor restart state.

**Acceptance:** `make test` passes on the host without requiring the phone.

## Step 6.2: Make characterize.sh reliable

Upgrade `FieldM/tests/characterize.sh`:

- wait for processes and windows with bounded, reported retries;
- clean every temp process;
- detect stale Xephyr/display state;
- test shell, launcher, settings, xterm, close, keyboard, and Recents;
- emit structured pass/fail output;
- record screenshots and metrics only with `--record`;
- return zero only for actual assertions.

**Acceptance:** verify mode consistently passes or gives a named failing check.

## Step 6.3: Add live SSH smoke scripts

Create a single documented live smoke command that:

- verifies RNDIS, SSH, display, input, and expected PIDs;
- opens Terminal;
- types 40+ characters;
- opens htop or BTOP++;
- switches Recents;
- closes one app;
- opens and closes a second app;
- clears all;
- switches keyboard pages;
- opens/closes quick settings;
- verifies screen blank/unblank;
- verifies emergency restart;
- captures logs and screenshots.

**Acceptance:** the script returns pass/fail and a copy-pasteable diagnostic
bundle after live runs.

## Step 6.4: Add repeat and soak tests

- 100 launch/close cycles;
- 1000 key events with character accounting;
- 100 page switches;
- 100 keyboard map/unmap cycles;
- 100 Recents open/close/clear cycles;
- Wi-Fi scan/connect/disconnect cycles;
- package search/install simulation where safe;
- memory/RSS and process/PID trends over 30 minutes.

**Acceptance:** no crash, leak, zombie, stale window, or input lock appears.

## Step 6.5: Add screenshot baselines

Capture and commit only intentional, manually inspected screenshots:

- home;
- drawer;
- Recents with exactly two real apps;
- letters keyboard;
- symbols keyboard;
- terminal keyboard;
- terminal typing test;
- htop;
- settings;
- quick settings;
- blank recovery feedback.

Remove outdated screenshots that cannot be reproduced.

**Acceptance:** screenshot baselines map to exact build revision and live target.

---

# Phase 7: build and release hardening

## Step 7.1: Make build targets explicit

Split and name builds:

- host UI build;
- FieldM ARM build;
- kernel build;
- Alpine rootfs build;
- Debian rootfs build;
- boot image build;
- live deploy only;
- live flash only.

Make release and clean targets remove only generated outputs.

**Acceptance:** `build.sh` and new named targets are reproducible and do not
surprise users with destructive image operations.

## Step 7.2: Consolidate duplicated sources

Find and remove divergence between:

- `FieldM/` and overlay copies;
- Alpine and Debian binary/install paths;
- native and Debian boot scripts;
- old and new keyboard scripts;
- legacy Onboard and native `fieldm-kbd`;
- duplicated terminal entry files;
- live phone binaries versus repository sources.

Select one source of truth and verify overlay application does not overwrite
untracked user changes.

**Acceptance:** a clean bootstrap plus build produces exactly the documented
FieldM files and no hidden live-only source dependency.

## Step 7.3: Pin and verify dependencies

- pin Alpine package versions where feasible;
- record Debian package state or use a reproducible package snapshot;
- verify GTK 4.12 X11 backend and Cairo renderer;
- verify Xft, Xtst, Xorg fbdev, evdev, and DBus availability;
- fail builds early with named missing-dependency errors;
- add build manifests and source hashes.

**Acceptance:** rebuilding on a clean host produces known binary and rootfs
metadata.

## Step 7.4: Add rollback and flash safety

- require TWRP confirmation before `flash.sh`;
- require ADB state `recovery`;
- verify BOOT/SYSTEM backups;
- verify source SHA-256 before pushing;
- verify target SHA-256 after writing;
- write only mmcblk0p9 and mmcblk0p19;
- refuse RECOVERY, EFS, CPEFS, RADIO, and bootloader targets.
- keep rollback artifacts and rollback instructions versioned.

**Acceptance:** flash scripts cannot write a wrong partition or proceed without
device confirmation.

## Step 7.5: Secure operational defaults

- prefer key-only root SSH;
- remove password login from production rootfs;
- give password login an explicit experimental-only build flag;
- ensure Wi-Fi credentials are mode 600;
- validate apt/package operations with root confirmation;
- log system changes but never log passwords or credentials;
- audit scripts for accidental secrets or sensitive paths.

**Acceptance:** production builds contain no plaintext passwords or broad
password-authentication settings.

---

# Phase 8: UI and information architecture

Only begin this phase after Phases 1-6 are green.

## Step 8.1: Define the final navigation model

Create a concise UX specification:

- Home;
- Drawer;
- Recents;
- Quick settings;
- Settings;
- App window;
- Keyboard.

Specify how each surface enters, returns, closes, and restores focus.
Remove duplicate navigation affordances and ambiguous Back behavior.

**Acceptance:** every user action has a documented destination and no surface
requires unexplained swipes or hidden gestures.

## Step 8.2: Rework Home

- remove the empty black pinned tile;
- show only pinned apps that exist;
- add an explicit pinned/edit state;
- limit pinned apps to a useful count;
- make dock slots equal-width and stable;
- always show Terminal and Settings;
- show Files and Browser only when installed;
- show an install-needed hint instead of absent slots;
- improve clock, date, and status hierarchy.

**Acceptance:** Home is readable at one glance and no tile appears empty.

## Step 8.3: Rework Drawer

- use four equal-width columns;
- show a real icon or a consistent icon tile;
- make labels readable and two-line where needed;
- sort and group apps predictably;
- keep search stable while typing;
- add empty, loading, and no-result states;
- add optional alphabetical index or category filters.

**Acceptance:** drawer search and browsing work at 720px and remain usable with
more than 50 apps.

## Step 8.4: Rework Recents

- use app thumbnails or live previews if feasible;
- otherwise use large, consistent icon/name cards;
- put close and switch targets at least 64px apart;
- give Clear all a clear destructive-action style;
- show "No recent apps";
- sort most-recently-used first;
- support close, switch, and clear-all with accessible feedback.

**Acceptance:** users can identify, switch, and close every app from a single
screen.

## Step 8.5: Rework status and quick settings

- use readable 48px status bar text;
- show Wi-Fi, cellular-equivalent/offline state, battery percent, charging,
  and clock clearly;
- replace tiny ambiguous glyphs with consistent symbols plus labels/tooltips;
- make quick settings fixed three-column tiles;
- add brightness, Wi-Fi, Bluetooth if present, keyboard, and settings;
- add a clear offline and unavailable state.

**Acceptance:** status data is understandable at arm's length and never shows
fabricated values.

---

# Phase 9: visual design refresh

## Step 9.1: Establish a small design system

- define 4, 8, 12, 16, 20, and 24 px spacing scale;
- define 48, 56, 64, 72, and 96 px touch targets;
- define typography roles: display, title, body, label, monospaced;
- define border, radius, elevation, active, disabled, and error states;
- ensure minimum contrast for all text and controls;
- keep `theme.h`, GTK CSS, and screenshots synchronized.

**Acceptance:** repeated components use the same constants and no ad hoc colors
or dimensions remain.

## Step 9.2: Refresh the Paper theme

The current serif-heavy paper treatment is visually distinct but can be
hard to parse. Keep it if it fits the device, but improve it:

- increase status and app label legibility;
- simplify title bars;
- remove overly heavy borders where they do not communicate structure;
- improve button pressed/focus states;
- support user-selected themes cleanly;
- avoid CSS selectors that rely on GTK internals that vary by version.

**Acceptance:** a contrast and readability pass at actual device DPI passes
before and after theme changes.

## Step 9.3: Add deliberate alternative themes

Possible themes, to be implemented only after the Paper theme is stable:

- clean high-contrast;
- terminal/hacker;
- large text/accessibility;
- dark AMOLED-like;
- low-power e-ink/paper.

Themes should control only approved tokens, not duplicate CSS trees.

**Acceptance:** switching themes does not change geometry or break controls.

## Step 9.4: Improve app windows and dialogs

- remove unnecessary rounded/boxed chrome;
- define a maximum dialog width for the portrait display;
- use clear primary and destructive actions;
- use progress and busy states for package/Wi-Fi operations;
- make package log scroll, copy, and status actions usable;
- keep text entry visible above the keyboard.

**Acceptance:** core user tasks require no hidden scroll or tiny text.

---

# Phase 10: accessibility and input quality

## Step 10.1: Touch and gesture review

- verify every control is at least 64px in one dimension;
- prevent overlapping or nested gesture areas;
- support tap, hold, swipe, drag, and scroll consistently;
- disable accidental launch while dragging;
- add press feedback for all tactile controls;
- document gesture conflicts.

**Acceptance:** a touch-only walkthrough requires no keyboard/mouse for core
tasks.

## Step 10.2: Text and keyboard accessibility

- support larger text settings independently from app scaling;
- ensure xterm and system text remain readable;
- avoid hard-coded xterm/GTK font sizes;
- support sticky Ctrl and common terminal shortcuts;
- add keyboard audio/touch feedback only as configurable options;
- ensure Shift, Caps, and page state are visually unambiguous.

**Acceptance:** 25-character and 40-character terminal/output checks remain
readable at chosen text sizes.

## Step 10.3: Notifications and error feedback

- replace silent stderr-only failures with a recoverable UI status;
- group repeated service errors;
- provide retry and dismiss actions;
- never expose root passwords or Wi-Fi passwords in logs/UI;
- define how FieldM reports Wi-Fi, battery, package, and app launch failures.

**Acceptance:** known failures produce useful user feedback rather than
mystery freezes.

---

# Phase 11: system features and advanced UX

## Step 11.1: File manager

- install `pcmanfm` or another GTK touchscreen-appropriate file manager;
- hide the Files slot until the app is verified;
- verify launch, scroll, folder navigation, and app close;
- integrate content opening through `xdg-open`;
- test external storage and permission behavior.

## Step 11.2: Browser

- install a browser that works with ARMv7 and the chosen rootfs;
- consider NetSurf/GTK or a carefully selected lightweight browser;
- verify text entry, scrolling, zoom, and keyboard behavior;
- hide the Browser slot if unavailable;
- document memory limits and whether the browser is supported.

**Status:** NetSurf/GTK is installed on the live ARMv7 target and is now
recognized explicitly in FieldM browser launch/fallback logic. Browser
windows are classified as text contexts so the on-screen keyboard can be
started; launching no-URL NetSurf now supplies `about:blank` instead of
exiting with `BadURL`. Fixed startup adoption for already-mapped browser
windows so a restarted window manager cannot leave NetSurf unmanaged.
Fixed browser URL argument ownership to avoid freeing a string literal.
Added stale-client pruning and tracked app-child reaping. A live 30-second
Browser stability test passed with the shell PID unchanged and NetSurf
framed within the 720x1280 work area; direct open/close no longer leaves
NetSurf zombies. Text-entry, scrolling, zoom, and URL navigation remain to be
verified interactively.

## Step 11.3: Package manager polish

- prevent apt operations outside root workflows;
- add operation cancellation and confirmation;
- stream line output without UI thread stalls;
- add search debounce;
- show package metadata and installed state;
- preserve package logs;
- test update/search/install/remove under low memory.

## Step 11.4: Wi-Fi reliability

- track scan, connect, disconnect, timeout, and DHCP state;
- prevent concurrent commands;
- handle duplicate SSIDs and hidden networks;
- keep credentials out of UI logs;
- show signal and security state;
- refresh status after connection changes;
- retry safely after sleep/reconnect.

## Step 11.5: Battery and telemetry

- handle absent, unknown, and charging battery paths;
- calculate status without blocking UI;
- refresh asynchronously;
- cache values for status and quick settings;
- remove or label placeholder CPU/RAM values.

## Step 11.6: Notifications and recent tasks

After core stability:

- add a notification model;
- add app badges;
- support quick settings/volume notifications;
- add clipboard and file transfer indicators;
- keep notifications independent from Recents.

---

# Phase 12: maintainability and release

## Step 12.1: Documentation cleanup

- update README, FieldM README, AGENTS/operational handoff, and CURRENT to match
  actual target and commands;
- remove stale build claims and duplicate instructions;
- document all live-only conventions as host configuration, not source files;
- document rollback first and flashing second;
- provide a troubleshooting decision tree.

## Step 12.2: CI and release automation

Add a host CI job for:

- clean build;
- app and desktop entry checks;
- unit tests;
- shell and WM compile with warnings;
- script shell syntax checks;
- generated manifest checks;
- diff/whitespace checks.

Add a separate hardware job for live smoke tests.

## Step 12.3: Source cleanup

- remove unreferenced legacy popup/backends;
- remove commented-out old settings code;
- split oversized `wm.c`, `desktop_view.c`, and `settings.c`;
- add small modules for close protocol, geometry, Recents filtering, config,
  subprocess, and hardware state;
- run clang-format or repository-standard formatting;
- document public and internal interfaces.

## Step 12.4: Release criteria

A release candidate requires:

- clean bootstrap/build;
- one known-good backup;
- no crash in 30-minute soak;
- no zombie/process growth;
- no keyboard character loss;
- no blank Recents card;
- close one/Clear all pass;
- hardware restart pass;
- live app launch, Wi-Fi, terminal, htop, package, and settings pass;
- screenshots match current build;
- rollback test passes;
- TODO items in Phases 1-7 are complete.

---

# Suggested execution order

## Milestone A: recoverable

1. supervisor and boot loop;
2. hardware-key and emergency restart;
3. EVIOCGRAB blanking;
4. live deploy/rollback script;
5. fresh live log and PID diagnostic bundle.

## Milestone B: no longer stranded

1. dynamic WM clients;
2. close protocol and Recents;
3. keyboard event loop and page switching;
4. geometry/struts/focus;
5. app child reaping;
6. preference atomicity.

## Milestone C: repeatably correct

1. host unit fixtures;
2. Xephyr smoke and characterization;
3. live SSH smoke;
4. repeat/soak tests;
5. screenshot baselines.

## Milestone D: credible UI

1. navigation simplification;
2. Home and drawer repair;
3. status and Recents redesign;
4. themes and visual tokens;
5. browser/file manager validation.

## Milestone E: release candidate

1. build/release manifests;
2. flash safety;
3. documentation cleanup;
4. source cleanup;
5. final soak and rollback.
