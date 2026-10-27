#!/bin/bash
# The ARM64 Wine of S11 Workspace's fast containers: Hangover's Wine (ARM64EC; FEX runs x64
# programs, Box64 32-bit ones) laid out as one of Winlator's installed Wine versions, for the
# app's rootfs: its programs ask for the rootfs's loader, and the libraries Wine opens come from
# the rootfs (LD_LIBRARY_PATH), as for the app's own programs.
#
# Then it is tried the way the app runs it: in the rootfs from the repository's assets, a new
# prefix, a console program and a window for ARM64, x64 and x86.
# Usage: package.sh <repo> <out dir>      (on an ARM64 Linux machine)
set -eu
REPO="$(cd "$1" && pwd)"
OUT="$2"
VERSION=11.16
HANGOVER="https://github.com/AndreRH/hangover/releases/download/hangover-$VERSION/hangover_${VERSION}_ubuntu2404_noble_arm64.tar"
NAME="wine-$VERSION-arm64"
APP_ROOTFS=/data/data/com.winlatab/files/rootfs          # the app's (applicationId com.winlatab)
ASSET_ROOTFS=/data/data/com.winlator/files/rootfs        # the paths in the repository's rootfs
WORK="${RUNNER_TEMP:-$HOME}/wine-arm64"
mkdir -p "$OUT" "$WORK"
OUT="$(cd "$OUT" && pwd)"
LOG="$OUT/package.log"
log() { echo "[wine-arm64] $*" | tee -a "$LOG"; }

sudo apt-get update -qq > /dev/null
for p in zstd patchelf file llvm xvfb x11-utils 7zip jq gcc; do
    sudo apt-get install -y -qq "$p" > /dev/null 2>&1 || log "package missing: $p"
done
LLVM_STRIP=$(ls /usr/bin/llvm-strip* | head -1)

# --- Hangover's packages ------------------------------------------------------------------------
cd "$WORK"
[ -s hangover.tar ] || curl -sSL --retry 3 -o hangover.tar "$HANGOVER"
rm -rf debs stage && mkdir debs stage
tar -xf hangover.tar -C debs
ls -la debs | tee -a "$LOG"
for d in debs/hangover-wine_*.deb debs/hangover-libarm64ecfex_*.deb debs/hangover-libwow64fex_*.deb debs/hangover-wowbox64_*.deb; do
    dpkg-deb -x "$d" stage
done

# --- the layout of an installed Wine: bin, lib/wine, share/wine --------------------------------
DEST="$WORK/$NAME"
rm -rf "$DEST" && mkdir -p "$DEST/share/doc"
cp -a stage/usr/bin stage/usr/lib "$DEST/"
cp -a stage/usr/share/wine "$DEST/share/"
for doc in stage/usr/share/doc/*; do
    mkdir -p "$DEST/share/doc/$(basename "$doc")"
    cp -a "$doc"/copyright* "$doc"/LICENSE* "$DEST/share/doc/$(basename "$doc")/" 2>/dev/null || true
done
# tools for building Windows programs are not needed to run them
( cd "$DEST/bin" && rm -f function_grep.pl widl winebuild winecpp winedump wineg++ winegcc winemaker wmc wrc )
rm -rf "$DEST/lib/wine/"*/*.a "$DEST/lib/wine/"*/*.def 2>/dev/null || true
log "unpacked: $(du -sh "$DEST" | cut -f1)"

# --- smaller: symbols out of the Unix and the Windows files -------------------------------------
find "$DEST" -type f \( -name "*.so" -o -name "*.so.*" \) -exec strip --strip-unneeded {} + 2>/dev/null || true
find "$DEST/bin" "$DEST/lib/wine/aarch64-unix" -maxdepth 1 -type f ! -name "*.so" | while read -r f; do
    file "$f" | grep -q ELF && strip --strip-unneeded "$f" 2>/dev/null || true
done
find "$DEST/lib/wine" -type f \( -name "*.dll" -o -name "*.exe" -o -name "*.drv" -o -name "*.sys" -o -name "*.ocx" \
    -o -name "*.cpl" -o -name "*.acm" -o -name "*.ax" -o -name "*.tlb" -o -name "*.ds" -o -name "*.msstyles" \) \
    -exec "$LLVM_STRIP" --strip-debug {} + 2>/dev/null || true
log "stripped: $(du -sh "$DEST" | cut -f1)"

# --- the rootfs's loader for every program that has one (not wine-preloader: it is static) ------
INTERP="$APP_ROOTFS/lib/ld-linux-aarch64.so.1"
while IFS= read -r f; do
    if file "$f" | grep -q "ELF.*aarch64.*interpreter"; then
        patchelf --set-interpreter "$INTERP" "$f"
        log "interpreter: ${f#$DEST/}"
    fi
done < <(find "$DEST/bin" "$DEST/lib/wine/aarch64-unix" -maxdepth 1 -type f)

# --- /tmp: Android has none; Wine's Linux build keeps its server socket there -------------------
# (Winlator's own Wine has the rootfs's path compiled in.) libs11tmp.so, preloaded by the app,
# sends Wine's /tmp paths to $TMPDIR, the rootfs's tmp.
gcc -O2 -Wall -shared -fPIC -o "$DEST/lib/libs11tmp.so" "$REPO/tools/s11/wine-arm64/s11tmp.c" -ldl
strip --strip-unneeded "$DEST/lib/libs11tmp.so"
log "libs11tmp.so: $(stat -c %s "$DEST/lib/libs11tmp.so") bytes"
nm -D --undefined-only "$DEST/lib/wine/aarch64-unix/ntdll.so" "$DEST/bin/wineserver" 2>/dev/null \
    | grep -E " (mkdir|chdir|l?stat(64)?|fstatat(64)?|open(64|at)?|unlink|rmdir|access|opendir)(@|$)" | sort -u >> "$LOG"

# --- the package ----------------------------------------------------------------------------------
tar -C "$WORK" -cf - "$NAME" | zstd -19 -T0 -q -o "$OUT/$NAME.tzst"
( cd "$OUT" && sha256sum "$NAME.tzst" > "$NAME.tzst.sha256" )
echo "$VERSION" > "$OUT/version.txt"
log "package: $(ls -la "$OUT/$NAME.tzst" | awk '{print $5}') bytes"

# --- try it as the app runs it (failures here are reported, the package stays) -------------------
set +e
TEST="$WORK/test-rootfs"
rm -rf "$TEST" && mkdir -p "$TEST"
zstd -dc "$REPO/app/src/main/assets/rootfs.tzst" | tar -x -C "$TEST"
mkdir -p "$TEST/tmp" "$TEST/home/xuser" "$TEST/opt/installed-wine"
chmod 1777 "$TEST/tmp"
sudo mkdir -p /data/data/com.winlatab/files /data/data/com.winlator/files
sudo ln -sfn "$TEST" "$APP_ROOTFS"
sudo ln -sfn "$TEST" "$ASSET_ROOTFS"
zstd -dc "$OUT/$NAME.tzst" | tar -x -C "$TEST/opt/installed-wine"
W="$APP_ROOTFS/opt/installed-wine/$NAME"
Xvfb :5 -screen 0 1600x1000x24 -nolisten tcp > "$WORK/xvfb.log" 2>&1 &
sleep 3
# the app's X server listens at <rootfs>/tmp/.X11-unix/X0; here Xvfb is :5, its socket mounted
# there (below), since the tests hide /tmp as the device has none
rm -rf "$TEST/tmp/.X11-unix" && mkdir -p "$TEST/tmp/.X11-unix"
ENVS="HOME=$APP_ROOTFS/home/xuser USER=xuser TMPDIR=$APP_ROOTFS/tmp DISPLAY=:5 WINEPREFIX=$APP_ROOTFS/home/xuser/.wine \
LD_LIBRARY_PATH=$APP_ROOTFS/usr/lib PATH=$W/bin:$APP_ROOTFS/usr/bin:/usr/bin:/bin WINEDEBUG=-all \
WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d"
PRELOAD="LD_PRELOAD=$W/lib/libs11tmp.so"
ME=$(id -un)
# a command without /tmp: an empty read-only one over it, X's socket in the rootfs
notmp() {
    sudo unshare -m -- bash -c 'mount --bind /tmp/.X11-unix "$1" && mount -t tmpfs -o ro,size=1m tmpfs /tmp && shift && exec sudo -u "$1" -- "${@:2}"' \
        _ "$TEST/tmp/.X11-unix" "$ME" "$@"
}
# without the shim Wine cannot make its server directory (as on the device)
notmp env -i $ENVS timeout 60 "$W/bin/wine" cmd /c exit > "$OUT/test-no-shim.log" 2>&1
log "test without /tmp and without libs11tmp.so: exit $? ($(grep -m1 -oE "wine(server)?: .*" "$OUT/test-no-shim.log" | head -c 160))"
ENVS="$ENVS $PRELOAD"
t0=$(date +%s)
notmp env -i $ENVS timeout 600 "$W/bin/wine" wineboot -i > "$OUT/test-wineboot.log" 2>&1 && st=0 || st=$?
notmp env -i $ENVS timeout 60 "$W/bin/wineserver" -w || true
log "server directory: $(ls -d "$TEST"/tmp/.wine-* 2>/dev/null)"
log "test: new prefix, exit $st after $(( $(date +%s) - t0 )) s; system32 $(ls "$TEST/home/xuser/.wine/drive_c/windows/system32" 2>/dev/null | wc -l) files"
notmp env -i $ENVS timeout 120 "$W/bin/wine" cmd /c "echo arm64 ok" > "$OUT/test-cmd.log" 2>&1 && log "test: cmd: $(tr -d '\r' < "$OUT/test-cmd.log" | tail -1)" || log "test: cmd failed"
# a window of each kind: 7-Zip's File Manager, from its installers
REL=$(curl -sSL -H "Authorization: Bearer ${GH_TOKEN:-}" https://api.github.com/repos/ip7z/7zip/releases/latest)
mkdir -p "$TEST/home/xuser/.wine/drive_c/test"
for a in arm64 x64 x86; do
    if [ $a = x86 ]; then pat='^7z[0-9]+[.]exe$'; else pat="^7z[0-9]+-$a[.]exe$"; fi
    url=$(echo "$REL" | jq -r --arg p "$pat" '.assets[] | select(.name | test($p)) | .browser_download_url' | head -1)
    curl -sSL -o "$WORK/7z-$a.exe" "$url" && 7z x -y -o"$TEST/home/xuser/.wine/drive_c/test/7z-$a" "$WORK/7z-$a.exe" > /dev/null
    ( notmp env -i $ENVS "$W/bin/wine" "C:\\test\\7z-$a\\7zFM.exe" > "$OUT/test-7zfm-$a.log" 2>&1 & )
    found=no
    for i in $(seq 1 150); do xwininfo -display :5 -root -tree 2>/dev/null | grep -qi "7-zip" && { found=yes; break; }; sleep 0.2; done
    log "test: 7-Zip File Manager ($a) window: $found"
    notmp env -i $ENVS "$W/bin/wineserver" -k > /dev/null 2>&1 || true
    sleep 1
done

cat > "$OUT/notes.md" <<EOF
ARM64 Wine $VERSION for S11 Workspace's fast containers: [Hangover](https://github.com/AndreRH/hangover) $VERSION
(Wine with ARM64EC; FEX runs x64 programs and Box64 32-bit ones), laid out for the app's rootfs.

The app downloads it from Settings; you do not need to install it by hand.

Licences: Wine (LGPL 2.1), FEX (MIT), Box64 (MIT); the notices are in share/doc.
EOF
log "done"
