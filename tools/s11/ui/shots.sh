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
in_front() { adb shell dumpsys window | grep -E "mCurrentFocus|mFocusedApp" | grep -q "$PKG"; }
launch() { adb shell am start -W -n $PKG/com.winlator.MainActivity >> "$LOG" 2>&1; sleep 4; }
ensure() { in_front || { log "app not in front; starting it again"; launch; }; }
# the drawer opens with the toolbar's button (an edge swipe is Android's back gesture)
drawer() {
    ensure
    tap content-desc "~위로" 2>/dev/null || tap content-desc "~Navigate" 2>/dev/null || adb shell input tap 56 110
    sleep 2
}
page() {   # drawer item text
    drawer
    tap text "$1" || return 1
    sleep 3
}
scroll() { adb shell input swipe 1280 1350 1280 500 500; sleep 2; }
back() { adb shell input keyevent KEYCODE_BACK; sleep 2; }

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
    ensure
    dump
    grep -q "설치하는 중\|Installing\|%" "$OUT/ui.xml" || break
    [ $(( ($(date +%s) - start) / 60 )) -eq 3 ] && [ ! -f "$OUT/.mid" ] && { shot installing; touch "$OUT/.mid"; }
    sleep 15
done
log "system files installed after $(( $(date +%s) - start )) s"
sleep 5
shot containers-empty

drawer; shot drawer; back
page "설정" && { shot settings; scroll; shot settings-2; scroll; shot settings-3; scroll; shot settings-4; }
page "입력 컨트롤" && shot input-controls
page "바로 가기" && shot shortcuts
page "정보" && { shot about; back; }
page "컨테이너"
tap content-desc "추가" || tap resource-id "$PKG:id/menu_item_add" || adb shell input tap 2510 110
sleep 5
shot container-new
for i in 1 2 3 4 5 6; do scroll; shot container-new-$i; done
# create it, then the list with one container and its menu
if tap resource-id "$PKG:id/BTConfirm"; then
    start=$(date +%s)
    while [ $(( $(date +%s) - start )) -lt 900 ]; do
        ensure
        has resource-id "$PKG:id/BTRun" && break
        sleep 10
    done
    log "container ready after $(( $(date +%s) - start )) s"
    shot containers-one
    tap resource-id "$PKG:id/BTMenu" && { shot container-menu; back; }
fi

# the light theme: Settings > Light, then the confirm button at the bottom (the app restarts)
if page "설정"; then
    tap resource-id "$PKG:id/RBLight" || tap text "밝게"
    tap resource-id "$PKG:id/BTConfirm"
    sleep 10
    ensure
    page "컨테이너" && shot light-containers
    tap resource-id "$PKG:id/BTMenu" && { shot light-container-menu; back; }
    tap content-desc "추가" || adb shell input tap 2510 110
    sleep 5; shot light-container-new; scroll; shot light-container-new-2
    page "설정" && shot light-settings
fi
log "done"
