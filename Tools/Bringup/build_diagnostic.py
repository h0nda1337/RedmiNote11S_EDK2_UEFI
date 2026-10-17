#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Build an offline diagnostic: stock drivers and this repository's init."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path

from own_image import AndroidV2, Entry, pack_newc, parse_newc
from probe_stock_ram_boot import STOCK_HASH
from avb_none import add_footer, verify
from init_elf import validate_init_elf


def build(stock_path, init_path, output):
    if output.exists():
        raise ValueError("Use a fresh private output directory")
    stock = stock_path.read_bytes()
    if hashlib.sha256(stock).hexdigest() != STOCK_HASH:
        raise ValueError("Stock input differs from the inspected firmware")
    binary = init_path.read_bytes()
    elf_report = validate_init_elf(binary)
    entries = []
    for index, name in enumerate(("dev", "proc", "sys", "config", "init"), 1):
        data = binary if name == "init" else b""
        mode = 0o100755 if name == "init" else 0o040755
        fields = (index, mode, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name) + 1, 0)
        entries.append(Entry(name, fields, data))
    archive = pack_newc(entries)
    if parse_newc(archive) != entries:
        raise ValueError("Initramfs roundtrip failed")
    original = AndroidV2(stock)
    payload = original.replace_ramdisk(gzip.compress(archive, compresslevel=9, mtime=0))
    candidate = add_footer(payload, stock)
    avb = verify(candidate)
    parsed = AndroidV2(candidate)
    names = ("kernel", "second", "recovery_dtbo", "dtb")
    if any(parsed.components[name] != original.components[name] for name in names):
        raise ValueError("A component other than ramdisk changed")
    output.mkdir(parents=True)
    image = output / "miel-linux-readout.img"
    image.write_bytes(candidate)
    report = {"purpose": "miel-linux-hardware-readout", "uefi_firmware": False,
        "stock_sha256": STOCK_HASH, "image_sha256": hashlib.sha256(candidate).hexdigest(),
        "image_size": len(candidate), "init_sha256": hashlib.sha256(binary).hexdigest(),
        "ramdisk_paths": [entry.name for entry in entries], "ram_only": False,
        "oem_signature_valid": False, "avb_footer_present": True,
        "avb_algorithm": "NONE", "stock_avb_properties_preserved": avb["properties"],
        "return_mode_requested": "bootloader (Linux RESTART2)",
        "external_userspace_payloads": False, "libc_linked": False,
        "unchanged_stock_components": {name: hashlib.sha256(parsed.components[name]).hexdigest() for name in names},
        "physical_execution_verified": False,
        "usb_identity": "MIELUEFIDIAG", "protocol": "MIEL-DIAG/1",
        "init_source_sha256": hashlib.sha256(Path("Tools/Bringup/Diagnostic/init.c").read_bytes()).hexdigest()}
    report["init_elf"] = elf_report
    report["init_support_source_sha256"] = {name: hashlib.sha256(
        (Path("Tools/Bringup/Diagnostic") / name).read_bytes()).hexdigest() for name in ("abi.h", "start.S")}
    (output / "build.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print(json.dumps({"image": str(image), "size": len(candidate), "sha256": report["image_sha256"],
        "uefi_firmware": False, "phone_operations": False}, indent=2))
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--init", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    build(args.stock, args.init, args.out)
