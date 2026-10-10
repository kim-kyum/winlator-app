#!/usr/bin/env python3
"""Build the Winlator S11 visual style (s11.msstyles) from Wine's Aero theme.

Wine 11 ships aero.msstyles, a modern light theme (flat buttons, thin borders, crisp
per-DPI bitmaps). Winlator's Wine 10.10 only has the older "light" theme but its
uxtheme loads Aero fine. This script takes aero.msstyles from a Wine 11 build, renames
the theme and replaces the [SysMetrics] block with the Winlator S11 colours and fonts,
then relinks the resources into a resource-only DLL with lld-link.

The theme bitmaps and INI are Wine's (LGPL 2.1, see COPYING.LIB in Wine's source).

Usage: build_msstyles.py <aero.msstyles> <sysmetrics.ini> <out.msstyles>
Needs lld-link (LLVM) on PATH.
"""
import os
import re
import struct
import subprocess
import sys
import tempfile

DISPLAY_NAME = "Winlator S11"
TOOLTIP = "Winlator S11 visual style (based on Wine's Aero)"


def read_resources(data):
    """Returns [(type, name, lang, bytes)] from a PE file's resource directory."""
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[pe:pe + 4] == b"PE\0\0", "not a PE file"
    nsec, = struct.unpack_from("<H", data, pe + 6)
    opt_size, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    magic, = struct.unpack_from("<H", data, opt)
    dd = opt + (96 if magic == 0x10B else 112)
    rsrc_rva, _ = struct.unpack_from("<II", data, dd + 2 * 8)
    sections = []
    sec = opt + opt_size
    for _ in range(nsec):
        vsize, va, rsize, rptr = struct.unpack_from("<IIII", data, sec + 8)
        sections.append((va, max(vsize, rsize), rptr))
        sec += 40

    def off(rva):
        for va, size, ptr in sections:
            if va <= rva < va + size:
                return rva - va + ptr
        raise ValueError(hex(rva))

    base = off(rsrc_rva)

    def name_of(entry):
        if entry & 0x80000000:
            o = base + (entry & 0x7FFFFFFF)
            n, = struct.unpack_from("<H", data, o)
            return data[o + 2:o + 2 + 2 * n].decode("utf-16-le")
        return entry

    out = []

    def walk(o, path):
        named, ids = struct.unpack_from("<HH", data, o + 12)
        for i in range(named + ids):
            entry, child = struct.unpack_from("<II", data, o + 16 + 8 * i)
            p = path + [name_of(entry)]
            if child & 0x80000000:
                walk(base + (child & 0x7FFFFFFF), p)
            else:
                rva, size, _, _ = struct.unpack_from("<IIII", data, base + child)
                start = off(rva)
                out.append((p[0], p[1], p[2], data[start:start + size]))

    walk(base, [])
    return out


def res_id(value):
    if isinstance(value, int):
        return struct.pack("<HH", 0xFFFF, value)
    return (value + "\0").encode("utf-16-le")


def write_res(resources, path):
    """Writes a Win32 .res file (what rc.exe produces)."""
    def entry(rtype, name, lang, payload, flags=0x1030):
        header = res_id(rtype) + res_id(name)
        if len(header) % 4:
            header += b"\0" * (4 - len(header) % 4)
        header += struct.pack("<IHHII", 0, flags, lang, 0, 0)
        blob = struct.pack("<II", len(payload), 8 + len(header)) + header + payload
        if len(blob) % 4:
            blob += b"\0" * (4 - len(blob) % 4)
        return blob

    with open(path, "wb") as f:
        f.write(entry(0, 0, 0, b"", flags=0))  # the empty header every .res starts with
        for rtype, name, lang, payload in resources:
            f.write(entry(rtype, name, lang, payload))


def ini_text(payload):
    return payload.decode("utf-16-le")


def ini_bytes(text):
    return text.encode("utf-16-le")


def replace_section(text, section, body):
    """Replaces the body of [section] (up to the next section header)."""
    pattern = re.compile(r"(^\[" + re.escape(section) + r"\]\r?\n)(.*?)(?=^\[)", re.S | re.M)
    newline = "\r\n" if "\r\n" in text else "\n"
    body = newline.join(body.strip().splitlines()) + newline + newline
    new, count = pattern.subn(lambda m: m.group(1) + body, text, count=1)
    if count != 1:
        raise SystemExit(f"[{section}] not found")
    return new


def main():
    src, sysmetrics_path, out = sys.argv[1:4]
    resources = read_resources(open(src, "rb").read())
    sysmetrics = open(sysmetrics_path, encoding="utf-8").read()

    patched = []
    for rtype, name, lang, payload in resources:
        if rtype == "TEXTFILE" and name == "THEMES_INI":
            text = ini_text(payload)
            text = re.sub(r"(?m)^DisplayName\s*=.*$", "DisplayName = " + DISPLAY_NAME, text)
            text = re.sub(r"(?m)^ToolTip\s*=.*$", "ToolTip = " + TOOLTIP, text)
            payload = ini_bytes(text)
        elif rtype == "TEXTFILE" and name.endswith("_INI"):
            payload = ini_bytes(replace_section(ini_text(payload), "SysMetrics", sysmetrics))
        patched.append((rtype, name, lang, payload))

    with tempfile.TemporaryDirectory() as tmp:
        res = os.path.join(tmp, "theme.res")
        write_res(patched, res)
        subprocess.run(["lld-link", "/nologo", "/dll", "/noentry", "/nodefaultlib", "/machine:x64",
                        "/out:" + out, res], check=True)
    print(f"{out}: {os.path.getsize(out)} bytes, {len(patched)} resources")


if __name__ == "__main__":
    main()
