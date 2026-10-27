#!/bin/bash
# S11 lab: browsers, the way the app would run them.
#  - today's containers: x64 builds under Winlator's Wine 10.10 and Box64 0.4.4
#  - the ARM64 Wine (the release the app downloads): ARM64 builds as they are, x64 ones via FEX
# Firefox and Chromium (ungoogled-chromium's Windows builds), headless: a JavaScript page that
# times itself, and the time until its screenshot is written.
# Usage: browser_lab.sh <repo> <out dir>
set -u
REPO="$(cd "$1" && pwd)"
OUT="$2"
LAB="${LAB:-$HOME/lab}"
mkdir -p "$LAB" "$OUT"
SUMMARY="$OUT/summary.txt"
RESULTS="$OUT/results.tsv"
log() { echo "[lab] $*" | tee -a "$SUMMARY"; }
result() { printf '%s\t%s\t%s\n' "$1" "$2" "$3" >> "$RESULTS"; }
printf 'config\ttest\tvalue\n' > "$RESULTS"
log "start $(date -u)"
lscpu | grep -iE "model name|^CPU\(s\)" >> "$SUMMARY"

sudo apt-get update -qq > /dev/null
for p in xvfb zstd cmake build-essential git imagemagick x11-utils unzip 7zip curl jq python3; do
    sudo apt-get install -y -qq "$p" > /dev/null 2>&1 || log "package missing: $p"
done
gh_latest() { curl -sSL -H "Authorization: Bearer ${GH_TOKEN:-}" "https://api.github.com/repos/$1/releases/latest"; }
fetch() { [ -s "$2" ] || curl -sSL --retry 3 -o "$2" "$1" || log "download failed: $1"; }

# --- the rootfs, where the app has it (both package names point at it) -------------------------
mkdir -p "$LAB/rootfs"
[ -x "$LAB/rootfs/opt/wine/bin/wine" ] || zstd -dc "$REPO/app/src/main/assets/rootfs.tzst" | tar -x -C "$LAB/rootfs"
sudo mkdir -p /data/data/com.winlator/files /data/data/com.winlatab/files
sudo ln -sfn "$LAB/rootfs" /data/data/com.winlator/files/rootfs
sudo ln -sfn "$LAB/rootfs" /data/data/com.winlatab/files/rootfs
ROOTFS=/data/data/com.winlatab/files/rootfs
mkdir -p "$LAB/rootfs/tmp" "$LAB/rootfs/home/xuser" "$LAB/rootfs/opt/installed-wine"
chmod 1777 "$LAB/rootfs/tmp"
LIBS="$ROOTFS/usr/lib"
XHOME="$ROOTFS/home/xuser"

Xvfb :1 -screen 0 1920x1080x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
export DISPLAY=:1
sleep 3
rm -rf "$LAB/rootfs/tmp/.X11-unix" && ln -s /tmp/.X11-unix "$LAB/rootfs/tmp/.X11-unix"

# --- today's containers: Winlator's Wine with Box64 (the prefix from the Unity lab's cache) -------
BOX64=""
if git clone -q --depth 1 -b v0.4.4 https://github.com/ptitSeb/box64.git "$LAB/box64-src" 2>/dev/null || [ -d "$LAB/box64-src" ]; then
    cmake -S "$LAB/box64-src" -B "$LAB/box64-build" -DARM_DYNAREC=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$OUT/box64-cmake.log" 2>&1
    make -C "$LAB/box64-build" -j"$(nproc)" > "$OUT/box64-build.log" 2>&1 && BOX64="$LAB/box64-build/box64"
fi
log "box64: ${BOX64:-none}"
XPREFIX="$XHOME/.wine"
PREFIX_CACHE="${PREFIX_CACHE:-$HOME/lab-cache/prefix.tar}"
if [ -f "$PREFIX_CACHE" ]; then rm -rf "$XPREFIX"; tar -xf "$PREFIX_CACHE" -C "$XHOME"; log "Winlator prefix from the cache"
else log "no cached Winlator prefix: no Box64 runs"; fi
cp "$REPO/app/src/main/assets/box64/default.box64rc" "$LAB/config.box64rc"
BOXENV="BOX64_NOBANNER=1 BOX64_DYNAREC=1 BOX64_RCFILE=$LAB/config.box64rc \
BOX64_DYNAREC_SAFEFLAGS=1 BOX64_DYNAREC_FASTNAN=1 BOX64_DYNAREC_FASTROUND=1 BOX64_DYNAREC_X87DOUBLE=0 \
BOX64_DYNAREC_BIGBLOCK=3 BOX64_DYNAREC_STRONGMEM=0 BOX64_DYNAREC_FORWARD=512 BOX64_DYNAREC_CALLRET=1 \
BOX64_DYNAREC_WAIT=1 BOX64_DYNAREC_NATIVEFLAGS=1 BOX64_DYNAREC_WEAKBARRIER=2 \
HOME=$XHOME USER=xuser TMPDIR=$ROOTFS/tmp WINEPREFIX=$XPREFIX WINEDEBUG=-all LD_LIBRARY_PATH=$LIBS \
BOX64_LD_LIBRARY_PATH=$LAB/rootfs/usr/lib/x86_64-linux-gnu:$LAB/box64-src/x64lib \
WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d"

# --- the ARM64 Wine, from the release the app downloads -------------------------------------------
PKG_URL="https://github.com/kim-kyum/winlator-app/releases/download/s11-wine-arm64-11.16/wine-11.16-arm64.tzst"
fetch "$PKG_URL" "$LAB/wine-arm64.tzst"
rm -rf "$LAB/rootfs/opt/installed-wine/wine-11.16-arm64"
zstd -dc "$LAB/wine-arm64.tzst" | tar -x -C "$LAB/rootfs/opt/installed-wine"
W="$ROOTFS/opt/installed-wine/wine-11.16-arm64"
APREFIX="$XHOME/.wine-arm64"
AENV="HOME=$XHOME USER=xuser TMPDIR=$ROOTFS/tmp WINEPREFIX=$APREFIX LD_LIBRARY_PATH=$LIBS LD_PRELOAD=$W/lib/libs11tmp.so \
WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d"
env -i DISPLAY=$DISPLAY $AENV timeout 600 "$W/bin/wine" wineboot -i > "$OUT/arm64-wineboot.log" 2>&1
env -i $AENV timeout 60 "$W/bin/wineserver" -w
log "ARM64 Wine prefix: $(ls "$APREFIX/drive_c/windows/system32" 2>/dev/null | wc -l) files in system32"

# --- the browsers ---------------------------------------------------------------------------------
B="$LAB/bench"; mkdir -p "$B"
cat > "$B/page.html" <<'HTMLEOF'
<!doctype html><meta charset="utf-8"><body style="font:28px monospace;background:#fff;color:#000;margin:24px">
<div id="out">running</div>
<script>
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
const lines = [];
function time(name, f) { const t = performance.now(); f(); const ms = Math.round(performance.now() - t); lines.push(name + " " + ms); return ms; }
let total = 0;
total += time("fib30", () => fib(30));
total += time("sort", () => { const a = []; for (let i = 0; i < 1000000; i++) a.push((i * 7919) % 1000003); a.sort((x, y) => x - y); });
total += time("map", () => { const m = new Map(); for (let i = 0; i < 300000; i++) m.set("k" + i, {i, s: "v" + i}); let s = 0; for (const [k, v] of m) s += v.i; });
total += time("json", () => { const o = []; for (let i = 0; i < 100000; i++) o.push({id: i, name: "item" + i, tags: ["a", "b", i]}); JSON.parse(JSON.stringify(o)); });
total += time("regex", () => { const s = "abc123def456 ".repeat(100000); let c = 0; for (const m of s.matchAll(/[a-z]+(\d+)/g)) c += m[1].length; });
total += time("dom", () => { const d = document.createElement("div"); for (let i = 0; i < 20000; i++) { const e = document.createElement("span"); e.textContent = "x" + i; d.appendChild(e); } document.body.appendChild(d); d.getBoundingClientRect(); d.remove(); });
lines.push("total " + total);
document.getElementById("out").innerHTML = lines.join("<br>");
</script>
HTMLEOF
for a in aarch64 x64; do
    os=$([ $a = aarch64 ] && echo win64-aarch64 || echo win64)
    fetch "https://download.mozilla.org/?product=firefox-latest-ssl&os=$os&lang=en-US" "$B/firefox-$a.exe"
    [ -d "$B/firefox-$a" ] || { mkdir -p "$B/ff-$a-x" && 7z x -y -o"$B/ff-$a-x" "$B/firefox-$a.exe" > /dev/null && mv "$B/ff-$a-x/core" "$B/firefox-$a"; }
done
REL=$(gh_latest ungoogled-software/ungoogled-chromium-windows)
echo "$REL" | jq -r '.tag_name, (.assets[].name)' > "$OUT/chromium-release.txt"
for a in arm64 x64; do
    url=$(echo "$REL" | jq -r --arg a "$a" '.assets[] | select(.name | test("windows_" + $a + "\\.zip$")) | .browser_download_url' | head -1)
    if [ -n "$url" ]; then
        fetch "$url" "$B/chromium-$a.zip"
        [ -d "$B/chromium-$a" ] || { mkdir -p "$B/chromium-$a.x" && unzip -q -o "$B/chromium-$a.zip" -d "$B/chromium-$a.x" && mv "$B/chromium-$a.x"/* "$B/chromium-$a"; }
    else log "no ungoogled-chromium $a build"; fi
done
ls "$B" >> "$SUMMARY"
mkdir -p "$B/ffprofile"
cat > "$B/ffprofile/user.js" <<'JSEOF'
user_pref("security.sandbox.content.level", 0);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRunURL", "");
JSEOF
MOZ="MOZ_HEADLESS=1 MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1 \
MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1 MOZ_DISABLE_UTILITY_SANDBOX=1 MOZ_DISABLE_GMP_SANDBOX=1"

# runs a browser twice (the first run makes its profile), records the second
browse() {   # config, browser (firefox/chromium), arch dir, launcher...
    local config="$1" browser="$2" arch="$3"; shift 3
    local name="$config-$browser" exe args shot="C:\\bench\\shot-$config-$browser.png"
    if [ "$browser" = firefox ]; then
        exe="C:\\bench\\firefox-$arch\\firefox.exe"
        rm -rf "$B/ffp-$config"; mkdir -p "$B/ffp-$config"; cp "$B/ffprofile/user.js" "$B/ffp-$config/"
        args="--headless --no-remote -profile C:\\bench\\ffp-$config --window-size=1100,700 --screenshot $shot file:///C:/bench/page.html"
    else
        exe="C:\\bench\\chromium-$arch\\chrome.exe"
        args="--headless=new --no-sandbox --disable-gpu --user-data-dir=C:\\bench\\crp-$config --window-size=1100,700 --screenshot=$shot file:///C:/bench/page.html"
    fi
    local run
    for run in first second; do
        rm -f "$B/shot-$config-$browser.png"
        local t0=$(date +%s.%N)
        ( cd "$B" && timeout 300 "$@" "$exe" $args > "$OUT/$name-$run.log" 2>&1 )
        local st=$? secs=$(python3 -c "print(round($(date +%s.%N) - $t0, 2))")
        log "$name ($run run): exit $st after $secs s; screenshot: $([ -f "$B/shot-$config-$browser.png" ] && echo yes || echo no)"
        [ $run = second ] && result "$config" "$browser-wall-s" "$secs"
    done
    [ -f "$B/shot-$config-$browser.png" ] && cp "$B/shot-$config-$browser.png" "$OUT/" || tail -12 "$OUT/$name-second.log" | sed 's/^/    /' >> "$SUMMARY"
}
mkdir -p "$APREFIX/drive_c"; ln -sfn "$B" "$APREFIX/drive_c/bench"
if [ -n "$BOX64" ] && [ -d "$XPREFIX" ]; then
    mkdir -p "$XPREFIX/drive_c"; ln -sfn "$B" "$XPREFIX/drive_c/bench"
    env $BOXENV "$BOX64" "$ROOTFS/opt/wine/bin/wineserver" -p > /dev/null 2>&1 &
    sleep 5
    browse box64-x64 firefox x64 env -i DISPLAY=$DISPLAY $BOXENV $MOZ "$BOX64" "$ROOTFS/opt/wine/bin/wine"
    browse box64-x64 chromium x64 env -i DISPLAY=$DISPLAY $BOXENV "$BOX64" "$ROOTFS/opt/wine/bin/wine"
    env $BOXENV "$BOX64" "$ROOTFS/opt/wine/bin/wineserver" -k > /dev/null 2>&1; sleep 3
fi
env -i $AENV "$W/bin/wineserver" -p > /dev/null 2>&1 &
sleep 3
browse arm64wine-arm64 firefox aarch64 env -i DISPLAY=$DISPLAY $AENV $MOZ "$W/bin/wine"
browse arm64wine-x64 firefox x64 env -i DISPLAY=$DISPLAY $AENV $MOZ "$W/bin/wine"
browse arm64wine-arm64 chromium arm64 env -i DISPLAY=$DISPLAY $AENV "$W/bin/wine"
browse arm64wine-x64 chromium x64 env -i DISPLAY=$DISPLAY $AENV "$W/bin/wine"
env -i $AENV "$W/bin/wineserver" -k > /dev/null 2>&1

python3 - "$RESULTS" > "$OUT/table.txt" <<'EOF'
import csv, sys
rows = list(csv.reader(open(sys.argv[1]), delimiter="\t"))[1:]
for c, t, v in rows: print(f"{c:18s} {t:22s} {v}")
EOF
cat "$OUT/table.txt" >> "$SUMMARY"
log "done $(date -u)"
