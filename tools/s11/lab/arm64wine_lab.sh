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
for p in xvfb zstd unzip 7zip curl jq patchelf imagemagick x11-utils file; do
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
for b in "$W64"/bin/*; do
    if file "$b" | grep -q "ELF.*aarch64"; then
        patchelf --set-interpreter "$INTERP" "$b" 2>> "$SUMMARY" && log "interpreter set: $(basename "$b")"
    fi
done

# --- what Wine's Unix side would load from the rootfs ----------------------------------------
LIBS="$ROOTFS/usr/lib"
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

Xvfb :1 -screen 0 1920x1080x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
export DISPLAY=:1
sleep 3

# --- a prefix, as the app would make it ----------------------------------------------------------
HOMEDIR="$ROOTFS/home/xuser"
PFX="$HOMEDIR/.wine-arm64"
WENV="HOME=$HOMEDIR USER=xuser TMPDIR=$ROOTFS/tmp WINEPREFIX=$PFX LD_LIBRARY_PATH=$LIBS PATH=$W64/bin:$ROOTFS/usr/bin:/usr/bin:/bin \
WINEDEBUG=-all,err+module WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d"
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

# Notepad's window
t0=$(date +%s.%N)
( cd "$PFX/drive_c" && env -i DISPLAY=$DISPLAY $WENV "$W64/bin/wine" notepad > "$OUT/notepad.log" 2>&1 & )
for i in $(seq 1 300); do xwininfo -root -tree 2>/dev/null | grep -qi notepad && break; sleep 0.2; done
log "notepad window after $(python3 -c "print(round($(date +%s.%N) - $t0, 2))") s"
import -window root -resize 50% -quality 70 "$OUT/notepad.jpg" 2>/dev/null
env -i $WENV "$W64/bin/wineserver" -k

python3 - "$RESULTS" > "$OUT/table.txt" <<'EOF'
import csv, sys
rows = list(csv.reader(open(sys.argv[1]), delimiter="\t"))[1:]
for c, t, v in rows: print(f"{c:14s} {t:20s} {v}")
EOF
cat "$OUT/table.txt" >> "$SUMMARY"
log "done $(date -u)"
