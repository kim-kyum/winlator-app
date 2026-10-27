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
    log "shot $1"
}
dump() {
    adb shell uiautomator dump /sdcard/ui.xml > /dev/null 2>&1
    adb pull /sdcard/ui.xml "$OUT/ui.xml" > /dev/null 2>&1
}
# taps the first view whose text, content-desc or resource-id matches (a leading ~ matches a part)
tap() {   # attribute value
    local xy
    dump
    xy=$(python3 "$HERE/find.py" "$OUT/ui.xml" "$1" "$2")
    if [ -z "$xy" ]; then log "not found: $1=$2"; return 1; fi
    adb shell input tap $xy
    sleep 2
}
has() { dump; python3 "$HERE/find.py" "$OUT/ui.xml" "$1" "$2" > /dev/null; }
drawer() { adb shell input swipe 2 900 700 900 250; sleep 2; }
scroll() { adb shell input swipe 1280 1300 1280 450 400; sleep 2; }
back() { adb shell input keyevent KEYCODE_BACK; sleep 2; }
first() { for v in "$@"; do tap text "$v" && return 0; done; return 1; }

adb shell getprop ro.product.cpu.abilist | tee -a "$LOG"
adb shell getprop ro.dalvik.vm.native.bridge | tee -a "$LOG"
adb shell wm size | tee -a "$LOG"
adb install -r -g "$APK" >> "$LOG" 2>&1 || { log "install failed"; exit 1; }
adb shell cmd locale set-app-locales $PKG --locales ko-KR >> "$LOG" 2>&1
adb shell appops set $PKG MANAGE_EXTERNAL_STORAGE allow >> "$LOG" 2>&1

adb shell am start -W -n $PKG/com.winlator.MainActivity >> "$LOG" 2>&1
sleep 15
shot first-start
# the system's storage permission dialog, if it shows
for t in "허용" "Allow" "ALLOW"; do tap text "$t" > /dev/null 2>&1 && break; done

# the rootfs is unpacked on the first start (a progress dialog); wait for the container list
start=$(date +%s)
installing_shot=0
while [ $(( $(date +%s) - start )) -lt 1500 ]; do
    if has resource-id "$PKG:id/RecyclerView" || has resource-id "$PKG:id/TVEmptyText"; then
        dump
        if ! grep -q "ProgressBar\|progress" "$OUT/ui.xml" || [ $(( $(date +%s) - start )) -gt 1200 ]; then break; fi
    fi
    if [ $installing_shot -eq 0 ] && [ $(( $(date +%s) - start )) -gt 30 ]; then shot installing; installing_shot=1; fi
    sleep 15
done
log "first start settled after $(( $(date +%s) - start )) s"
sleep 5
shot containers-empty

drawer; shot drawer
first "설정" "Settings" && { shot settings; scroll; shot settings-2; scroll; shot settings-3; }
drawer; first "입력 컨트롤" "Input Controls" "Input controls" && shot input-controls
drawer; first "바로 가기" "Shortcuts" && shot shortcuts
drawer; first "정보" "About" && { shot about; back; }
drawer; first "컨테이너" "Containers"
sleep 2
tap content-desc "추가" || tap content-desc "Add" || tap resource-id "$PKG:id/menu_item_add"
sleep 4
shot container-new
for i in 1 2 3 4 5; do scroll; shot container-new-$i; done
# create it, then the list with one container and its menu
tap resource-id "$PKG:id/BTConfirm" && {
    start=$(date +%s)
    while [ $(( $(date +%s) - start )) -lt 600 ]; do
        has resource-id "$PKG:id/BTRun" && break
        sleep 10
    done
    shot containers-one
    tap resource-id "$PKG:id/BTMenu" && { shot container-menu; back; }
}

# the light theme: Settings > theme > Light, confirm (the app restarts)
drawer; first "설정" "Settings" && {
    first "밝게" "Light" && { scroll; scroll; scroll; first "확인" "OK" "Confirm" "저장"; sleep 8; shot light-after-save; }
}
drawer; first "컨테이너" "Containers" && shot light-containers
tap content-desc "추가" || tap content-desc "Add"
sleep 4; shot light-container-new; scroll; shot light-container-new-2
log "done"
