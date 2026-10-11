# S11 Workspace

Winlator tuned for the Galaxy Tab S11 (Dimensity 9400+, Mali Immortalis-G925,
2560×1600). It installs next to the official Winlator, so existing containers
there stay untouched.

## How it differs from upstream

- Application id `com.winlatab` (official: `com.winlator`), app name "S11 Workspace".
- The id must stay exactly 12 characters: `tools/s11/patch_assets.py` rewrites the
  install path `/data/data/com.winlator` inside the bundled rootfs, Box64 and
  driver archives byte-for-byte at build time. The same path is changed in
  `AppUtils.java`, `gladio.h`, `vortek.h` and `winlator.h`.
- The FileProvider authority follows the application id.

## Desktop look

The Wine desktop is set up to look like Windows 10/11 rather than Windows 95:

- **Visual style "Winlator S11"** (`tools/s11/theme/build_msstyles.py`): Wine 11's
  Aero theme with our colours and name. Wine 10.10's uxtheme loads it. Shipped in the
  rootfs overlay and linked into each prefix; used with the light desktop theme.
  `ModernLook` marks it as loaded before, so uxtheme keeps the colours from
  `WineThemeManager` instead of the theme's own.
- **Colours** (`WineThemeManager`): light title bars that match the dialogs, Windows
  blue selection, 3D colours tuned so classic controls get flat 1 px borders.
- **Title bars**: Windows 10 sizes (`ModernLook`; no padded border, which Wine leaves
  unpainted as a black frame), thin Marlett glyphs
  (`tools/s11/theme/build_marlett.py`, installed as a newer Marlett in the overlay),
  and `CaptionButtons` turns off the raised push-button frame Wine draws around them
  (one verified call in the bundled `win32u.so`). It also fixes where clicks land: Wine's
  hit test took the close button to be as wide as the title bar is tall, so with wide
  buttons a click on × could maximize and one on □ minimize. The shell shows a gray (red
  for close) highlight under the mouse, as Windows does.
- **Fonts**: "Segoe UI" is served by S11 Sans, our build of Microsoft's Selawik
  (`tools/s11/fonts/build_s11sans.py`, SIL OFL; renamed as the OFL requires), and
  dialogs follow it. NanumGothic is linked behind the common Latin fonts for Hangul.
- **Resolution**: new containers use 1920×1200 at 144 dpi; containers still on the old
  1280×800/96 dpi defaults move there once.
- **Wallpaper**: an original S11 wallpaper (`tools/s11/theme/make_wallpaper.py`),
  rendered at the screen's size instead of 480 px tall.
- **Product identity**: the Android app is named "S11 Workspace"; the S11 mark now uses
  a brighter Windows 11-style blue gradient. The upstream Winlator origin and its licence
  notices remain documented and intact.

## Taskbar and start menu

`tools/s11/shell/s11shell.c` is a small Win32 program (built with clang against Wine's
headers, `build.sh`; the exe ships in the rootfs overlay) that replaces explorer.exe's
taskbar on the desktop session: centred program buttons with icons and tooltips, the
notification area (it answers Wine's `Shell_NotifyIcon` messages as `Shell_TrayWnd`), a
clock, input-language indicator and quick links for hidden background icons, Wi-Fi and
Bluetooth, and a start menu with every installed program, search (Hangul initial consonants
too) and Shut down / Restart, which go to the app over winhandler's UDP port. The Windows
key opens it; Win+D shows the desktop, Win+E the file manager, Win+I the settings.
Maximized windows are kept above it, and it steps aside for fullscreen programs. The icons
sit in the middle or on the left (Settings, Personalization).

The desktop session is named "s11" instead of "shell": Winlator's Wine turns explorer's
taskbar on for "shell" whatever EnableShell says, and it came up over ours. `S11Shell` sets
the registry for both names and has winhandler.exe start the shell; the app's Settings has
a switch to go back to Wine's taskbar.

The X server keeps the shell's windows (WM_CLASS `s11shell.exe`) above the others: Wine
only restacks a window in X when it becomes the very top one, so windows that opened later
covered the taskbar. When a window is resized the X server now keeps its contents until the
program repaints (it showed black meanwhile, flickering while resizing).

The clock opens a calendar. Alt+Tab switches between open windows; Shift+Alt+Tab reverses
the order. Ctrl+Esc opens Start and Ctrl+Shift+Esc the Task Manager; on
Samsung devices the Windows key itself reaches the container (`MetaKeyCapture`, One UI
keeps it otherwise). The power tab at the top of the screen is gone on the S11 desktop (the
start menu has shut down and restart); Ctrl+Alt+Del still opens the power menu. When the S11
shell is enabled, the redundant Android power tab is hidden even when a program is launched
from a shortcut. Session restart/exit transitions are guarded against duplicate callbacks.
The Command Prompt starts in `C:\`.
Wi-Fi and Bluetooth open Wine's network and Bluetooth control-panel entry points in the
container rather than leaving the desktop for Android Settings; actual radio control remains
subject to Wine's device passthrough support. The language indicator opens the Wine locale
control panel. The overflow menu exposes notification icons marked hidden by their owning
Windows applications.

Wine gets the device's time zone as a POSIX TZ string (`TimeZones`): the rootfs has no
zone database, so it ran on UTC. The Windows keys now
reach Wine: Meta maps to Super_L/R, and the X server's keyboard mapping reply was fixed to
cover keycodes above 131.

## Settings app

`tools/s11/shell/s11settings.c` (`s11settings.exe`, next to the shell in the overlay) is a
Windows 11 style Settings window, opened from the start menu (gear, or "설정" in the list),
Win+I, or the taskbar's menu:

- **System**: display scale (LogPixels), resolution, the Windows version programs see,
  mouse acceleration, and a button to open Wine Configuration (`winecfg.exe`).
- **Personalization**: background (the two S11 pictures, your own picture, or a new one
  from a file; applied at once), light, dark or Android system-following mode, taskbar alignment.
- **Apps**: installed programs from the Uninstall keys, with their icons; uninstall, or
  start an installer.
- **Fonts**: installed scalable fonts with a preview; install .ttf/.otf/.ttc files.
- **About**: S11 Workspace version, container, Wine, Windows, processor, memory, display.

What Wine keeps in the registry it writes directly. Resolution, mode and background belong
to the container in Winlator, so it sends them to winhandler's UDP port (codes 102-104,
`S11Settings`), the app stores them in the container, and they apply when it restarts; a
banner offers to restart right away. The S11 pictures are packed into the overlay from the
app's assets by `build_overlay.py`.

The display size is fixed when the X server starts (`XServer.screenInfo` is immutable), so a
resolution change still requires restarting the container; changing it live needs coordinated
X-server, renderer, Wine desktop and window-manager resize support.

Licences: the visual style and Marlett come from Wine (LGPL 2.1); S11 Sans keeps
Selawik's OFL (`S11Sans-OFL.txt` next to the fonts).

## Builds

Every push to the `tab-s11` branch runs `.github/workflows/build-s11.yml`, which
patches the assets (cached between runs), builds the APK and publishes it as a
GitHub release named `s11-buildN`.

Updates install over the previous build only when every build is signed with the
same key. The key is `tools/s11/signing/s11-release.p12` (PKCS12, encrypted with
AES-256); its password is the repository secret `S11_KEYSTORE_PASSWORD`. Without
the secret the workflow still compiles, but publishes nothing.

## Following upstream

`main` mirrors brunodev85/winlator-app. To pick up a new upstream release, sync
`main` on GitHub, then merge `main` into `tab-s11`. Keep S11 changes small and
self-contained so these merges stay easy.
