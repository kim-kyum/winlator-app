#!/bin/bash
# S11 lab: can an ARM64 Wine (Hangover's: ARM64EC, FEX for x64 code) run inside Winlator's
# rootfs, with its glibc and libraries, the way S11 Workspace would ship it?
#  1. Hangover's Wine goes to <rootfs>/opt/wine-arm64; its programs get the rootfs's ELF
#     interpreter (as patch_assets.py does for the rootfs's own programs on the device).
#  2. Which libraries Wine's Unix side needs that the rootfs lacks.
#  3. A new prefix, Notepad, and the perf suite: x64 programs through FEX, ARM64 natively.
# Usage: arm64wine_lab.sh <repo> <out dir>
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

sudo apt-get update -qq > /dev/null
for p in xvfb zstd unzip 7zip curl jq patchelf imagemagick x11-utils x11-apps file; do
    sudo apt-get install -y -qq "$p" > /dev/null 2>&1 || log "package missing: $p"
done
gh_latest() { curl -sSL -H "Authorization: Bearer ${GH_TOKEN:-}" "https://api.github.com/repos/$1/releases/latest"; }

# --- Winlator's rootfs where the device has it ----------------------------------------------
ROOTFS=/data/data/com.winlator/files/rootfs
mkdir -p "$LAB/rootfs"
[ -x "$LAB/rootfs/opt/wine/bin/wine" ] || zstd -dc "$REPO/app/src/main/assets/rootfs.tzst" | tar -x -C "$LAB/rootfs"
sudo mkdir -p /data/data/com.winlator/files
sudo ln -sfn "$LAB/rootfs" "$ROOTFS"
mkdir -p "$LAB/rootfs/tmp" "$LAB/rootfs/home/xuser"
chmod 1777 "$LAB/rootfs/tmp"
INTERP=$(ls "$ROOTFS"/usr/lib/ld-linux-aarch64.so.1 "$ROOTFS"/lib/ld-linux-aarch64.so.1 2>/dev/null | head -1)
log "rootfs interpreter: $INTERP"
"$INTERP" --version 2>&1 | head -1 >> "$SUMMARY"
ls "$ROOTFS/usr/lib" | wc -l >> "$SUMMARY"

# --- Hangover's Wine, unpacked (not installed) ------------------------------------------------
json=$(gh_latest AndreRH/hangover)
url=$(echo "$json" | jq -r '.assets[] | select(.name | test("ubuntu2404|noble"; "i")) | .browser_download_url' | head -1)
log "hangover: $url"
mkdir -p "$LAB/hangover" && cd "$LAB/hangover"
curl -sSL -o pkg.tar "$url" && tar -xf pkg.tar
ls -la >> "$SUMMARY"
mkdir -p stage
for d in *.deb; do dpkg-deb -x "$d" stage; done
cd - > /dev/null
find "$LAB/hangover/stage" -maxdepth 3 -type d | head -30 >> "$SUMMARY"
W64="$ROOTFS/opt/wine-arm64"
rm -rf "$W64" && mkdir -p "$W64"
# the packages install to /usr: bin, lib/wine, share/wine
cp -a "$LAB/hangover/stage/usr/." "$W64/"
du -sh "$W64" >> "$SUMMARY"
ls "$W64/bin" "$W64/lib/wine" >> "$SUMMARY" 2>&1
# every program with an ELF interpreter: bin/ and lib/wine/aarch64-unix/ (Wine re-executes
# lib/wine/aarch64-unix/wine, directly or through wine-preloader, which is static)
ELFS=""
while IFS= read -r b; do
    file "$b" | grep -q "ELF.*aarch64.*interpreter" && { ELFS="$ELFS $b"; cp -a "$b" "$b.orig"; }
done < <(find "$W64/bin" "$W64/lib/wine/aarch64-unix" -maxdepth 1 -type f ! -name "*.so")
log "programs to patch:$ELFS"
# run 11: every program dumped core right after patchelf. Run 12: without HOME and USER, Wine
# reads the rootfs's /etc/passwd (there is none) and crashes at NULL; with them (as below) it
# still dumped core in wineboot (SIGBUS). Find where.
sudo apt-get install -y -qq strace gdb binutils > /dev/null 2>&1
LIBS="$ROOTFS/usr/lib"
HOMEDIR="$ROOTFS/home/xuser"
PFX="$HOMEDIR/.wine-arm64"
WENV="HOME=$HOMEDIR USER=xuser TMPDIR=$ROOTFS/tmp WINEPREFIX=$PFX LD_LIBRARY_PATH=$LIBS PATH=$W64/bin:$ROOTFS/usr/bin:/usr/bin:/bin \
WINEDEBUG=-all,err+module WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d"
patch_all() {   # patchelf options...
    for b in $ELFS; do cp -a "$b.orig" "$b"; patchelf "$@" --set-interpreter "$INTERP" "$b" 2>> "$SUMMARY"; done
}
crash_context() {   # strace output: the last 45 lines of a process before each fatal signal
    python3 - "$1" <<'PYEOF'
import collections, re, sys
by_pid = collections.defaultdict(list)
for line in open(sys.argv[1], errors="replace").read().splitlines():
    pid = line.split(" ", 1)[0]
    by_pid[pid].append(line)
    if re.search(r"--- SIG(SEGV|BUS|ILL|ABRT|FPE) \{", line) and "SI_TKILL" not in line:
        print("\n".join(by_pid[pid][-45:]))
        print("-----")
PYEOF
}
Xvfb :1 -screen 0 1920x1080x24 -nolisten tcp -extension MIT-SHM > "$OUT/xvfb.log" 2>&1 &
export DISPLAY=:1
sleep 3
patch_all
{
    echo "== page size: $(getconf PAGESIZE); patchelf $(patchelf --version)"
    echo "== the rootfs's /etc"; ls -la "$ROOTFS/etc" | head -60
    echo "== T2 wine.orig --version (Ubuntu's glibc)"; timeout 60 "$W64/bin/wine.orig" --version; echo "exit $?"
    echo "== T3 patched wine --version, with HOME and USER"; env -i $WENV timeout 60 "$W64/bin/wine" --version; echo "exit $?"
    echo "== T5 a new prefix: wine cmd /c exit, under strace"
    strace -f -s 200 -o "$OUT/strace-cmd.txt" env -i DISPLAY=$DISPLAY $WENV timeout 600 "$W64/bin/wine" cmd /c exit
    echo "exit $?"
    crash_context "$OUT/strace-cmd.txt"
    echo "== processes"; grep -E "execve\(" "$OUT/strace-cmd.txt" | cut -c1-220 | head -40
} >> "$OUT/diag.txt" 2>&1
gzip -9 "$OUT/strace-cmd.txt"
# run 13: Wine re-executes itself through lib/wine/aarch64-unix/wine-preloader, which maps wine
# and the rootfs's ld.so; the first malloc then faults (SIGBUS, misaligned). Without it?
PRELOADER="$W64/lib/wine/aarch64-unix/wine-preloader"
starts() { env -i DISPLAY=$DISPLAY $WENV timeout 300 "$W64/bin/wine" cmd /c exit > "$OUT/start-$1.log" 2>&1; local st=$?; log "start ($1): exit $st"; return $st; }
if starts with-preloader; then
    PRELOAD_MODE=with
else
    rm -rf "$PFX"; env -i $WENV "$W64/bin/wineserver" -k > /dev/null 2>&1
    mv "$PRELOADER" "$PRELOADER.off"
    if starts without-preloader; then
        PRELOAD_MODE=without
    else
        {
            echo "== without the preloader, strace"
            strace -f -s 200 -o "$OUT/strace-nopre.txt" env -i DISPLAY=$DISPLAY $WENV timeout 300 "$W64/bin/wine" cmd /c exit
            echo "exit $?"; crash_context "$OUT/strace-nopre.txt"
            tail -40 "$OUT/start-without-preloader.log"
        } >> "$OUT/diag.txt" 2>&1
        gzip -9 "$OUT/strace-nopre.txt"
        log "the patched Wine does not start; see diag.txt"
        tail -200 "$OUT/diag.txt" >> "$SUMMARY"
        exit 0
    fi
fi
log "preloader: $PRELOAD_MODE"
log "the patched Wine starts"

# --- what Wine's Unix side would load from the rootfs ----------------------------------------
{
    echo "== missing libraries (rootfs only) =="
    for so in "$W64"/lib/wine/aarch64-unix/*.so "$W64"/bin/wine "$W64"/bin/wineserver; do
        miss=$(LD_LIBRARY_PATH="$LIBS" "$INTERP" --list "$so" 2>&1 | grep "not found" | awk '{print $1}' | tr '\n' ' ')
        [ -n "$miss" ] && echo "$(basename "$so"): $miss"
    done
    echo "== dlopen'd ones: =="
    for lib in libX11.so.6 libXext.so.6 libXrender.so.1 libXrandr.so.2 libXi.so.6 libXcursor.so.1 libXcomposite.so.1 \
               libXfixes.so.3 libXinerama.so.1 libXxf86vm.so.1 libxkbcommon.so.0 libfreetype.so.6 libfontconfig.so.1 \
               libgnutls.so.30 libvulkan.so.1 libGL.so.1 libEGL.so.1 libpulse.so.0 libasound.so.2 libdbus-1.so.3 \
               libunwind.so.8 libcups.so.2 libgstreamer-1.0.so.0 libSDL2-2.0.so.0 libudev.so.1 libusb-1.0.so.0 libpcap.so.1; do
        f=$(ls "$LIBS/$lib" 2>/dev/null)
        echo "$lib: ${f:+present}${f:-MISSING}"
    done
} >> "$OUT/libraries.txt" 2>&1
cat "$OUT/libraries.txt" >> "$SUMMARY"

# --- a prefix, as the app would make it ----------------------------------------------------------
t0=$(date +%s)
env -i DISPLAY=$DISPLAY $WENV timeout 900 "$W64/bin/wine" wineboot -i > "$OUT/wineboot.log" 2>&1
log "wineboot exit $? after $(( $(date +%s) - t0 )) s; system32: $(ls "$PFX/drive_c/windows/system32" 2>/dev/null | wc -l) files"
env -i $WENV timeout 120 "$W64/bin/wineserver" -w
tail -30 "$OUT/wineboot.log" >> "$SUMMARY"
env -i DISPLAY=$DISPLAY $WENV "$W64/bin/wine" reg query 'HKLM\Software\Microsoft\Wow64' /s >> "$SUMMARY" 2>&1

# --- the programs (as in perf_lab.sh) --------------------------------------------------------
B="$LAB/bench"; mkdir -p "$B"
fetch() { [ -s "$2" ] || curl -sSL --retry 3 -o "$2" "$1" || log "download failed: $1"; }
SEVEN=$(gh_latest ip7z/7zip | jq -r '.assets[] | select(.name | test("extra\\.7z$")) | .browser_download_url' | head -1)
fetch "$SEVEN" "$B/7z-extra.7z"; mkdir -p "$B/7z" && 7z x -y -o"$B/7z" "$B/7z-extra.7z" > /dev/null
NODEV=$(curl -sSL https://nodejs.org/dist/latest-v22.x/SHASUMS256.txt | grep -o 'node-v[0-9.]*-win-x64.zip' | head -1 | sed 's/-win-x64.zip//')
for a in x64 arm64; do
    fetch "https://nodejs.org/dist/latest-v22.x/$NODEV-win-$a.zip" "$B/node-$a.zip"
    [ -d "$B/node-$a" ] || { unzip -q -o "$B/node-$a.zip" -d "$B" && mv "$B/$NODEV-win-$a" "$B/node-$a"; }
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
mkdir -p "$PFX/drive_c" && ln -sfn "$B" "$PFX/drive_c/bench"

measure() {   # config, test, timeout, program args...
    local config="$1" test="$2" limit="$3"; shift 3
    local log="$OUT/$config-$test.log" t0 t1 status total
    t0=$(date +%s.%N)
    ( cd "$PFX/drive_c" && env -i DISPLAY=$DISPLAY $WENV timeout "$limit" "$W64/bin/wine" "$@" > "$log" 2>&1 )
    status=$?
    t1=$(date +%s.%N)
    local wall=$(python3 -c "print(round($t1 - $t0, 2))")
    total=$(grep -aE "^total " "$log" | tail -1 | awk '{print $2}')
    [ -z "$total" ] && total=$(grep -a "^Tot:" "$log" | tail -1 | awk '{print $(NF)}')
    result "$config" "$test-wall-s" "$wall"; [ -n "$total" ] && result "$config" "$test" "$total"
    log "$config $test: exit $status, wall ${wall}s, result ${total:-?}"
    [ "$status" -ne 0 ] && tail -15 "$log" | sed 's/^/    /' >> "$SUMMARY"
}
env -i $WENV "$W64/bin/wineserver" -p > /dev/null 2>&1 &
sleep 3
measure rootfs-arm64 start-cmd 120 cmd /c exit
measure rootfs-arm64 node 600 'C:\bench\node-arm64\node.exe' 'C:\bench\bench.js'
measure rootfs-arm64 7zip-1t 600 'C:\bench\7z\arm64\7za.exe' b 2 -mmt1 -md22
measure rootfs-x64 node 600 'C:\bench\node-x64\node.exe' 'C:\bench\bench.js'
measure rootfs-x64 7zip-1t 600 'C:\bench\7z\x64\7za.exe' b 2 -mmt1 -md22
measure rootfs-x86 7zip-1t 600 'C:\bench\7z\7za.exe' b 2 -mmt1 -md22

# --- windows ------------------------------------------------------------------------------------
# run 15: no Notepad window. The rootfs's libxcb may look for the X socket under the rootfs's
# /tmp (Winlator's X server listens at <rootfs>/tmp/.X11-unix/X0)
{
    echo "== X socket paths in the rootfs's libxcb / libX11"
    strings "$LIBS/libxcb.so.1" 2>/dev/null | grep -i "x11-unix" | head
    strings "$LIBS/libX11.so.6" 2>/dev/null | grep -i "x11-unix" | head
} >> "$OUT/diag.txt"
if [ ! -e "$ROOTFS/tmp/.X11-unix/X1" ]; then
    rm -rf "$ROOTFS/tmp/.X11-unix"; ln -s /tmp/.X11-unix "$ROOTFS/tmp/.X11-unix"
fi
GUIENV="$WENV"
GUIENV="${GUIENV/WINEDEBUG=-all,err+module/WINEDEBUG=fixme-all}"
window() {   # name, window title pattern, program...
    local name="$1" title="$2"; shift 2
    local t0=$(date +%s.%N)
    ( cd "$PFX/drive_c" && env -i DISPLAY=$DISPLAY $GUIENV "$W64/bin/wine" "$@" > "$OUT/$name.log" 2>&1 & )
    local found=no
    for i in $(seq 1 300); do xwininfo -root -tree 2>/dev/null | grep -qiE "$title" && { found=yes; break; }; sleep 0.2; done
    local secs=$(python3 -c "print(round($(date +%s.%N) - $t0, 2))")
    log "$name window: $found after $secs s"
    [ "$found" = yes ] && result "$WINCONF" "$name-window-s" "$secs"
    sleep 2
    import -window root -resize 50% -quality 70 "$OUT/$name.jpg" 2>/dev/null
    [ "$found" = no ] && tail -20 "$OUT/$name.log" | sed 's/^/    /' >> "$SUMMARY"
    env -i $WENV "$W64/bin/wineserver" -k > /dev/null 2>&1
    sleep 1
}
WINCONF=rootfs-arm64
window notepad "notepad|메모장" notepad
window notepad-again "notepad|메모장" notepad
# 7-Zip's File Manager, from its installers (which 7-Zip unpacks)
REL=$(gh_latest ip7z/7zip)
for a in arm64 x64 x86; do
    if [ $a = x86 ]; then pat='^7z[0-9]+[.]exe$'; else pat="^7z[0-9]+-$a[.]exe$"; fi
    url=$(echo "$REL" | jq -r --arg p "$pat" '.assets[] | select(.name | test($p)) | .browser_download_url' | head -1)
    fetch "$url" "$B/7z-setup-$a.exe"
    mkdir -p "$B/7zfm-$a" && 7z x -y -o"$B/7zfm-$a" "$B/7z-setup-$a.exe" > /dev/null
done
ls "$B"/7zfm-* >> "$OUT/diag.txt" 2>&1
window 7zfm-arm64 "7-zip" 'C:\bench\7zfm-arm64\7zFM.exe'
WINCONF=rootfs-x64
window 7zfm-x64 "7-zip" 'C:\bench\7zfm-x64\7zFM.exe'
WINCONF=rootfs-x86
window 7zfm-x86 "7-zip" 'C:\bench\7zfm-x86\7zFM.exe'

# run 16: windows were found but the root window screenshots were black; list them
( cd "$PFX/drive_c" && env -i DISPLAY=$DISPLAY $GUIENV "$W64/bin/wine" notepad > /dev/null 2>&1 & )
sleep 4
{ echo "== windows with Notepad open"; xwininfo -root -tree 2>&1 | grep -v "has no name" | head -40; } >> "$OUT/diag.txt"
xwd -root -silent 2>/dev/null | convert xwd:- -resize 50% "$OUT/notepad-xwd.png" 2>> "$OUT/diag.txt"
env -i $WENV "$W64/bin/wineserver" -k > /dev/null 2>&1; sleep 1

# --- a browser: Firefox for Windows on ARM and for x64, headless, a JavaScript page -----------
cat > "$B/ff.html" <<'HTMLEOF'
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
    ls "$B/firefox-$a" | head -5 >> "$OUT/diag.txt"
done
mkdir -p "$B/ffprofile"
cat > "$B/ffprofile/user.js" <<'JSEOF'
user_pref("security.sandbox.content.level", 0);
user_pref("security.sandbox.gpu.level", 0);
user_pref("security.sandbox.rdd.level", 0);
user_pref("security.sandbox.socket.process.level", 0);
user_pref("browser.shell.checkDefaultBrowser", false);
user_pref("browser.startup.homepage_override.mstone", "ignore");
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRunURL", "");
user_pref("layers.acceleration.disabled", true);
user_pref("gfx.direct2d.disabled", true);
JSEOF
browser() {   # config, arch, run name
    local config="$1" arch="$2" name="$3"
    rm -rf "$B/ffprofile-$arch"; mkdir -p "$B/ffprofile-$arch"; cp "$B/ffprofile/user.js" "$B/ffprofile-$arch/"
    local t0=$(date +%s.%N)
    ( cd "$PFX/drive_c" && env -i DISPLAY=$DISPLAY $GUIENV MOZ_HEADLESS=1 MOZ_DISABLE_CONTENT_SANDBOX=1 MOZ_DISABLE_GPU_SANDBOX=1 MOZ_DISABLE_RDD_SANDBOX=1 \
        MOZ_DISABLE_SOCKET_PROCESS_SANDBOX=1 MOZ_DISABLE_UTILITY_SANDBOX=1 MOZ_DISABLE_GMP_SANDBOX=1 \
        timeout 600 "$W64/bin/wine" "C:\\bench\\firefox-$arch\\firefox.exe" \
        --headless --no-remote -profile "C:\\bench\\ffprofile-$arch" --window-size=1100,700 --screenshot "C:\\bench\\$name.png" "file:///C:/bench/ff.html" > "$OUT/$name.log" 2>&1 )
    local st=$? secs=$(python3 -c "print(round($(date +%s.%N) - $t0, 2))")
    log "$config $name: exit $st after $secs s; screenshot: $(ls "$B/$name.png" 2>/dev/null || echo none)"
    result "$config" "$name-wall-s" "$secs"
    [ -f "$B/$name.png" ] && cp "$B/$name.png" "$OUT/"
    [ $st -ne 0 ] && tail -15 "$OUT/$name.log" | sed 's/^/    /' >> "$SUMMARY"
    env -i $WENV "$W64/bin/wineserver" -k > /dev/null 2>&1; sleep 1
}
browser rootfs-arm64 aarch64 firefox-arm64-first
browser rootfs-arm64 aarch64 firefox-arm64
browser rootfs-x64 x64 firefox-x64-first
browser rootfs-x64 x64 firefox-x64

# --- size: what an app package of this Wine would weigh ----------------------------------------
{
    echo "== sizes"
    du -sh "$W64" "$W64"/lib/wine/* "$W64/share" 2>/dev/null
    S="$LAB/strip-test"; rm -rf "$S"; mkdir -p "$S"; cp -a "$W64/lib" "$W64/bin" "$S/"
    find "$S" -name "*.so" -exec strip --strip-unneeded {} + 2>/dev/null
    for f in "$S"/bin/*; do strip --strip-unneeded "$f" 2>/dev/null; done
    sudo apt-get install -y -qq llvm > /dev/null 2>&1
    STRIP_PE=$(ls /usr/bin/llvm-strip* 2>/dev/null | head -1)
    echo "llvm-strip: $STRIP_PE"
    [ -n "$STRIP_PE" ] && find "$S/lib/wine" -name "*.dll" -o -name "*.exe" -o -name "*.drv" -o -name "*.sys" | while read -r f; do "$STRIP_PE" --strip-debug "$f" 2>/dev/null; done
    du -sh "$S" "$S"/lib/wine/*
    tar -C "$S" -cf - . | zstd -19 -T0 -q -o "$LAB/wine-arm64.tzst" && ls -la "$LAB/wine-arm64.tzst"
    echo "== hangover's dxvk"
    tar -tzf "$LAB/hangover/dxvk-v2.7.1.tar.gz" | head -30
} >> "$OUT/diag.txt" 2>&1
grep -A40 "== sizes" "$OUT/diag.txt" >> "$SUMMARY"

python3 - "$RESULTS" > "$OUT/table.txt" <<'EOF'
import csv, sys
rows = list(csv.reader(open(sys.argv[1]), delimiter="\t"))[1:]
for c, t, v in rows: print(f"{c:14s} {t:20s} {v}")
EOF
cat "$OUT/table.txt" >> "$SUMMARY"
log "done $(date -u)"
