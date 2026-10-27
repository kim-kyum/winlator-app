#!/bin/bash
# S11 perf lab: how fast are Windows programs on an ARM64 CPU, the way S11 Workspace runs them
# (Winlator's x86-64 Wine under Box64 0.4.4) against the alternatives:
#   box64        Box64 0.4.4, PERFORMANCE preset, no DynaCache (what the app does today)
#   box64cache   the same with Box64's DynaCache (translated code kept on disk), second run
#   box64new     the newest Box64 release, same Wine
#   hangover64   ARM64EC Wine (Hangover) running the same x64 programs through FEX
#   hangoverarm  the same Wine running the Windows ARM64 builds of the programs (no emulation)
#   native       Linux ARM64 builds, as the ceiling
# Programs: 7-Zip (compression), Node.js (V8, the engine of Chrome and Electron apps), Java
# (HotSpot, the engine of ImageJ and Tracker), Python. Each prints its own timings; the wall
# time adds process and runtime start-up.
#
# Usage: perf_lab.sh <repo> <out dir>      (needs sudo for packages)
set -u
REPO="$(cd "$1" && pwd)"
OUT="$2"
LAB="${LAB:-$HOME/lab}"
mkdir -p "$LAB" "$OUT"
SUMMARY="$OUT/summary.txt"
RESULTS="$OUT/results.tsv"
CONFIGS="${PERF_CONFIGS:-native,box64,box64cache,box64new,hangover64,hangoverarm}"

log() { echo "[lab] $*" | tee -a "$SUMMARY"; }
result() { printf '%s\t%s\t%s\n' "$1" "$2" "$3" >> "$RESULTS"; }   # config, test, value

log "start $(date -u)"
uname -a >> "$SUMMARY"
grep -m1 -i "features" /proc/cpuinfo >> "$SUMMARY"
lscpu | grep -iE "model name|^CPU\(s\)|MHz" >> "$SUMMARY"
printf 'config\ttest\tvalue\n' > "$RESULTS"

# --- packages --------------------------------------------------------------------------
sudo apt-get update -qq > /dev/null
for p in xvfb zstd cmake build-essential python3 git imagemagick x11-utils unzip 7zip curl jq \
         libgl1-mesa-dri libglx-mesa0 libgl1 libegl1 mesa-vulkan-drivers libvulkan1 \
         libfreetype6 libfontconfig1 libgnutls30t64 libpulse0 libasound2t64 libxrandr2 \
         libxinerama1 libxcursor1 libxi6 libxcomposite1 libxext6 libxrender1 libxfixes3 \
         libxxf86vm1 libxkbcommon0 libsdl2-2.0-0 libdbus-1-3 libunwind8 libcups2t64 \
         nodejs openjdk-21-jdk-headless; do
    sudo apt-get install -y -qq "$p" > /dev/null 2>&1 || log "package missing: $p"
done

gh_latest() {   # repo -> release json
    curl -sSL -H "Authorization: Bearer ${GH_TOKEN:-}" "https://api.github.com/repos/$1/releases/latest"
}

# --- Box64 builds ----------------------------------------------------------------------------
build_box64() {   # tag, dir
    [ -x "$2/box64" ] && return 0
    git clone -q --depth 1 -b "$1" https://github.com/ptitSeb/box64.git "$2-src" || return 1
    cmake -S "$2-src" -B "$2" -DARM_DYNAREC=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$OUT/box64-$1-cmake.log" 2>&1
    make -C "$2" -j"$(nproc)" > "$OUT/box64-$1-build.log" 2>&1 || { log "box64 $1 build failed"; tail -30 "$OUT/box64-$1-build.log" >> "$SUMMARY"; return 1; }
}
build_box64 v0.4.4 "$LAB/box64-build" || exit 1
BOX64="$LAB/box64-build/box64"
log "box64: $("$BOX64" --version 2>&1 | head -1)"
grep -n "DYNACACHE" "$LAB/box64-build-src/docs/USAGE.md" | head -5 >> "$SUMMARY"
NEW_TAG=$(gh_latest ptitSeb/box64 | jq -r .tag_name)
log "newest Box64 release: $NEW_TAG"
BOX64NEW=""
if [ -n "$NEW_TAG" ] && [ "$NEW_TAG" != "null" ] && [ "$NEW_TAG" != v0.4.4 ]; then
    build_box64 "$NEW_TAG" "$LAB/box64-new" && BOX64NEW="$LAB/box64-new/box64"
    [ -n "$BOX64NEW" ] && log "box64new: $("$BOX64NEW" --version 2>&1 | head -1)"
fi

# --- Winlator's Wine and container prefix (as in unity_lab.sh) -------------------------------
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
XHOME="$ROOTFS/home/xuser"
XPREFIX="$XHOME/.wine"
PREFIX_CACHE="${PREFIX_CACHE:-$HOME/lab-cache/prefix.tar}"
if [ -f "$PREFIX_CACHE" ]; then
    rm -rf "$XPREFIX"
    tar -xf "$PREFIX_CACHE" -C "$XHOME"
    log "Winlator prefix from the cache"
else
    log "no cached Winlator prefix ($PREFIX_CACHE): the box64 runs need one; run the Unity lab first"
fi

Xvfb :1 -screen 0 1920x1080x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
export DISPLAY=:1
sleep 3

cp "$REPO/app/src/main/assets/box64/default.box64rc" "$LAB/config.box64rc"
# GuestProgramLauncherComponent and Box64PresetManager (PERFORMANCE)
BOXENV="BOX64_NOBANNER=1 BOX64_DYNAREC=1 BOX64_DYNACACHE=0 BOX64_UNITYPLAYER=1 BOX64_RCFILE=$LAB/config.box64rc \
BOX64_DYNAREC_SAFEFLAGS=1 BOX64_DYNAREC_FASTNAN=1 BOX64_DYNAREC_FASTROUND=1 BOX64_DYNAREC_X87DOUBLE=0 \
BOX64_DYNAREC_BIGBLOCK=3 BOX64_DYNAREC_STRONGMEM=0 BOX64_DYNAREC_FORWARD=512 BOX64_DYNAREC_CALLRET=1 \
BOX64_DYNAREC_WAIT=1 BOX64_DYNAREC_NATIVEFLAGS=1 BOX64_DYNAREC_WEAKBARRIER=2 \
HOME=$XHOME USER=xuser TMPDIR=$ROOTFS/tmp WINEPREFIX=$XPREFIX WINEDEBUG=-all \
BOX64_LD_LIBRARY_PATH=$LAB/rootfs/usr/lib/x86_64-linux-gnu:$LAB/box64-build-src/x64lib \
WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d"

# --- Hangover: ARM64EC Wine with FEX ---------------------------------------------------------
HPREFIX="$LAB/hangover-prefix"
HWINE=""
install_hangover() {
    local json url
    json=$(gh_latest AndreRH/hangover)
    echo "$json" | jq -r '.tag_name, (.assets[].name)' > "$OUT/hangover-release.txt"
    url=$(echo "$json" | jq -r '.assets[] | select(.name | test("(2404|noble)"; "i")) | select(.name | test("arm64|aarch64"; "i")) | .browser_download_url' | head -1)
    [ -n "$url" ] || { log "hangover: no Ubuntu 24.04 arm64 asset"; return 1; }
    log "hangover: $url"
    mkdir -p "$LAB/hangover" && cd "$LAB/hangover" || return 1
    curl -sSL -o pkg "$url" || return 1
    case "$url" in
        *.tar|*.tar.*) tar -xf pkg ;;
        *.zip) unzip -q pkg ;;
        *.deb) mv pkg hangover.deb ;;
    esac
    ls -la >> "$SUMMARY"
    sudo apt-get install -y -qq ./*.deb > "$OUT/hangover-install.log" 2>&1 || { log "hangover: install failed"; tail -20 "$OUT/hangover-install.log" >> "$SUMMARY"; cd - > /dev/null; return 1; }
    cd - > /dev/null
    HWINE=$(command -v wine)
    log "hangover wine: $HWINE $(WINEPREFIX=$HPREFIX "$HWINE" --version 2>&1 | tail -1)"
    local t=$(date +%s)
    WINEPREFIX="$HPREFIX" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" timeout 900 "$HWINE" wineboot -u > "$OUT/hangover-wineboot.log" 2>&1
    WINEPREFIX="$HPREFIX" timeout 120 wineserver -w
    log "hangover: prefix in $(( $(date +%s) - t )) s; system32: $(ls "$HPREFIX/drive_c/windows/system32" 2>/dev/null | wc -l) files"
    # which emulator DLLs the prefix uses for x64 and x86 code
    WINEPREFIX="$HPREFIX" "$HWINE" reg query 'HKLM\Software\Microsoft\Wow64' /s >> "$SUMMARY" 2>&1
    ls /usr/lib/wine/aarch64-windows 2>/dev/null | grep -iE "fex|box|xtajit|wow64" >> "$SUMMARY"
}
case ",$CONFIGS," in *,hangover*) install_hangover || log "hangover not available" ;; esac

# --- the programs, x64 and ARM64 Windows builds ----------------------------------------------
B="$LAB/bench"
mkdir -p "$B"
fetch() { [ -s "$2" ] || curl -sSL --retry 3 -o "$2" "$1" || log "download failed: $1"; }

# 7-Zip "extra": 7za.exe for x64 and arm64
SEVEN=$(gh_latest ip7z/7zip | jq -r '.assets[] | select(.name | test("extra\\.7z$")) | .browser_download_url' | head -1)
fetch "$SEVEN" "$B/7z-extra.7z"
mkdir -p "$B/7z" && 7z x -y -o"$B/7z" "$B/7z-extra.7z" > /dev/null
ls -R "$B/7z" | head -20 >> "$SUMMARY"

# Node.js 22 (V8)
NODEV=$(curl -sSL https://nodejs.org/dist/latest-v22.x/SHASUMS256.txt | grep -o 'node-v[0-9.]*-win-x64.zip' | head -1 | sed 's/-win-x64.zip//')
for a in x64 arm64; do
    fetch "https://nodejs.org/dist/latest-v22.x/$NODEV-win-$a.zip" "$B/node-$a.zip"
    [ -d "$B/node-$a" ] || { unzip -q -o "$B/node-$a.zip" -d "$B" && mv "$B/$NODEV-win-$a" "$B/node-$a"; }
done
log "node: $NODEV"

# Java 21 (Microsoft Build of OpenJDK has both)
for a in x64 aarch64; do
    fetch "https://aka.ms/download-jdk/microsoft-jdk-21-windows-$a.zip" "$B/jdk-$a.zip"
    if [ ! -d "$B/jdk-$a" ]; then
        mkdir -p "$B/jdk-$a.tmp" && unzip -q -o "$B/jdk-$a.zip" -d "$B/jdk-$a.tmp" && mv "$B/jdk-$a.tmp"/* "$B/jdk-$a" && rmdir "$B/jdk-$a.tmp"
    fi
done
log "jdk: $(ls "$B" | grep jdk | tr '\n' ' ')"

# Python 3.13 embeddable
PYV=3.13.1
for a in amd64 arm64; do
    fetch "https://www.python.org/ftp/python/$PYV/python-$PYV-embed-$a.zip" "$B/py-$a.zip"
    [ -d "$B/py-$a" ] || unzip -q -o "$B/py-$a.zip" -d "$B/py-$a"
done

cat > "$B/bench.js" <<'EOF'
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
function time(name, f) { const t = Date.now(); f(); const ms = Date.now() - t; console.log(name + " " + ms); return ms; }
let total = 0;
total += time("fib30", () => fib(30));
total += time("sort", () => { const a = []; for (let i = 0; i < 1000000; i++) a.push((i * 7919) % 1000003); a.sort((x, y) => x - y); });
total += time("map", () => { const m = new Map(); for (let i = 0; i < 300000; i++) m.set("k" + i, {i, s: "v" + i}); let s = 0; for (const [k, v] of m) s += v.i; });
total += time("json", () => { const o = []; for (let i = 0; i < 100000; i++) o.push({id: i, name: "item" + i, tags: ["a", "b", i]}); JSON.parse(JSON.stringify(o)); });
total += time("regex", () => { const s = "abc123def456 ".repeat(100000); let c = 0; for (const m of s.matchAll(/[a-z]+(\d+)/g)) c += m[1].length; });
console.log("total " + total);
EOF
cat > "$B/Bench.java" <<'EOF'
import java.util.*;
public class Bench {
    static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
    static long time(String name, Runnable r) { long t = System.nanoTime(); r.run(); long ms = (System.nanoTime() - t) / 1000000; System.out.println(name + " " + ms); return ms; }
    public static void main(String[] a) {
        long total = 0;
        total += time("fib32", () -> fib(32));
        total += time("sort", () -> { int[] x = new int[2000000]; for (int i = 0; i < x.length; i++) x[i] = (int)((i * 7919L) % 1000003); Arrays.sort(x); });
        total += time("hashmap", () -> { HashMap<String,Integer> m = new HashMap<>(); for (int i = 0; i < 500000; i++) m.put("k" + i, i); long s = 0; for (int v : m.values()) s += v; });
        total += time("strings", () -> { StringBuilder sb = new StringBuilder(); for (int i = 0; i < 300000; i++) sb.append(i).append(','); String[] p = sb.toString().split(","); });
        total += time("filter", () -> { float[] img = new float[1024 * 1024]; for (int k = 0; k < 10; k++) for (int i = 1; i < img.length - 1; i++) img[i] = (img[i - 1] + img[i] + img[i + 1]) / 3f + k; });
        System.out.println("total " + total);
    }
}
EOF
cat > "$B/bench.py" <<'EOF'
import time
def fib(n): return n if n < 2 else fib(n - 1) + fib(n - 2)
def t(name, f):
    s = time.perf_counter(); f(); ms = int((time.perf_counter() - s) * 1000); print(name, ms); return ms
total = 0
total += t("fib25", lambda: fib(25))
total += t("sort", lambda: sorted((i * 7919) % 1000003 for i in range(500000)))
total += t("dict", lambda: sum({("k%d" % i): i for i in range(300000)}.values()))
total += t("strings", lambda: ",".join(str(i) for i in range(300000)).split(","))
print("total", total)
EOF

# the programs in each Wine prefix's C:\bench (Wine maps it, so paths are Windows paths)
place_bench() {   # prefix
    mkdir -p "$1/drive_c"
    ln -sfn "$B" "$1/drive_c/bench"
}
[ -d "$XPREFIX" ] && place_bench "$XPREFIX"

# --- running ----------------------------------------------------------------------------------
# runs a command, records the wall time and the program's own "total" (or 7-Zip's rating)
measure() {   # config, test, timeout, command...
    local config="$1" test="$2" limit="$3"; shift 3
    local log="$OUT/$config-$test.log" t0 t1 status total
    t0=$(date +%s.%N)
    timeout "$limit" "$@" > "$log" 2>&1
    status=$?
    t1=$(date +%s.%N)
    local wall=$(python3 -c "print(round($t1 - $t0, 2))")
    total=$(grep -aE "^total " "$log" | tail -1 | awk '{print $2}')
    [ -z "$total" ] && total=$(grep -a "^Tot:" "$log" | tail -1 | awk '{print $(NF)}')
    result "$config" "$test-wall-s" "$wall"
    [ -n "$total" ] && result "$config" "$test" "$total"
    log "$config $test: exit $status, wall ${wall}s, result ${total:-?}"
    [ "$status" -ne 0 ] && tail -15 "$log" | sed 's/^/    /' >> "$SUMMARY"
}

suite() {   # config, arch (x64/arm64), launcher...
    local config="$1" arch="$2"; shift 2
    local node="C:\\bench\\node-$arch\\node.exe" java="C:\\bench\\jdk-$([ "$arch" = x64 ] && echo x64 || echo aarch64)\\bin\\java.exe"
    local py="C:\\bench\\py-$([ "$arch" = x64 ] && echo amd64 || echo arm64)\\python.exe" seven="C:\\bench\\7z\\$arch\\7za.exe"
    [ "$arch" = x64 ] && seven="C:\\bench\\7z\\x64\\7za.exe"
    measure "$config" start-cmd 300 "$@" cmd /c exit
    measure "$config" start-node 300 "$@" "$node" -e 0
    measure "$config" node 900 "$@" "$node" "C:\\bench\\bench.js"
    measure "$config" start-java 300 "$@" "$java" -version
    measure "$config" java 900 "$@" "$java" "C:\\bench\\Bench.java"
    measure "$config" start-python 300 "$@" "$py" -c pass
    measure "$config" python 900 "$@" "$py" "C:\\bench\\bench.py"
    measure "$config" 7zip-1t 900 "$@" "$seven" b 2 -mmt1 -md22
    measure "$config" 7zip-mt 900 "$@" "$seven" b 2 -md22
}

notepad_window() {   # config, launcher...: seconds until Notepad's window shows
    local config="$1"; shift
    local t0=$(date +%s.%N) t1 found=no
    ( "$@" notepad > /dev/null 2>&1 & )
    for i in $(seq 1 300); do
        if xwininfo -root -tree 2>/dev/null | grep -qi 'notepad'; then found=yes; break; fi
        sleep 0.2
    done
    t1=$(date +%s.%N)
    result "$config" notepad-window-s "$(python3 -c "print(round($t1 - $t0, 2))")"
    log "$config notepad window: $found after $(python3 -c "print(round($t1 - $t0, 2))") s"
    import -window root -resize 50% -quality 70 "$OUT/$config-notepad.jpg" 2>/dev/null
}

IFS=',' read -r -a LIST <<< "$CONFIGS"
for c in "${LIST[@]}"; do
    case "$c" in
        native)
            measure native node 600 node "$B/bench.js"
            mkdir -p "$LAB/javabench" && cp "$B/Bench.java" "$LAB/javabench/"
            measure native java 600 java "$LAB/javabench/Bench.java"
            measure native python 600 python3 "$B/bench.py"
            measure native 7zip-1t 600 7z b 2 -mmt1 -md22
            measure native 7zip-mt 600 7z b 2 -md22
            ;;
        box64|box64cache|box64new)
            [ -d "$XPREFIX" ] || { log "$c: no prefix"; continue; }
            bx="$BOX64"; extra=""
            [ "$c" = box64new ] && { [ -n "$BOX64NEW" ] || { log "box64new: no newer Box64"; continue; }; bx="$BOX64NEW"; }
            if [ "$c" = box64cache ]; then
                rm -rf "$LAB/dynacache"; mkdir -p "$LAB/dynacache"
                extra="BOX64_DYNACACHE=1 BOX64_DYNACACHE_FOLDER=$LAB/dynacache"
            fi
            # wineserver stays up between the programs (start-up of the server is not the test)
            env $BOXENV $extra "$bx" "$WINESERVER" -p > /dev/null 2>&1 &
            sleep 5
            if [ "$c" = box64cache ]; then
                log "box64cache: first pass fills the cache"
                suite box64cache-fill x64 env $BOXENV $extra "$bx" "$WINE"
                log "box64cache: cache $(du -sh "$LAB/dynacache" | cut -f1), $(find "$LAB/dynacache" -type f | wc -l) files"
            fi
            suite "$c" x64 env $BOXENV $extra "$bx" "$WINE"
            notepad_window "$c" env $BOXENV $extra "$bx" "$WINE"
            env $BOXENV "$bx" "$WINESERVER" -k > /dev/null 2>&1; sleep 3
            ;;
        hangover64|hangoverarm)
            [ -n "$HWINE" ] || { log "$c: no Hangover"; continue; }
            place_bench "$HPREFIX"
            HENV="WINEPREFIX=$HPREFIX WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d"
            env $HENV wineserver -p > /dev/null 2>&1 &
            sleep 3
            if [ "$c" = hangover64 ]; then suite "$c" x64 env $HENV "$HWINE"
            else suite "$c" arm64 env $HENV "$HWINE"; fi
            notepad_window "$c" env $HENV "$HWINE"
            env $HENV wineserver -k > /dev/null 2>&1; sleep 3
            ;;
    esac
done

# --- one table: rows are tests, columns configs ------------------------------------------------
python3 - "$RESULTS" > "$OUT/table.txt" <<'EOF'
import csv, sys
rows = list(csv.reader(open(sys.argv[1]), delimiter="\t"))[1:]
configs, tests, data = [], [], {}
for c, t, v in rows:
    if c not in configs: configs.append(c)
    if t not in tests: tests.append(t)
    data[(c, t)] = v
w = max(len(t) for t in tests) + 2
print("".ljust(w) + "".join(c.rjust(16) for c in configs))
for t in tests:
    print(t.ljust(w) + "".join(data.get((c, t), "-").rjust(16) for c in configs))
EOF
cat "$OUT/table.txt" | tee -a "$SUMMARY"
log "done $(date -u)"
