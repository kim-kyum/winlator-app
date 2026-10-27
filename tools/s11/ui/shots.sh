#!/bin/bash
# Screenshots of S11 Workspace's Android screens in an emulator (x86_64 image; the app's ARM64
# code runs through Android's ARM translation). Run inside android-emulator-runner.
# Usage: shots.sh <apk> <out dir>
set -u
APK="$1"
OUT="$2"
PKG=com.winlatab
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$OUT"
LOG="$OUT/shots.log"
log() { echo "[ui] $*" | tee -a "$LOG"; }

n=0
shot() {   # name
    n=$((n + 1))
    sleep 2
    adb exec-out screencap -p > "$OUT/$(printf %02d $n)-$1.png"
    dump; cp "$OUT/ui.xml" "$OUT/$(printf %02d $n)-$1.xml" 2>/dev/null
    log "shot $1"
}
dump() {
    adb shell uiautomator dump /sdcard/ui.xml > /dev/null 2>&1
    adb pull /sdcard/ui.xml "$OUT/ui.xml" > /dev/null 2>&1
    # the emulator's launcher sometimes stops answering while the app unpacks: wait for it
    if grep -q "responding" "$OUT/ui.xml" 2>/dev/null; then
        local xy
        xy=$(python3 "$HERE/find.py" "$OUT/ui.xml" text "Wait")
        if [ -n "$xy" ]; then adb shell input tap $xy; log "dismissed a 'not responding' dialog"; sleep 3; fi
        adb shell uiautomator dump /sdcard/ui.xml > /dev/null 2>&1
        adb pull /sdcard/ui.xml "$OUT/ui.xml" > /dev/null 2>&1
    fi
}
find_xy() { dump; python3 "$HERE/find.py" "$OUT/ui.xml" "$1" "$2"; }
# taps the first view whose text, content-desc or resource-id matches (a leading ~ matches a part)
tap() {   # attribute value
    local xy
    xy=$(find_xy "$1" "$2")
    if [ -z "$xy" ]; then log "not found: $1=$2"; return 1; fi
    adb shell input tap $xy
    sleep 2
}
has() { find_xy "$1" "$2" > /dev/null; }
scroll() { adb shell input swipe 1280 1300 1280 600 400; sleep 2; }
scroll_up() { adb shell input swipe 1280 500 1280 1400 300; sleep 1; }
# scrolls down until a view shows (or gives up)
scroll_to() {   # attribute value [tries]
    for i in $(seq 1 "${3:-10}"); do has "$1" "$2" && return 0; scroll; done
    has "$1" "$2"
}
back() { adb shell input keyevent KEYCODE_BACK; sleep 2; }
in_front() { adb shell dumpsys window | grep -E "mCurrentFocus|mFocusedApp" | grep -q "$PKG"; }
crashes() {   # where the app went: its crash, if any
    log "app not in front ($1); its crash, if any:"
    adb logcat -d -b crash 2>/dev/null | tail -60 >> "$OUT/crash-$1.txt"
    adb logcat -d 2>/dev/null | grep -E "AndroidRuntime|FATAL|DEBUG +:|libc +:|$PKG" | tail -80 >> "$OUT/crash-$1.txt"
    head -30 "$OUT/crash-$1.txt" | sed 's/^/    /' | tee -a "$LOG"
}
launch() { adb shell am start -W -n $PKG/com.winlator.MainActivity >> "$LOG" 2>&1; sleep 4; }
ensure() { in_front || { crashes "${1:-unknown}"; launch; }; }
drawer_open() { has text "정보" && has text "입력 컨트롤"; }
# the drawer opens with the toolbar's button (an edge swipe is Android's back gesture)
drawer() {
    ensure drawer
    drawer_open && return 0
    tap content-desc "~위로" 2>/dev/null || tap content-desc "~Navigate" 2>/dev/null || { adb shell input tap 56 110; sleep 2; }
}
page() {   # drawer item text
    drawer
    tap text "$1" || return 1
    sleep 3
}

adb logcat -c
adb shell getprop ro.dalvik.vm.native.bridge | tee -a "$LOG"
adb shell wm size | tee -a "$LOG"
adb shell settings put secure navigation_mode 0 2>/dev/null   # three-button navigation
adb install -r -g "$APK" >> "$LOG" 2>&1 || { log "install failed"; exit 1; }
adb shell cmd locale set-app-locales $PKG --locales ko-KR >> "$LOG" 2>&1
adb shell appops set $PKG MANAGE_EXTERNAL_STORAGE allow >> "$LOG" 2>&1

launch
sleep 8
shot first-start
for t in "허용" "Allow" "ALLOW"; do tap text "$t" > /dev/null 2>&1 && break; done

# the rootfs is unpacked on the first start (a progress dialog); wait until it is gone
start=$(date +%s)
while [ $(( $(date +%s) - start )) -lt 1800 ]; do
    ensure install
    dump
    grep -q "설치하는 중\|Installing\|%" "$OUT/ui.xml" || break
    [ $(( ($(date +%s) - start) / 60 )) -eq 3 ] && [ ! -f "$OUT/.mid" ] && { shot installing; touch "$OUT/.mid"; }
    sleep 15
done
log "system files installed after $(( $(date +%s) - start )) s"
sleep 5
shot containers-empty

drawer; shot drawer
in_front || crashes drawer
page "설정" && { shot settings; scroll; shot settings-2; scroll; shot settings-3; scroll; shot settings-4; }
page "입력 컨트롤" && shot input-controls
page "정보" && { shot about; back; }

# a new container: the settings pages, then create it
page "컨테이너"
tap content-desc "추가" || tap resource-id "$PKG:id/menu_item_add" || adb shell input tap 2510 110
sleep 6
if in_front; then
    shot container-new
    for i in 1 2 3; do scroll; shot container-new-$i; done
    if scroll_to resource-id "$PKG:id/BTConfirm" 8 && tap resource-id "$PKG:id/BTConfirm"; then
        start=$(date +%s)
        while [ $(( $(date +%s) - start )) -lt 900 ]; do
            in_front || { crashes create; break; }
            has resource-id "$PKG:id/BTRun" && break
            sleep 10
        done
        log "container ready after $(( $(date +%s) - start )) s"
    fi
else
    crashes container-new
    launch
fi
ensure list
shot containers-one
if tap resource-id "$PKG:id/BTMenu"; then
    shot container-menu
    if tap text "삭제"; then shot dialog-remove; tap text "취소" || back; else back; fi
fi
if tap resource-id "$PKG:id/BTMenu" && tap text "편집"; then
    shot container-edit; scroll; shot container-edit-2; back
fi

# start it: the loading screen, then the running container's drawer
ensure run
if tap resource-id "$PKG:id/BTRun"; then
    sleep 1; shot loading
    sleep 20; shot running
    back; sleep 2; shot running-drawer
    tap text "나가기" || back
    sleep 5
fi
ensure after-run

# the light theme: Settings > Light, then the confirm button (the app restarts)
if page "설정"; then
    if scroll_to resource-id "$PKG:id/RBLight" 10; then
        tap resource-id "$PKG:id/RBLight"
        tap resource-id "$PKG:id/BTConfirm"
        sleep 10
    else
        log "no light theme button"
    fi
    ensure light
    page "컨테이너" && shot light-containers
    drawer; shot light-drawer
    tap resource-id "$PKG:id/BTMenu" && { shot light-container-menu; back; }
    if tap resource-id "$PKG:id/BTMenu" && tap text "편집"; then shot light-container-edit; back; fi
    page "설정" && { shot light-settings; scroll; shot light-settings-2; }
    page "정보" && { shot light-about; back; }
fi
# the ARM64 Wine: Settings > Download (about 124 MB), unpacked, its version asked
if page "설정" && scroll_to resource-id "$PKG:id/BTArm64Wine" 14; then
    shot arm64-wine-row
    tap resource-id "$PKG:id/BTArm64Wine"
    start=$(date +%s)
    while [ $(( $(date +%s) - start )) -lt 900 ]; do
        sleep 10
        in_front || { crashes arm64-wine; break; }
        has text "Wine 설치" && break
        [ $(( $(date +%s) - start )) -gt 60 ] && [ ! -f "$OUT/.dl" ] && { shot arm64-wine-downloading; touch "$OUT/.dl"; }
    done
    log "ARM64 Wine: $(( $(date +%s) - start )) s"
    shot arm64-wine-install
    if has text "Wine 설치" && tap text "확인"; then
        sleep 40; shot arm64-wine-prefix
        sleep 60; shot arm64-wine-prefix-2
    fi
fi
adb logcat -d > "$OUT/logcat.txt" 2>/dev/null
gzip -9 "$OUT/logcat.txt"
log "done"
