#!/bin/bash
# S11 lab: runs a Unity (Mono) Windows program with Winlator's Wine under Box64 on an ARM64
# GitHub runner, with the Box64 settings S11 Workspace uses, and records how far it gets.
#
# The CPU side is what the tablet does (ARMv9, weak memory ordering, Box64 0.4.4 dynarec,
# the same Wine build and prefix); graphics are Mesa's software renderer instead of Vortek.
#
# Usage: unity_lab.sh <repo> <out dir>      (needs sudo for packages)
set -u
REPO="$(cd "$1" && pwd)"
OUT="$2"
LAB="${LAB:-$HOME/lab}"
RUN_SECONDS="${RUN_SECONDS:-200}"
mkdir -p "$LAB" "$OUT"
SUMMARY="$OUT/summary.txt"

log() { echo "[lab] $*" | tee -a "$SUMMARY"; }

log "start $(date -u)"
uname -a >> "$SUMMARY"
grep -m1 -i "features" /proc/cpuinfo >> "$SUMMARY"
nproc >> "$SUMMARY"

# --- packages --------------------------------------------------------------------------
sudo apt-get update -qq > /dev/null
for p in xvfb zstd cmake build-essential python3 git imagemagick x11-utils unzip \
         libgl1-mesa-dri libglx-mesa0 libgl1 libegl1 mesa-vulkan-drivers libvulkan1 \
         libfreetype6 libfontconfig1 libgnutls30t64 libpulse0 libasound2t64 libxrandr2 \
         libxinerama1 libxcursor1 libxi6 libxcomposite1 libxext6 libxrender1 libxfixes3 \
         libxxf86vm1 libxkbcommon0 libsdl2-2.0-0 libdbus-1-3 libunwind8 libcups2t64; do
    sudo apt-get install -y -qq "$p" > /dev/null 2>&1 || log "package missing: $p"
done

# --- Box64 0.4.4, the version S11 Workspace ships ---------------------------------------
if [ ! -x "$LAB/box64-build/box64" ]; then
    git clone -q --depth 1 -b v0.4.4 https://github.com/ptitSeb/box64.git "$LAB/box64-src"
    cmake -S "$LAB/box64-src" -B "$LAB/box64-build" -DARM_DYNAREC=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        > "$OUT/box64-cmake.log" 2>&1
    make -C "$LAB/box64-build" -j"$(nproc)" > "$OUT/box64-build.log" 2>&1 || { log "box64 build failed"; tail -50 "$OUT/box64-build.log" >> "$SUMMARY"; exit 1; }
fi
BOX64="$LAB/box64-build/box64"
log "box64: $("$BOX64" --version 2>&1 | head -1)"

# --- Winlator's Wine and container prefix ------------------------------------------------
# Winlator's Wine keeps its paths under /data/data/com.winlator/files/rootfs (wineserver's
# socket directory, for one), so the rootfs goes there, as on the device.
ROOTFS=/data/data/com.winlator/files/rootfs
if [ ! -x "$LAB/rootfs/opt/wine/bin/wine" ]; then
    mkdir -p "$LAB/rootfs"
    zstd -dc "$REPO/app/src/main/assets/rootfs.tzst" | tar -x -C "$LAB/rootfs"
fi
sudo mkdir -p /data/data/com.winlator/files
sudo ln -sfn "$LAB/rootfs" "$ROOTFS"
mkdir -p "$LAB/rootfs/tmp" "$LAB/rootfs/home/xuser"
chmod 1777 "$LAB/rootfs/tmp"
WINE="$ROOTFS/opt/wine/bin/wine"
WINESERVER="$ROOTFS/opt/wine/bin/wineserver"
export HOME="$ROOTFS/home/xuser" USER=xuser TMPDIR="$ROOTFS/tmp"
export WINEPREFIX="$HOME/.wine"
if [ ! -d "$WINEPREFIX" ]; then
    zstd -dc "$REPO/app/src/main/assets/container_pattern.tzst" | tar -x -C "$HOME"
fi
ls -la "$WINEPREFIX/dosdevices" >> "$SUMMARY"
export BOX64_LD_LIBRARY_PATH="$LAB/rootfs/usr/lib/x86_64-linux-gnu:$LAB/box64-src/x64lib"
export WINEDEBUG="${WINEDEBUG:--all}"
# no wine-mono / wine-gecko install prompts (they blocked the first run), no menu entries
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
export MESA_GL_VERSION_OVERRIDE=4.5 MESA_GLSL_VERSION_OVERRIDE=450

# --- X ----------------------------------------------------------------------------------
Xvfb :1 -screen 0 1920x1080x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
export DISPLAY=:1
sleep 3

# --- the Box64 environment of S11 Workspace (GuestProgramLauncherComponent, presets) -----
cp "$REPO/app/src/main/assets/box64/default.box64rc" "$LAB/config.box64rc"
base_env() {
    echo BOX64_NOBANNER=1 BOX64_DYNAREC=1 BOX64_DYNACACHE=0 BOX64_RCFILE="$LAB/config.box64rc"
}
preset_env() {   # Box64PresetManager
    case "$1" in
        PERFORMANCE) echo BOX64_DYNAREC_SAFEFLAGS=1 BOX64_DYNAREC_FASTNAN=1 BOX64_DYNAREC_FASTROUND=1 BOX64_DYNAREC_X87DOUBLE=0 \
                          BOX64_DYNAREC_BIGBLOCK=3 BOX64_DYNAREC_STRONGMEM=0 BOX64_DYNAREC_FORWARD=512 BOX64_DYNAREC_CALLRET=1 \
                          BOX64_DYNAREC_WAIT=1 BOX64_DYNAREC_NATIVEFLAGS=1 BOX64_DYNAREC_WEAKBARRIER=2 ;;
        STABILITY) echo BOX64_DYNAREC_SAFEFLAGS=2 BOX64_DYNAREC_FASTNAN=0 BOX64_DYNAREC_FASTROUND=0 BOX64_DYNAREC_X87DOUBLE=1 \
                        BOX64_DYNAREC_BIGBLOCK=0 BOX64_DYNAREC_STRONGMEM=2 BOX64_DYNAREC_FORWARD=128 BOX64_DYNAREC_CALLRET=0 \
                        BOX64_DYNAREC_WAIT=0 BOX64_DYNAREC_NATIVEFLAGS=0 BOX64_DYNAREC_WEAKBARRIER=0 ;;
    esac
}

# --- prefix update, once, outside the timed runs -------------------------------------------
log "wine --version: $(env $(base_env) timeout 120 "$BOX64" "$WINE" --version 2>&1 | tail -1)"
wineboot_prefix() {
    local start=$(date +%s)
    env $(base_env) $(preset_env STABILITY) BOX64_UNITYPLAYER=0 BOX64_LOG=1 WINEDEBUG=err+all,+module,+loaddll \
        timeout 900 "$BOX64" "$WINE" wineboot -u > "$OUT/wineboot-full.log" 2>&1
    env $(base_env) timeout 120 "$BOX64" "$WINESERVER" -w
    { head -250 "$OUT/wineboot-full.log"; echo "....."; tail -120 "$OUT/wineboot-full.log"; } > "$OUT/wineboot-$1.log"
    rm -f "$OUT/wineboot-full.log"
    log "wineboot ($1) done in $(( $(date +%s) - start )) s; kernel32 errors: $(grep -c 'could not load kernel32' "$OUT/wineboot-$1.log")"
    ! grep -q "could not load kernel32" "$OUT/wineboot-$1.log"
}
# A new prefix takes about 17 minutes under Box64, so it is kept in the Actions cache
# ($PREFIX_CACHE). Winlator's container pattern alone does not start (kernel32 is missing).
PREFIX_CACHE="${PREFIX_CACHE:-$HOME/../lab-cache/prefix.tar}"
if [ -f "$PREFIX_CACHE" ]; then
    rm -rf "$WINEPREFIX"
    tar -xf "$PREFIX_CACHE" -C "$HOME"
    log "prefix from the cache"
elif ! wineboot_prefix pattern; then
    log "the container pattern does not start; trying a new prefix"
    mv "$WINEPREFIX" "$WINEPREFIX.pattern"
    if wineboot_prefix fresh; then
        mkdir -p "$(dirname "$PREFIX_CACHE")"
        tar -cf "$PREFIX_CACHE" -C "$HOME" .wine && log "prefix saved for the cache"
    else
        log "a new prefix does not start either"
    fi
fi
ls "$WINEPREFIX/drive_c/windows/system32" 2>/dev/null | head -20 >> "$SUMMARY"
ls "$ROOTFS/opt/wine/lib/wine/" >> "$SUMMARY"

# --- the test program ----------------------------------------------------------------------
APPDIR="$WINEPREFIX/drive_c/apps"
mkdir -p "$APPDIR"
fetch_github_zip() {   # repo, name pattern
    local api="https://api.github.com/repos/$1/releases/latest" url
    curl -sSL -H "Authorization: Bearer ${GH_TOKEN:-}" "$api" > "$OUT/release-$(basename "$1").json"
    url=$(python3 - "$OUT/release-$(basename "$1").json" "$2" <<'EOF'
import json, re, sys
data = json.load(open(sys.argv[1]))
for a in data.get("assets", []):
    if re.search(sys.argv[2], a["name"], re.I) and a["name"].lower().endswith(".zip"):
        print(a["browser_download_url"]); break
EOF
)
    [ -n "$url" ] || return 1
    log "download $url"
    curl -sSL -o "$LAB/$(basename "$1").zip" "$url" && unzip -q -o "$LAB/$(basename "$1").zip" -d "$APPDIR/$(basename "$1")"
}
if [ "${CONFIGS:-}" = "steam" ]; then EXE=none
elif [ -n "${TEST_EXE:-}" ]; then EXE="$TEST_EXE"; else
    fetch_github_zip Arcthesia/ArcCreate "win" || log "no ArcCreate windows zip"
    EXE=$(find "$APPDIR" -maxdepth 4 -iname "*.exe" ! -iname "UnityCrashHandler*" | head -1)
fi
[ -n "$EXE" ] || { log "no test program"; exit 1; }
GAMEDIR="$(dirname "$EXE")"
if [ "$EXE" != none ]; then
    log "test program: $EXE"
    if [ -d "$GAMEDIR/MonoBleedingEdge" ]; then log "scripting: Mono"; elif [ -f "$GAMEDIR/GameAssembly.dll" ]; then log "scripting: IL2CPP"; else log "scripting: unknown"; fi
    ls "$GAMEDIR" >> "$SUMMARY"
    find "$GAMEDIR" -maxdepth 2 \( -name globalgamemanagers -o -name data.unity3d \) | head -2 | while read -r f; do
        log "unity version: $(strings "$f" | grep -m1 -E '^20[0-9]{2}\.[0-9]+\.[0-9]+[abfp][0-9]+')"
    done
fi

# --- runs ------------------------------------------------------------------------------------
declare -A RUNS
run_config() {   # name, env...
    local name="$1"; shift
    RUNS[$name]=$(( ${RUNS[$name]:-0} + 1 ))
    [ "${RUNS[$name]}" -gt 1 ] && name="$name-${RUNS[$name]}"
    local started alive=yes t pid
    log "=== $name: $*"
    find "$WINEPREFIX/drive_c/users" -name "Player*.log" -delete 2>/dev/null
    started=$(date +%s)
    ( cd "$GAMEDIR" && env $(base_env) "$@" timeout "$RUN_SECONDS" "$BOX64" "$WINE" "$EXE" ${GAME_ARGS:-} > "$OUT/$name-wine.log" 2>&1 ) &
    pid=$!
    for t in 20 45 75 110 150 190; do
        while [ $(( $(date +%s) - started )) -lt "$t" ]; do
            kill -0 "$pid" 2>/dev/null || break
            sleep 2
        done
        if ! kill -0 "$pid" 2>/dev/null; then alive=no; break; fi
        import -window root -resize 50% -quality 70 "$OUT/$name-${t}s.jpg" 2>/dev/null
        log "$name ${t}s: running, windows: $(xwininfo -root -children 2>/dev/null | grep -c '"')"
    done
    wait "$pid"; local status=$?
    log "$name: exit status $status after $(( $(date +%s) - started )) s (alive at end: $alive)"
    env $(base_env) timeout 60 "$BOX64" "$WINESERVER" -k > /dev/null 2>&1
    sleep 3
    find "$WINEPREFIX/drive_c/users" -name "Player*.log" | while read -r f; do
        cp "$f" "$OUT/$name-$(basename "$f")"
        log "$name: $(basename "$f") $(wc -l < "$f") lines, last: $(grep -v '^\s*$' "$f" | tail -3 | tr '\n' ' ' | cut -c1-400)"
    done
    tail -40 "$OUT/$name-wine.log" > "$OUT/$name-wine-tail.txt"
}

# each one twice: memory ordering bugs do not show every time
# DXVK as S11 Workspace ships it, on Mesa's software Vulkan (lavapipe) instead of Vortek
install_dxvk() {
    [ -f "$WINEPREFIX/.lab-dxvk" ] && return
    zstd -dc "$REPO/app/src/main/assets/dxwrapper/dxvk-2.4.1.tzst" | tar -x -C "$WINEPREFIX/drive_c/windows"
    touch "$WINEPREFIX/.lab-dxvk"
    log "DXVK 2.4.1 installed"
}
DXVK_ENV="WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d;d3d11,dxgi,d3d10core,d3d9,d3d8=n,b VK_ICD_FILENAMES=$(ls /usr/share/vulkan/icd.d/lvp_icd*.json 2>/dev/null | head -1) DXVK_LOG_LEVEL=info"

# --- the Steam client: Steam games quit when started without it -----------------------------
STEAM_ARGS="${STEAM_ARGS:--no-cef-sandbox -cef-disable-gpu -cef-disable-gpu-compositing -cef-disable-d3d11}"
run_steam() {
    local setup="$APPDIR/SteamSetup.exe" started pid t status alive=yes steamdir   # inside a Wine drive
    curl -sSL -o "$setup" https://cdn.cloudflare.steamstatic.com/client/installer/SteamSetup.exe || { log "steam: download failed"; return; }
    log "steam: installer $(stat -c %s "$setup") bytes, $(file -b "$setup" | cut -c1-60)"
    started=$(date +%s)
    env $(base_env) $(preset_env PERFORMANCE) timeout 900 "$BOX64" "$WINE" "$setup" /S > "$OUT/steam-setup.log" 2>&1
    log "steam: installer exit $? after $(( $(date +%s) - started )) s"
    # the installer starts its own child and returns at once: wait for steam.exe
    for t in $(seq 1 60); do
        STEAM=$(find "$WINEPREFIX/drive_c" -maxdepth 4 -iname steam.exe 2>/dev/null | head -1)
        [ -n "$STEAM" ] && break
        sleep 3
    done
    env $(base_env) timeout 120 "$BOX64" "$WINESERVER" -w
    steamdir=$(dirname "${STEAM:-/nonexistent/x}")
    [ -f "$STEAM" ] || { log "steam: steam.exe missing"; ls -la "$WINEPREFIX/drive_c/Program Files (x86)" "$WINEPREFIX/drive_c/Program Files" >> "$SUMMARY" 2>&1; tail -40 "$OUT/steam-setup.log" > "$OUT/steam-setup-tail.txt"; return; }
    log "steam: $STEAM ($(file -b "$STEAM" | cut -c1-50))"
    started=$(date +%s)
    ( cd "$steamdir" && env $(base_env) $(preset_env PERFORMANCE) ${STEAM_ENV:-} timeout "${STEAM_SECONDS:-1500}" "$BOX64" "$WINE" "$STEAM" $STEAM_ARGS > "$OUT/steam-wine.log" 2>&1 ) &
    pid=$!
    for t in 60 120 240 360 480 600 780 960 1140 1320 1480; do
        while [ $(( $(date +%s) - started )) -lt "$t" ]; do
            kill -0 "$pid" 2>/dev/null || break
            sleep 5
        done
        if ! kill -0 "$pid" 2>/dev/null; then alive=no; break; fi
        import -window root -resize 50% -quality 70 "$OUT/steam-${t}s.jpg" 2>/dev/null
        log "steam ${t}s: windows: $(xwininfo -root -children 2>/dev/null | grep -c '"'), $(xwininfo -root -children 2>/dev/null | grep -o '"[^"]*"' | sort -u | grep -v '"Wine' | head -6 | tr '\n' ' ')"
        log "steam ${t}s: bootstrap: $(tail -1 "$steamdir/logs/bootstrap_log.txt" 2>/dev/null | cut -c1-200)"
        log "steam ${t}s: processes: $(ps -eo args | grep -io '[a-z_]*\.exe' | sort | uniq -c | tr '\n' ' ' | cut -c1-300)"
    done
    wait "$pid"; status=$?
    log "steam: exit status $status after $(( $(date +%s) - started )) s (alive at end: $alive)"
    env $(base_env) timeout 60 "$BOX64" "$WINESERVER" -k > /dev/null 2>&1
    mkdir -p "$OUT/steam-logs"
    cp "$steamdir"/logs/*.txt "$OUT/steam-logs/" 2>/dev/null
    tail -80 "$OUT/steam-wine.log" > "$OUT/steam-wine-tail.txt"
    du -sh "$steamdir" >> "$SUMMARY" 2>&1
}

IFS=',' read -r -a CONFIGS <<< "${CONFIGS:-perf_unity0,perf_unity1,perf_unity0,perf_unity1,stability}"
for c in "${CONFIGS[@]}"; do
    case "$c" in
        perf_unity0) run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=0 ;;
        perf_unity1) run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=1 ;;
        perf_unity1_mono) run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=1 BOX64_DYNAREC_STRONGMEM=1 BOX64_DYNAREC_BIGBLOCK=0 ;;
        stability) run_config "$c" $(preset_env STABILITY) BOX64_UNITYPLAYER=0 ;;
        perf_unity0_seh) WINEDEBUG=+seh,+loaddll run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=0 BOX64_SHOWSEGV=1 ;;
        perf_unity0_direct) GAME_ARGS=-force-gfx-direct run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=0 ;;
        perf_unity0_dxvk) install_dxvk; run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=0 $DXVK_ENV ;;
        perf_unity1_dxvk) install_dxvk; run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=1 $DXVK_ENV ;;
        stability_dxvk) install_dxvk; run_config "$c" $(preset_env STABILITY) BOX64_UNITYPLAYER=0 $DXVK_ENV ;;
        steam) run_steam ;;
    esac
done
log "done $(date -u)"
