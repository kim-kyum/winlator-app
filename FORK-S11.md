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
taskbar on the desktop session: centred program buttons with the S11 app icons and tooltips, the
notification area (it answers Wine's `Shell_NotifyIcon` messages as `Shell_TrayWnd`), a
clock, input-language indicator and quick links for hidden background icons, Wi-Fi and
Bluetooth, and a start menu with every installed program, search (Hangul initial consonants
too) and Shut down / Restart, which go to the app over winhandler's UDP port. The Windows
key opens it; Win+D shows the desktop, Win+E the file manager, Win+I the settings.
Maximized windows are kept above it, and it steps aside for fullscreen programs. The icons
sit in the middle or on the left (Settings, Personalization).

The Start menu is capped at 520×520 dp and uses five columns so the icon cards stay compact.
`Win+Shift+S` requests an Android `PixelCopy` of the guest desktop; the PNG is stored at
`C:\S11\Screenshots` and opened from the S11 Gallery app. The gallery provides a
thumbnail grid, refresh, selection and full-size preview.

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
The taskbar Wi-Fi and Bluetooth buttons open the Settings app's Connectivity page (see
"Wi-Fi and Bluetooth" below). The language indicator opens the Wine locale
control panel. The overflow menu exposes notification icons marked hidden by their owning
Windows applications.

`Win+Shift+S` is detected both by the S11 shell hook and the Android Activity key dispatcher.
It opens a translucent drag-to-select overlay; PixelCopy copies only the selected rectangle
from the guest surface and saves it to the active rootfs at
`home/xuser/.wine/drive_c/S11/Screenshots` (guest path `C:\S11\Screenshots`). The leading
slash in `RootFS.WINEPREFIX` must be removed before joining it to `rootFS.getRootDir()`.

Wine gets the device's time zone as a POSIX TZ string (`TimeZones`): the rootfs has no
zone database, so it ran on UTC. The Windows keys now
reach Wine: Meta maps to Super_L/R, and the X server's keyboard mapping reply was fixed to
cover keycodes above 131.

## Settings app

`tools/s11/shell/s11settings.c` (`s11settings.exe`, next to the shell in the overlay) is a
Windows 11 style Settings window, opened from the start menu (gear, or "설정" in the list),
Win+I, or the taskbar's menu:

- **System**: display scale (LogPixels) and resolution.
- **Graphics**: the Vulkan and OpenGL drivers with each driver's options (Vortek: Vulkan
  version, device memory, image cache, resource memory, exposed extensions), Direct3D through
  DXVK or WineD3D with their options, DirectX 12 (VKD3D), and the HUD.
- **Sound**: audio driver, performance mode, volume, latency.
- **Wine**: the Windows version programs see, mouse acceleration, guest pointer speed (1–20)
  and DirectInput mouse warp. The speed control does not alter a physical mouse sensor's DPI.
- **Compatibility**: Box64 preset, Box64's translation cache (on; it can be turned off or
  cleared), the services that start, and the processor cores for 64-bit and 32-bit programs.
  The cache is Box64's DynaCache, which upstream Winlator turns off: translated code kept in the
  container's `~/.cache/box64` (1 GB at most) makes programs start much faster the second time
  (S11 lab, Box64 0.4.4: `cmd /c exit` 1.34 s to 0.24 s, Notepad's window 0.82 s to 0.61 s).
- **Windows components**: built-in (Wine) or native (Windows) for each component.
- **Drives**: the drive letters and the Android folders behind them; add (pick a folder) or remove.
- **Environment variables**: add, edit and remove the container's variables.
- **Connectivity**: Wi-Fi and Bluetooth on and off, nearby Wi-Fi networks (join with a
  password, disconnect), and the paired Bluetooth devices with their state (below).
- **Personalization**: background (the two S11 pictures, your own picture, or a new one
  from a file; applied at once), light, dark or Android system-following mode, taskbar
  alignment, and the system font of title bars and menus.
- **Apps**: installed programs from the Uninstall keys, with their icons; uninstall, or
  start an installer.
- **Fonts**: installed scalable fonts with a preview; install .ttf/.otf/.ttc files.
- **Troubleshoot**: why programs did not start or quit, and the games' Unity logs (below).
- **About**: S11 Workspace version, container, Wine, Windows, processor, memory, display, and
  the container's name.

The File Explorer keeps its shell navigation but now applies the S11 palette and Segoe-style
typography to the embedded file list, uses Explorer-themed headers, double buffering, and
full-row selection.

## Wi-Fi and Bluetooth

`S11Connectivity` gives the Settings app's Connectivity page the device's Wi-Fi and Bluetooth.
The app targets API 28, so Android still lets it use the calls newer targets lost:
`WifiManager.setWifiEnabled`, `addNetwork`/`enableNetwork` to join a network,
`getConfiguredNetworks`, and `BluetoothAdapter.enable`/`disable` (permissions: `CHANGE_WIFI_STATE`,
`BLUETOOTH`, `BLUETOOTH_ADMIN`, and coarse/fine location).

- It writes the page's rows to `~/.config/s11-connectivity.txt` (header `S11CONNECTIVITY 1
  <revision>`, rows like `s11-container.txt`'s with the types `section`, `toggle`, `action`,
  `network` and `info`) whenever one of Android's Wi-Fi, location or Bluetooth broadcasts
  changes it; the Settings app reads it again when the revision changes.
- Commands come back over winhandler code 109 like the container settings, with keys
  starting with `net.`: `net.wifi`/`net.bluetooth` (1/0), `net.scan`, `net.refresh` (sent when
  the page opens; scans at most every 20 s, Android allows 4 scans in 2 minutes),
  `net.connect <SSID>` with the password as value (empty for open or saved networks),
  `net.disconnect`, `net.permission`, `net.location`, `net.appsettings`, `net.open.wifi`,
  `net.open.bluetooth`.
- Nearby networks need the location permission and location services, as for any app; the
  page asks for them (`net.permission` shows Android's prompt; once refused for good it opens
  the app's settings). Work and school (802.1X) networks and pairing new Bluetooth devices
  stay in Android's settings, which the page opens.
- A failed join shows on the page: a wrong password from the supplicant's authentication
  error, otherwise after 25 s.
- The password field never keeps what was typed: it is cleared after sending and the buffers
  are zeroed; a reload of the page while typing keeps the text.

## Troubleshooting

A program Wine cannot start shows nothing at all: a missing DLL ends it before it has a window,
and Wine says why only in its output, which Winlator threw away. `S11Problems` now reads Wine's
output (`ProcessHelper.exec` passes every line to it, debug log or not) and Unity's logs:

- `WINEDEBUG` keeps the module channel's errors when the user has no debug log on
  (`-all,err+module`): "Library X (which is needed by Y) not found" and "Importing dlls for Z
  failed" become "Z: couldn't start, X is missing" (the first DLL not found is the cause).
  DLLs a running program fails to load (plugins) are listed without a notice.
- Wine prints unhandled exceptions whatever `WINEDEBUG` says; winedbg's report that follows names
  the program (the "(D)" line of its thread list) and the faulting module.
- Unity catches its own crashes, so Wine never hears of them: every `Player.log` under
  `AppData\LocalLow\company\product` is summarised (a crash with its exception, Steam's
  `RestartAppIfNecessary` quit, a missing plugin, a graphics or script error, or the last line).
- Each problem's report goes to `C:\S11\Logs` (UTF-8 with a BOM), with `wine-errors.txt`, the
  last error lines. `~/.config/s11-problems.txt` holds the Settings page's rows (`log` rows have
  Open, in Notepad, and Share, which sends `problems.share` and Android's share sheet gets a copy
  through the FileProvider); `~/.config/s11-notice.txt` holds the latest notice, which the
  taskbar shows once as a card bottom right (clicking it opens the page).
- Wine's Notepad started with the "System" bitmap font at 12 pixels: no Hangul and tiny.
  `KoreanFonts` gives it the UI font at 11 points with wrapping, unless the user picked a font.

## Korean in Wine's shell

Wine's Korean resources leave some of its file views English. shell32 builds the Type column
itself ("Folder", "File", "<ext> file" are fixed strings) and takes the type names from the
registry, where wine.inf put English ones ("Application", "Text Document"); Wine 10.10's
Korean translation lacks "Ne&w" and "Run as &Administrator"; verbs without a description show
their key name; Wine's Korean locale data writes "AM"/"PM" and "2026. 10. 27.".

- `KoreanShell` writes Korean names for Wine's file types and verbs into the registry before
  Wine starts (English ones again when the app or the container's `LC_ALL` is not Korean), and,
  once Wine has set the user locale to ko-KR, Windows' date notation (`2026-10-27 오전 3:07`).
  This reaches every program's file dialogs. Only display names change, never a command.
- `tools/s11/shell/s11shellko.h` fixes the rest where the S11 programs show it: a
  `WH_CALLWNDPROCRET` hook on the UI thread rewrites the shell view's Type and date texts
  after Wine fills them in (finding the columns by property key), menu items (`WM_INITMENUPOPUP`)
  and the read-only fields of the properties pages (`WM_INITDIALOG`). The File Explorer and the
  Settings app (its file dialogs) install it; the explorer also uses Windows' column titles and
  alignment and names drives as its navigation pane does ("로컬 디스크 (C:)").
- `WineRegistryEditor` writes characters beyond ASCII as `\x` escapes, exactly as Wine does,
  and reads them back. The native helper counted every such character as one extra byte, but
  Hangul is three in UTF-8, so an edit after Korean text in the same file landed at the wrong
  offset and corrupted it (`mbclen`, fixed as well).

## App icons

The built-in programs share one icon style: a white symbol on the blue tile of the S11 mark
(Settings, File Explorer, Gallery, Command Prompt, Notepad, WordPad, Task Manager, Registry
Editor, Wine settings, the installers, GPU info, Direct3D test, WineMine and 7-Zip).
`tools/s11/branding/make_app_icons.py` draws them from a few shapes: the .ico files of the S11
programs, a preview sheet, and `tools/s11/shell/s11tiles.h`, the same shapes as numbers, which
the shell draws with GDI+ on the taskbar and in the start menu (`draw_s11_tile`). Other
programs keep their own icon on a soft rounded surface.

What Wine keeps in the registry it writes directly. Resolution, mode and background belong
to the container in Winlator, so it sends them to winhandler's UDP port (codes 102-104,
`S11Settings`), the app stores them in the container, and they apply when it restarts; a
banner offers to restart right away. The S11 pictures are packed into the overlay from the
app's assets by `build_overlay.py`.

Every other container setting has a row of its own as well (graphics, sound, compatibility,
components, drives, variables, name). The Settings app cannot read the container data, so
Winlator writes the settings with their current values, choices and labels to
`~/.config/s11-container.txt` before Wine starts and after every change
(`S11ContainerSettings`); a change goes back as `key TAB value` (code 109) and the app reads
the file again, so rows that depend on a choice (the options of the chosen driver or wrapper)
follow it. They apply when the container starts again, as in the container list.

The Android editor of the whole container (code 108, `S11ContainerSettingsActivity`) is still
there but no longer opened from the Settings app. What it changes in the registry while the
container runs (system font, dpi, mouse warp, Windows version) waits in the container data
(`PendingRegistry`) and is written before Wine starts the next time, because wineserver
writes its copy of the registry over `user.reg` and `system.reg` while it runs.

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
