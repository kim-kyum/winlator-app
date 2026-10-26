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
if [ ! -x "$LAB/rootfs/opt/wine/bin/wine" ]; then
    mkdir -p "$LAB/rootfs"
    zstd -dc "$REPO/app/src/main/assets/rootfs.tzst" | tar -x -C "$LAB/rootfs" ./opt/wine ./usr/lib/x86_64-linux-gnu
fi
WINE="$LAB/rootfs/opt/wine/bin/wine"
WINESERVER="$LAB/rootfs/opt/wine/bin/wineserver"
export WINEPREFIX="$LAB/prefix"
if [ ! -d "$WINEPREFIX" ]; then
    mkdir -p "$LAB/pattern"
    zstd -dc "$REPO/app/src/main/assets/container_pattern.tzst" | tar -x -C "$LAB/pattern"
    mv "$LAB/pattern/.wine" "$WINEPREFIX"
    ln -sfn / "$WINEPREFIX/dosdevices/z:"
fi

export HOME="$LAB/home" USER=xuser
mkdir -p "$HOME"
export BOX64_LD_LIBRARY_PATH="$LAB/rootfs/usr/lib/x86_64-linux-gnu:$LAB/box64-src/x64lib"
export WINEDEBUG="${WINEDEBUG:--all}"
export WINEDLLOVERRIDES="winemenubuilder.exe=d"
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
log "wineboot"
start=$(date +%s)
env $(base_env) $(preset_env STABILITY) BOX64_UNITYPLAYER=0 timeout 900 "$BOX64" "$WINE" wineboot -u > "$OUT/wineboot.log" 2>&1
env $(base_env) timeout 120 "$BOX64" "$WINESERVER" -w
log "wineboot done in $(( $(date +%s) - start )) s"

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
if [ -n "${TEST_EXE:-}" ]; then EXE="$TEST_EXE"; else
    fetch_github_zip Arcthesia/ArcCreate "win" || log "no ArcCreate windows zip"
    EXE=$(find "$APPDIR" -maxdepth 4 -iname "*.exe" ! -iname "UnityCrashHandler*" | head -1)
fi
[ -n "$EXE" ] || { log "no test program"; exit 1; }
GAMEDIR="$(dirname "$EXE")"
log "test program: $EXE"
if [ -d "$GAMEDIR/MonoBleedingEdge" ]; then log "scripting: Mono"; elif [ -f "$GAMEDIR/GameAssembly.dll" ]; then log "scripting: IL2CPP"; else log "scripting: unknown"; fi
ls "$GAMEDIR" >> "$SUMMARY"
find "$GAMEDIR" -maxdepth 2 \( -name globalgamemanagers -o -name data.unity3d \) | head -2 | while read -r f; do
    log "unity version: $(strings "$f" | grep -m1 -E '^20[0-9]{2}\.[0-9]+\.[0-9]+[abfp][0-9]+')"
done

# --- runs ------------------------------------------------------------------------------------
run_config() {   # name, env...
    local name="$1"; shift
    local started alive=yes t pid
    log "=== $name: $*"
    find "$WINEPREFIX/drive_c/users" -name "Player*.log" -delete 2>/dev/null
    started=$(date +%s)
    ( cd "$GAMEDIR" && env $(base_env) "$@" timeout "$RUN_SECONDS" "$BOX64" "$WINE" "$EXE" > "$OUT/$name-wine.log" 2>&1 ) &
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

IFS=',' read -r -a CONFIGS <<< "${CONFIGS:-perf_unity0,perf_unity1,perf_unity1_mono,stability}"
for c in "${CONFIGS[@]}"; do
    case "$c" in
        perf_unity0) run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=0 ;;
        perf_unity1) run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=1 ;;
        perf_unity1_mono) run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=1 BOX64_DYNAREC_STRONGMEM=1 BOX64_DYNAREC_BIGBLOCK=0 ;;
        stability) run_config "$c" $(preset_env STABILITY) BOX64_UNITYPLAYER=0 ;;
        perf_unity0_seh) WINEDEBUG=+seh,+loaddll run_config "$c" $(preset_env PERFORMANCE) BOX64_UNITYPLAYER=0 BOX64_SHOWSEGV=1 ;;
    esac
done
log "done $(date -u)"
