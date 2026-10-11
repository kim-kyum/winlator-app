#!/usr/bin/env python3
"""Pack tools/s11/rootfs-overlay into the app's assets.

Writes <assets>/s11/rootfs-overlay.tzst plus rootfs-overlay.version (a hash of the
archive). The app compares that version with a marker in the rootfs and extracts the
overlay again when it differs (com.winlator.s11.RootfsOverlay).

The tar is deterministic (sorted names, fixed owner and time), so the version only
changes when the overlay's content does.

A few files come from the app's assets instead of being kept twice in the repository
(FROM_ASSETS): the S11 pictures, which the Settings app (s11settings.exe) offers as
backgrounds.

Usage: build_overlay.py <overlay_dir> <assets_dir>
"""
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile


# (path in the rootfs, path in the assets)
FROM_ASSETS = [
    ("usr/share/s11/wallpapers/s11-dark.jpg", "wallpapers/wallpaper-s11/full.jpg"),
    ("usr/share/s11/wallpapers/s11-light.jpg", "wallpapers/wallpaper-s11-light/full.jpg"),
]


def add_entry(tar, path, arcname):
    info = tar.gettarinfo(path, arcname=arcname)
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mtime = 1700000000
    info.mode = 0o755 if info.isdir() else 0o644
    if info.isreg():
        with open(path, "rb") as f:
            tar.addfile(info, f)
    else:
        tar.addfile(info)


def build_tar(src, assets):
    buf = io.BytesIO()
    added = set()
    with tarfile.open(fileobj=buf, mode="w:", format=tarfile.GNU_FORMAT) as tar:
        for root, dirs, files in os.walk(src):
            dirs.sort()
            rel_root = os.path.relpath(root, src)
            for name in sorted(dirs) + sorted(files):
                path = os.path.join(root, name)
                arcname = os.path.normpath(os.path.join(rel_root, name))
                add_entry(tar, path, arcname)
                added.add(arcname)
        for arcname, asset in FROM_ASSETS:
            # the folders first: the app's extractor does not create them for files
            parts = arcname.split("/")
            for i in range(1, len(parts)):
                folder = "/".join(parts[:i])
                if folder not in added:
                    add_entry(tar, os.path.dirname(os.path.join(assets, asset)), folder)
                    added.add(folder)
            add_entry(tar, os.path.join(assets, asset), arcname)
            added.add(arcname)
    return buf.getvalue()


def main():
    src, assets = sys.argv[1], sys.argv[2]
    data = build_tar(src, assets)
    out_dir = os.path.join(assets, "s11")
    os.makedirs(out_dir, exist_ok=True)
    zstd = os.environ.get("ZSTD") or shutil.which("zstd") or sys.exit("zstd not found")
    out = os.path.join(out_dir, "rootfs-overlay.tzst")
    subprocess.run([zstd, "-19", "-q", "-f", "-o", out], input=data, check=True)
    version = hashlib.sha256(data).hexdigest()[:16]
    with open(os.path.join(out_dir, "rootfs-overlay.version"), "w") as f:
        f.write(version + "\n")
    print(f"overlay {os.path.getsize(out)} bytes, version {version}")


if __name__ == "__main__":
    main()
