#!/bin/bash
# Builds s11shell.exe (taskbar and start menu) and s11settings.exe (Settings) as x86-64 PE
# files with clang and lld, against the Windows headers and import libraries of a Wine build
# (wine-11.x from the Kron4ek/Wine-Builds releases works).
# Usage: build.sh <wine dir> <out dir>
set -e
WINE="$1"; OUT="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
INC="-I$WINE/include/wine/windows -I$WINE/include/wine/msvcrt"
LIBS="$WINE/lib/wine/x86_64-windows"
COMMON_LIBS="-luser32 -lgdi32 -lkernel32 -lshell32 -lole32 -lgdiplus -lcomctl32 -lws2_32 -ladvapi32 -lntdll -lmsvcrt"

build() {  # name, extra libraries
    local name="$1"; shift
    "$WINE/bin/wrc" --nostdinc $INC -I"$HERE" -o "$TMP/$name.res" "$HERE/$name.rc"
    clang --target=x86_64-w64-mingw32 $INC -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE -O2 -Wall \
          -Wno-missing-braces -fno-stack-protector -mno-stack-arg-probe -c "$HERE/$name.c" -o "$TMP/$name.o"
    clang --target=x86_64-w64-mingw32 -nostdlib -fuse-ld=lld -Wl,--entry=entry -Wl,--subsystem,windows -Wl,--no-insert-timestamp \
          "$TMP/$name.o" "$TMP/$name.res" -L"$LIBS" $COMMON_LIBS "$@" -o "$OUT/$name.exe"
    echo "$OUT/$name.exe: $(stat -c %s "$OUT/$name.exe") bytes"
}

build s11shell
build s11settings -lcomdlg32 -luxtheme
