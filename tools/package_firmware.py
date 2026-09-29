#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package a finished PlatformIO build into release files.

Run after `pio run`. Produces, in the output directory:

  <name>-full.bin   bootloader + partition table + app in one image. Flash it
                    at 0x0 to set up a blank or unknown board in one step (the
                    web flasher, or `esptool.py write_flash 0x0 <file>`).
  <name>-app.bin    the application alone, for updating a board that already
                    runs this firmware (`esptool.py write_flash 0x10000 <file>`).
  SHA256SUMS        checksums of both.

Offsets and flash settings come from the build's own sdkconfig and the
partition table, so this stays correct if either changes. The app is checked
against the factory partition size; an image that would not fit fails here
instead of on the board.

Usage: package_firmware.py --env ENV --name NAME [--out DIR]
"""
import argparse
import csv
import hashlib
import os
import subprocess
import sys


def sdkconfig_value(path, key, default=None):
    with open(path) as f:
        for line in f:
            if line.startswith(key + "="):
                return line.split("=", 1)[1].strip().strip('"')
    if default is None:
        sys.exit(f"{path}: {key} not found")
    return default


def factory_partition(path):
    """(offset, size) of the factory app partition, as ints."""
    with open(path) as f:
        rows = [r for r in csv.reader(f) if r and not r[0].lstrip().startswith("#")]
    for r in rows:
        name, ptype, subtype = (c.strip() for c in r[:3])
        if ptype == "app" and subtype == "factory":
            return int(r[3].strip(), 0), int(r[4].strip(), 0)
    sys.exit(f"{path}: no factory app partition")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--env", required=True, help="PlatformIO environment name")
    ap.add_argument("--name", required=True, help="base name for the output files")
    ap.add_argument("--out", default="output")
    ap.add_argument("--summary", help="append a Markdown report to this file")
    args = ap.parse_args()

    build = os.path.join(".pio", "build", args.env)
    sdk = f"sdkconfig.{args.env}"
    boot_off = sdkconfig_value(sdk, "CONFIG_BOOTLOADER_OFFSET_IN_FLASH")
    ptab_off = sdkconfig_value(sdk, "CONFIG_PARTITION_TABLE_OFFSET")
    chip = sdkconfig_value(sdk, "CONFIG_IDF_TARGET")
    app_off, app_max = factory_partition("partitions.csv")

    app = os.path.join(build, "firmware.bin")
    size = os.path.getsize(app)
    pct = size * 100 // app_max
    print(f"app: {size // 1024} KB of {app_max // 1024} KB factory partition ({pct}%)")
    if size > app_max:
        sys.exit(f"::error::firmware.bin ({size} B) exceeds the factory partition ({app_max} B)")
    if pct >= 90:
        print(f"::warning::firmware uses {pct}% of the factory partition")

    os.makedirs(args.out, exist_ok=True)
    full = os.path.join(args.out, f"{args.name}-full.bin")
    app_out = os.path.join(args.out, f"{args.name}-app.bin")

    # "keep" leaves the flash mode/freq/size the bootloader was built with.
    subprocess.run([
        "pio", "pkg", "exec", "--package", "tool-esptoolpy", "--",
        "esptool.py", "--chip", chip, "merge_bin", "-o", full,
        "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep",
        boot_off, os.path.join(build, "bootloader.bin"),
        ptab_off, os.path.join(build, "partitions.bin"),
        hex(app_off), app,
    ], check=True)
    with open(app, "rb") as src, open(app_out, "wb") as dst:
        dst.write(src.read())

    sums = {os.path.basename(p): sha256(p) for p in (full, app_out)}
    with open(os.path.join(args.out, "SHA256SUMS"), "w") as f:
        for name, digest in sums.items():
            f.write(f"{digest}  {name}\n")

    report = (
        f"### {args.name}\n\n"
        f"App: **{size // 1024} KB** of {app_max // 1024} KB ({pct}%)\n\n"
        f"| File | Flash at | Use |\n|---|---|---|\n"
        f"| `{os.path.basename(full)}` | `0x0` | first install / full reflash |\n"
        f"| `{os.path.basename(app_out)}` | `{hex(app_off)}` | update the app only |\n"
    )
    print(report)
    if args.summary:
        with open(args.summary, "a") as f:
            f.write(report + "\n")


if __name__ == "__main__":
    main()
