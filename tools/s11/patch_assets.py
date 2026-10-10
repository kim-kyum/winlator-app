#!/usr/bin/env python3
"""Rewrite the app's data path inside Winlator's bundled .tzst archives.

Winlator's rootfs, Box64, graphics drivers and patches have the install path
"/data/data/com.winlator" compiled into binaries (ELF interpreter, RPATH),
config files and symlink targets. Winlator S11 installs next to the official
app under a different application id, so that path must change everywhere.

The new id has the same length as the old one, so every replacement keeps
byte offsets intact and binaries stay valid. Tar headers are rebuilt with
Python's tarfile so their checksums stay correct.

Usage: patch_assets.py <assets_dir> [--app-id com.winlatab] [--check]
"""
import argparse
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile

OLD_ID = "com.winlator"


def zstd_bin():
    exe = os.environ.get("ZSTD") or shutil.which("zstd")
    if not exe:
        sys.exit("zstd not found (install it or set ZSTD=/path/to/zstd)")
    return exe


def decompress(path):
    return subprocess.run([zstd_bin(), "-dc", "-q", path], check=True,
                          stdout=subprocess.PIPE).stdout


def compress(data, path, level):
    # Level 22 matches the upstream archives (128 MiB window), which Winlator's
    # zstd-jni decoder already accepts; lower levels make the APK ~12 MB bigger.
    tmp = path + ".tmp"
    cmd = [zstd_bin(), f"-{level}", "-T0", "-q", "-f", "-o", tmp]
    if level > 19:
        cmd.insert(1, "--ultra")
    subprocess.run(cmd, input=data, check=True)
    os.replace(tmp, path)


def rewrite_tar(raw, old, new):
    """Return (new_tar_bytes, files_changed, replacements)."""
    src = tarfile.open(fileobj=io.BytesIO(raw), mode="r:")
    out_buf = io.BytesIO()
    dst = tarfile.open(fileobj=out_buf, mode="w:", format=tarfile.GNU_FORMAT)
    changed = hits = 0
    for ti in src.getmembers():
        n = 0
        if ti.issym() or ti.islnk():
            if old.decode() in ti.linkname:
                n += ti.linkname.count(old.decode())
                ti.linkname = ti.linkname.replace(old.decode(), new.decode())
            dst.addfile(ti)
        elif ti.isreg():
            data = src.extractfile(ti).read()
            if old in data:
                n += data.count(old)
                data = data.replace(old, new)
            dst.addfile(ti, io.BytesIO(data))
        else:
            dst.addfile(ti)
        if n:
            changed += 1
            hits += n
    dst.close()
    return out_buf.getvalue(), changed, hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("assets_dir")
    ap.add_argument("--app-id", default=os.environ.get("S11_APP_ID", "com.winlatab"))
    ap.add_argument("--level", type=int, default=22)
    ap.add_argument("--check", action="store_true", help="report only, write nothing")
    args = ap.parse_args()

    if len(args.app_id) != len(OLD_ID):
        sys.exit(f"app id must be {len(OLD_ID)} characters long (got {args.app_id!r})")
    old = f"/data/data/{OLD_ID}".encode()
    new = f"/data/data/{args.app_id}".encode()

    total_files = total_hits = 0
    for root, _, names in os.walk(args.assets_dir):
        for name in sorted(names):
            if not name.endswith(".tzst"):
                continue
            path = os.path.join(root, name)
            raw = decompress(path)
            if old not in raw:
                continue
            data, changed, hits = rewrite_tar(raw, old, new)
            if old in data:
                sys.exit(f"{path}: old path still present after rewrite")
            rel = os.path.relpath(path, args.assets_dir)
            print(f"{rel}: {changed} files, {hits} replacements")
            total_files += changed
            total_hits += hits
            if not args.check:
                compress(data, path, args.level)
    print(f"done: {total_files} files, {total_hits} replacements -> {new.decode()}")


if __name__ == "__main__":
    main()
