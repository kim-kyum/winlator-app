#!/bin/bash
# S11 lab: ImageJ (the user's research tools are Java: ImageJ, AstroImageJ, Tracker), the way
# the app would run it.
#  - today's containers: x64 JDK under Winlator's Wine 10.10 and Box64 0.4.4
#  - the ARM64 Wine: the ARM64 JDK as it is, the x64 JDK via FEX
# ImageJ in batch mode runs an image macro that times itself; and the JVM's start-up.
# Usage: java_lab.sh <repo> <out dir>
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
for p in xvfb zstd cmake build-essential git unzip curl python3; do
    sudo apt-get install -y -qq "$p" > /dev/null 2>&1 || log "package missing: $p"
done
fetch() { [ -s "$2" ] || curl -sSL --retry 3 -o "$2" "$1" || log "download failed: $1"; }

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

# --- today's containers --------------------------------------------------------------------------
BOX64=""
git clone -q --depth 1 -b v0.4.4 https://github.com/ptitSeb/box64.git "$LAB/box64-src" 2>/dev/null
cmake -S "$LAB/box64-src" -B "$LAB/box64-build" -DARM_DYNAREC=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$OUT/box64-cmake.log" 2>&1
make -C "$LAB/box64-build" -j"$(nproc)" > "$OUT/box64-build.log" 2>&1 && BOX64="$LAB/box64-build/box64"
XPREFIX="$XHOME/.wine"
PREFIX_CACHE="${PREFIX_CACHE:-$HOME/lab-cache/prefix.tar}"
[ -f "$PREFIX_CACHE" ] && { rm -rf "$XPREFIX"; tar -xf "$PREFIX_CACHE" -C "$XHOME"; log "Winlator prefix from the cache"; }
cp "$REPO/app/src/main/assets/box64/default.box64rc" "$LAB/config.box64rc"
BOXENV="BOX64_NOBANNER=1 BOX64_DYNAREC=1 BOX64_RCFILE=$LAB/config.box64rc \
BOX64_DYNAREC_SAFEFLAGS=1 BOX64_DYNAREC_FASTNAN=1 BOX64_DYNAREC_FASTROUND=1 BOX64_DYNAREC_X87DOUBLE=0 \
BOX64_DYNAREC_BIGBLOCK=3 BOX64_DYNAREC_STRONGMEM=0 BOX64_DYNAREC_FORWARD=512 BOX64_DYNAREC_CALLRET=1 \
BOX64_DYNAREC_WAIT=1 BOX64_DYNAREC_NATIVEFLAGS=1 BOX64_DYNAREC_WEAKBARRIER=2 \
HOME=$XHOME USER=xuser TMPDIR=$ROOTFS/tmp WINEPREFIX=$XPREFIX WINEDEBUG=-all \
BOX64_LD_LIBRARY_PATH=$LAB/rootfs/usr/lib/x86_64-linux-gnu:$LAB/box64-src/x64lib \
WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d"

# --- the ARM64 Wine (the release the app downloads) ------------------------------------------------
fetch "https://github.com/kim-kyum/winlator-app/releases/download/s11-wine-arm64-11.16/wine-11.16-arm64.tzst" "$LAB/wine-arm64.tzst"
rm -rf "$LAB/rootfs/opt/installed-wine/wine-11.16-arm64"
zstd -dc "$LAB/wine-arm64.tzst" | tar -x -C "$LAB/rootfs/opt/installed-wine"
W="$ROOTFS/opt/installed-wine/wine-11.16-arm64"
APREFIX="$XHOME/.wine-arm64"
AENV="HOME=$XHOME USER=xuser TMPDIR=$ROOTFS/tmp WINEPREFIX=$APREFIX LD_LIBRARY_PATH=$LIBS LD_PRELOAD=$W/lib/libs11tmp.so \
WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d"
env -i DISPLAY=$DISPLAY $AENV timeout 600 "$W/bin/wine" wineboot -i > "$OUT/arm64-wineboot.log" 2>&1
env -i $AENV timeout 60 "$W/bin/wineserver" -w

# --- ImageJ and the JDKs ---------------------------------------------------------------------------
B="$LAB/bench"; mkdir -p "$B"
for a in x64 aarch64; do
    fetch "https://aka.ms/download-jdk/microsoft-jdk-21-windows-$a.zip" "$B/jdk-$a.zip"
    [ -d "$B/jdk-$a" ] || { mkdir -p "$B/jdk-$a.tmp" && unzip -q -o "$B/jdk-$a.zip" -d "$B/jdk-$a.tmp" && mv "$B/jdk-$a.tmp"/* "$B/jdk-$a"; }
done
fetch "https://imagej.net/ij/download/zips/ij154.zip" "$B/ij.zip"
[ -d "$B/ImageJ" ] || unzip -q -o "$B/ij.zip" -d "$B"
ls "$B" "$B/ImageJ" >> "$SUMMARY" 2>&1
cat > "$B/bench.ijm" <<'EOF2'
t0 = getTime();
newImage("t", "32-bit random", 2048, 2048, 1);
t = getTime(); run("Gaussian Blur...", "sigma=4"); print("blur " + (getTime() - t));
t = getTime(); run("FFT"); print("fft " + (getTime() - t));
close();
selectImage("t");
t = getTime(); for (i = 0; i < 3; i++) run("Median...", "radius=3"); print("median " + (getTime() - t));
t = getTime(); run("Find Edges"); run("Enhance Contrast...", "saturated=0.35"); print("edges " + (getTime() - t));
t = getTime(); s = 0; for (i = 0; i < 2000000; i++) s += sin(i) * cos(i); print("macro-loop " + (getTime() - t));
print("total " + (getTime() - t0));
EOF2
mkdir -p "$APREFIX/drive_c" "$XPREFIX/drive_c"
ln -sfn "$B" "$APREFIX/drive_c/bench"; ln -sfn "$B" "$XPREFIX/drive_c/bench"

run() {   # config, jdk, launcher...
    local config="$1" jdk="$2"; shift 2
    local java="C:\\bench\\jdk-$jdk\\bin\\java.exe" t0 secs total
    t0=$(date +%s.%N)
    ( cd "$B" && timeout 300 "$@" "$java" -version > "$OUT/$config-version.log" 2>&1 )
    secs=$(python3 -c "print(round($(date +%s.%N) - $t0, 2))")
    log "$config java -version: exit $? after $secs s"
    result "$config" "java-start-s" "$secs"
    t0=$(date +%s.%N)
    ( cd "$B/ImageJ" && timeout 600 "$@" "$java" -Xmx2g -Djava.awt.headless=true -jar "C:\\bench\\ImageJ\\ij.jar" -batch "C:\\bench\\bench.ijm" > "$OUT/$config-imagej.log" 2>&1 )
    secs=$(python3 -c "print(round($(date +%s.%N) - $t0, 2))")
    total=$(grep -aE "^total " "$OUT/$config-imagej.log" | tail -1 | awk '{print $2}')
    log "$config ImageJ: wall $secs s, macro ${total:-?} ms"
    result "$config" "imagej-wall-s" "$secs"; [ -n "$total" ] && result "$config" "imagej-ms" "$total"
    grep -aE "^(blur|fft|median|edges|macro-loop) " "$OUT/$config-imagej.log" | sed "s/^/    /" >> "$SUMMARY"
    [ -z "$total" ] && tail -12 "$OUT/$config-imagej.log" | sed 's/^/    /' >> "$SUMMARY"
}
if [ -n "$BOX64" ] && [ -d "$XPREFIX" ]; then
    env $BOXENV "$BOX64" "$ROOTFS/opt/wine/bin/wineserver" -p > /dev/null 2>&1 &
    sleep 5
    run box64-x64 x64 env -i DISPLAY=$DISPLAY $BOXENV "$BOX64" "$ROOTFS/opt/wine/bin/wine"
    env $BOXENV "$BOX64" "$ROOTFS/opt/wine/bin/wineserver" -k > /dev/null 2>&1; sleep 3
fi
env -i $AENV "$W/bin/wineserver" -p > /dev/null 2>&1 &
sleep 3
run arm64wine-arm64 aarch64 env -i DISPLAY=$DISPLAY $AENV "$W/bin/wine"
run arm64wine-x64 x64 env -i DISPLAY=$DISPLAY $AENV "$W/bin/wine"
env -i $AENV "$W/bin/wineserver" -k > /dev/null 2>&1

python3 - "$RESULTS" > "$OUT/table.txt" <<'EOF2'
import csv, sys
rows = list(csv.reader(open(sys.argv[1]), delimiter="\t"))[1:]
for c, t, v in rows: print(f"{c:18s} {t:16s} {v}")
EOF2
cat "$OUT/table.txt" >> "$SUMMARY"
log "done $(date -u)"
