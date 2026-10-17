#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Wrap this port's inspected FD in a private, matching Android v2 boot image.

LK's stock gzip transport expands the FD before its direct ARM64 SEC entry.
There is no Linux kernel, userspace, BootShim or additional loader in this image.
This program only reads and writes files on the host.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

from avb_none import add_footer, verify
from own_image import AndroidV2, align, pack_newc
from probe_stock_ram_boot import STOCK_HASH
from verify_native_fd import FD_BASE, FD_SIZE, require_entry_probe, require_native_lab

EDK2_COMMIT = "b03a21a63e3bd001f52c527e5a57feddb53a690b"


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def sources():
    root = Path(__file__).resolve().parents[2]
    paths = [p for p in (root / "Platform/Xiaomi/mielPkg").rglob("*") if p.is_file()]
    paths += [root / name for name in ("Tools/Build/build_native.sh", "Tools/Bringup/native_boot.py",
        "Tools/Bringup/verify_native_fd.py", "Tools/Bringup/run_diagnostic_session.py")]
    return {p.relative_to(root).as_posix(): digest(p.read_bytes()) for p in sorted(paths)}


def inspect_mode(fd, mode):
    if mode == "shell":
        return require_native_lab(fd)
    if mode not in ("handoff", "mmu"):
        raise ValueError("Unknown native test mode")
    return require_entry_probe(fd, mode)


def inflate(kernel):
    if not kernel.startswith(b"\x1f\x8b\x08") or len(kernel) > FD_SIZE + 65536:
        raise ValueError("Expected one bounded gzip FD transport")
    decoder = zlib.decompressobj(31)
    try:
        fd = decoder.decompress(kernel, FD_SIZE + 1)
    except zlib.error as error:
        raise ValueError("Invalid FD gzip transport") from error
    if len(fd) != FD_SIZE or not decoder.eof or decoder.unused_data or decoder.unconsumed_tail:
        raise ValueError("Transport must expand to exactly one complete 8 MiB FD")
    return fd


def repack(stock, fd):
    original = AndroidV2(stock)
    if struct.unpack_from("<I", original.header, 12)[0] != FD_BASE:
        raise ValueError("Stock load address does not match the native FD")
    parts = dict(original.components, kernel=gzip.compress(fd, compresslevel=9, mtime=0),
                 ramdisk=gzip.compress(pack_newc([]), compresslevel=9, mtime=0))
    header = bytearray(original.header)
    struct.pack_into("<I", header, 8, len(parts["kernel"]))
    struct.pack_into("<I", header, 16, len(parts["ramdisk"]))
    out, checksum = bytearray(header), hashlib.sha1()
    for name in ("kernel", "ramdisk", "second", "recovery_dtbo", "dtb"):
        raw = parts[name]
        if name == "recovery_dtbo":
            struct.pack_into("<Q", out, 1636, len(out) if raw else 0)
        out.extend(raw)
        out.extend(bytes(align(len(out), original.page) - len(out)))
        checksum.update(raw)
        checksum.update(struct.pack("<I", len(raw)))
    out[576:608] = checksum.digest() + bytes(12)
    return bytes(out)


def validate(stock, image, manifest, mode):
    if len(stock) != 67108864 or digest(stock) != STOCK_HASH:
        raise ValueError("Matching stock restoration image required")
    if (manifest.get("purpose") != "miel-native-edk2-test" or manifest.get("mode") != mode or
            manifest.get("bootshim_used") is not False or manifest.get("linux_kernel_retained") is not False or
            manifest.get("kernel_transport") != "gzip" or manifest.get("edk2_commit") != EDK2_COMMIT or
            manifest.get("reset_request_after_seconds") != (90 if mode == "shell" else 10)):
        raise ValueError("Expected a reviewed native test manifest for the selected mode")
    if len(image) != len(stock) or digest(image) != manifest.get("image_sha256"):
        raise ValueError("Native Android image hash or geometry mismatch")
    fd = inflate(AndroidV2(image).components["kernel"])
    report = inspect_mode(fd, mode)
    if report["fd_sha256"] != manifest.get("native_fd_sha256"):
        raise ValueError("Native FD differs from the reviewed build")
    packed = repack(stock, fd)
    avb = verify(image)
    if image[:len(packed)] != packed or avb["image_size"] != len(packed):
        raise ValueError("Unexpected content in native image")
    if avb["properties"] != verify(stock, require_unsigned=False)["properties"]:
        raise ValueError("Stock AVB properties changed")
    if manifest.get("source_sha256") != sources():
        raise ValueError("Native platform sources changed since image preparation")
    root = Path(__file__).resolve().parents[2]
    commit = subprocess.check_output(["git", "-C", str(root / "Common/edk2"), "rev-parse", "HEAD"]).decode().strip()
    if commit != EDK2_COMMIT:
        raise ValueError("EDK2 checkout differs from the reviewed build")


def build(stock_path, fd_path, output, mode):
    if output.exists():
        raise ValueError("Use a fresh private output directory")
    stock, fd = stock_path.read_bytes(), fd_path.read_bytes()
    if len(stock) != 67108864 or digest(stock) != STOCK_HASH:
        raise ValueError("Unexpected stock boot image")
    report = inspect_mode(fd, mode)
    image = add_footer(repack(stock, fd), stock)
    manifest = {"purpose": "miel-native-edk2-test", "mode": mode, "image_sha256": digest(image),
        "image_size": len(image), "stock_sha256": STOCK_HASH, "native_fd_sha256": report["fd_sha256"],
        "edk2_commit": EDK2_COMMIT, "kernel_transport": "gzip", "bootshim_used": False,
        "linux_kernel_retained": False, "oem_ramdisk_retained": False, "avb_algorithm": "NONE",
        "oem_signature_valid": False, "reset_request_after_seconds": 90 if mode == "shell" else 10,
        "reset_return_path_verified": False, "source_sha256": sources(),
        "physical_execution_verified": False, "phone_operations_performed": False}
    validate(stock, image, manifest, mode)
    output.mkdir(parents=True)
    (output / "miel-native.img").write_bytes(image)
    (output / "build.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf8")
    print(json.dumps({"mode": mode, "size": len(image), "sha256": digest(image),
        "direct_sec_entry_inspected": True, "physical_execution_verified": False}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--fd", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--mode", choices=("handoff", "mmu", "shell"), required=True)
    args = parser.parse_args()
    build(args.stock, args.fd, args.out, args.mode)
