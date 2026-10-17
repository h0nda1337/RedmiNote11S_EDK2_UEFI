#!/usr/bin/env python3
"""Inspect Android boot v2 and DTBO offline; never communicates with a phone."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


FDT_MAGIC = 0xD00DFEED
DTBO_MAGIC = 0xD7B7AB1E


def align(value, page):
    return (value + page - 1) // page * page


def bounded(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError("Image section exceeds file bounds")
    return data[offset:offset + size]


def fdt_size(data):
    if len(data) < 40:
        raise ValueError("Truncated FDT header")
    magic, total, structure, strings, reserve, version, last, _, string_len, struct_len = struct.unpack_from(
        ">10I", data)
    if magic != FDT_MAGIC or not 40 <= total <= len(data) or version < 17 or last > 17:
        raise ValueError("Invalid or unsupported FDT header")
    for offset, size in ((structure, struct_len), (strings, string_len), (reserve, 16)):
        if offset < 40 or offset + size > total:
            raise ValueError("FDT section exceeds total size")
    return total


def parse_boot(data):
    if len(data) < 1660 or data[:8] != b"ANDROID!":
        raise ValueError("Not a complete Android boot v2 header")
    fields = struct.unpack_from("<10I", data, 8)
    kernel, kernel_addr, ramdisk, ramdisk_addr, second, second_addr, tags_addr, page, version, osver = fields
    if version != 2:
        raise ValueError(f"Expected verified device format v2; got v{version}")
    if page < 1660 or page > 65536 or page & (page - 1):
        raise ValueError("Invalid page size")
    recovery_size, recovery_offset, header_size, dtb_len, dtb_addr = struct.unpack_from("<IQIIQ", data, 1632)
    if header_size != 1660 or not kernel or not dtb_len:
        raise ValueError("Unexpected v2 header size or missing kernel/DTB")
    sections = {}
    cursor = page
    for name, size in (("kernel", kernel), ("ramdisk", ramdisk), ("second", second)):
        sections[name] = bounded(data, cursor, size)
        cursor += align(size, page)
    if recovery_size:
        if recovery_offset != cursor:
            raise ValueError("Unexpected recovery DTBO offset")
        sections["recovery_dtbo"] = bounded(data, cursor, recovery_size)
        cursor += align(recovery_size, page)
    sections["dtb"] = bounded(data, cursor, dtb_len)
    if sections["dtb"][:4] == struct.pack(">I", DTBO_MAGIC):
        parse_dtbo(sections["dtb"])
    else:
        fdt_size(sections["dtb"])
    cmdline = data[64:576].split(b"\0", 1)[0] + data[608:1632].split(b"\0", 1)[0]
    info = {"header_version": version, "header_size": header_size, "page_size": page,
        "kernel_size": kernel, "ramdisk_size": ramdisk, "second_size": second,
        "dtb_size": dtb_len, "dtb_offset": cursor,
        "kernel_addr": hex(kernel_addr), "ramdisk_addr": hex(ramdisk_addr),
        "second_addr": hex(second_addr), "tags_addr": hex(tags_addr),
        "dtb_addr": hex(dtb_addr), "os_version_encoded": osver,
        "cmdline": cmdline.decode("utf-8", "replace")}
    return info, sections


def parse_dtbo(data):
    if len(data) < 32:
        raise ValueError("Truncated DTBO table")
    magic, total, header_size, entry_size, count, entries_offset, page, version = struct.unpack_from(
        ">8I", data)
    if magic != DTBO_MAGIC or total > len(data) or total < 32 or version != 0:
        raise ValueError("Invalid or unsupported DTBO table")
    if header_size < 32 or entry_size < 32 or entries_offset < header_size:
        raise ValueError("Invalid DTBO table layout")
    table_end = entries_offset + count * entry_size
    bounded(data[:total], entries_offset, count * entry_size)
    entries = []
    for index in range(count):
        size, offset, identifier, revision, *custom = struct.unpack_from(
            ">8I", data, entries_offset + index * entry_size)
        if offset < table_end:
            raise ValueError("DTBO overlaps its table")
        blob = bounded(data[:total], offset, size)
        if fdt_size(blob) != size:
            raise ValueError("DTBO length differs from FDT total size")
        entries.append(({"index": index, "id": identifier, "revision": revision,
            "custom": custom, "size": size, "sha256": hashlib.sha256(blob).hexdigest()}, blob))
    return entries


def inspect(boot, dtbo, output):
    data = boot.read_bytes()
    report, sections = parse_boot(data)
    report["boot_sha256"] = hashlib.sha256(data).hexdigest()
    overlays = []
    if dtbo:
        dtbo_data = dtbo.read_bytes()
        overlays = parse_dtbo(dtbo_data)
        report["dtbo_sha256"] = hashlib.sha256(dtbo_data).hexdigest()
    # Validate all inputs before writing extracted outputs.
    trees = []
    remaining = sections["dtb"]
    if remaining[:4] == struct.pack(">I", DTBO_MAGIC):
        base_entries = parse_dtbo(remaining)
        trees = [blob for _, blob in base_entries]
        report["dtb_container"] = "android-dt-table"
        report["base_entries"] = [info for info, _ in base_entries]
    else:
        report["dtb_container"] = "raw-fdt"
        while remaining:
            size = fdt_size(remaining)
            trees.append(remaining[:size])
            remaining = remaining[size:]
            if remaining and not any(remaining):
                break
    output.mkdir(parents=True, exist_ok=True)
    for index, tree in enumerate(trees):
        (output / f"base-{index}.dtb").write_bytes(tree)
    for info, blob in overlays:
        (output / f"overlay-{info['index']}.dtbo").write_bytes(blob)
    report["dtb_count"] = len(trees)
    report["overlays"] = [info for info, _ in overlays]
    (output / "stock-boot-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Android boot v2, page={report['page_size']}, DTBs={len(trees)}, overlays={len(overlays)}")
    print(f"Report: {output / 'stock-boot-report.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", required=True, type=Path)
    parser.add_argument("--dtbo", type=Path)
    parser.add_argument("--out", default=Path("Local/Stock"), type=Path)
    args = parser.parse_args()
    inspect(args.boot, args.dtbo, args.out)


if __name__ == "__main__":
    main()
