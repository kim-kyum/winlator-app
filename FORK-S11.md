# Winlator S11

Winlator tuned for the Galaxy Tab S11 (Dimensity 9400+, Mali Immortalis-G925,
2560×1600). It installs next to the official Winlator, so existing containers
there stay untouched.

## How it differs from upstream

- Application id `com.winlatab` (official: `com.winlator`), app name "Winlator S11".
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
- **Title bars**: Windows 10 sizes (`ModernLook`), thin Marlett glyphs
  (`tools/s11/theme/build_marlett.py`, installed as a newer Marlett in the overlay),
  and `CaptionButtons` turns off the raised push-button frame Wine draws around them
  (one verified call in the bundled `win32u.so`).
- **Fonts**: "Segoe UI" is served by S11 Sans, our build of Microsoft's Selawik
  (`tools/s11/fonts/build_s11sans.py`, SIL OFL; renamed as the OFL requires), and
  dialogs follow it. NanumGothic is linked behind the common Latin fonts for Hangul.
- **Resolution**: new containers use 1920×1200 at 144 dpi; containers still on the old
  1280×800/96 dpi defaults move there once.
- **Wallpaper**: an original S11 wallpaper (`tools/s11/theme/make_wallpaper.py`),
  rendered at the screen's size instead of 480 px tall.

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
