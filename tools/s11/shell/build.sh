#!/bin/bash
# Builds s11shell.exe (x86-64 PE) with clang and lld, against the Windows headers and
# import libraries of a Wine build (wine-11.x from the Kron4ek/Wine-Builds releases works).
# Usage: build.sh <wine dir> <out.exe>
set -e
WINE="$1"; OUT="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
INC="-I$WINE/include/wine/windows -I$WINE/include/wine/msvcrt"
LIBS="$WINE/lib/wine/x86_64-windows"
"$WINE/bin/wrc" --nostdinc $INC -I"$HERE" -o "$TMP/s11shell.res" "$HERE/s11shell.rc"
clang --target=x86_64-w64-mingw32 $INC -D_WIN32_WINNT=0x0A00 -DUNICODE -D_UNICODE -O2 -Wall \
      -Wno-missing-braces -fno-stack-protector -mno-stack-arg-probe -c "$HERE/s11shell.c" -o "$TMP/s11shell.o"
clang --target=x86_64-w64-mingw32 -nostdlib -fuse-ld=lld -Wl,--entry=entry -Wl,--subsystem,windows \
      "$TMP/s11shell.o" "$TMP/s11shell.res" -L"$LIBS" \
      -luser32 -lgdi32 -lkernel32 -lshell32 -lole32 -lgdiplus -lcomctl32 -lws2_32 -ladvapi32 -lntdll -lmsvcrt \
      -o "$OUT"
rm -rf "$TMP"
echo "$OUT: $(stat -c %s "$OUT") bytes"
