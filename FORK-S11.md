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

## Builds

Every push to the `tab-s11` branch runs `.github/workflows/build-s11.yml`, which
patches the assets (cached between runs), builds the APK and publishes it as a
GitHub release named `s11-buildN`.

Updates install over the previous build only when every build is signed with the
same key. The key lives in the repository secrets `S11_KEYSTORE_B64`,
`S11_KEYSTORE_PASSWORD` and `S11_KEY_PASSWORD`; without them the build falls back
to a throwaway debug key.

## Following upstream

`main` mirrors brunodev85/winlator-app. To pick up a new upstream release, sync
`main` on GitHub, then merge `main` into `tab-s11`. Keep S11 changes small and
self-contained so these merges stay easy.
