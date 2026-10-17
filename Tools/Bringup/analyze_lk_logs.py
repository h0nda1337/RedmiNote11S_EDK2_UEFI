#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Extract a small public hardware summary from private Fastboot LK logs.

The output is an observation of stock bootloader state, not a final runtime map.
Factory identifiers, arbitrary log text and raw captures are never emitted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

NAMES = ("lk_addr_mb", "scratch_addr_mb", "framebuffer", "atf-reserved",
    "atf-log-reserved", "atf-ramdump-reserved", "tee-reserved", "tee-secmem",
    "tee-log-reserved", "tee_ree_reserved", "SSPM-reserved", "logo_db_addr_pa", "tempfb_addr")


def normalize(raw):
    if len(raw) > 2 * 1024 * 1024:
        raise ValueError("Log exceeds the bounded input size")
    output = []
    for packet in re.split(rb"\(bootloader\) ", raw)[1:]:
        packet = re.split(rb"\r?\n(?:OKAY|FAILED|Finished\.)", packet)[0]
        if packet.endswith(b"\r\n"):
            packet = packet[:-2]
        elif packet.endswith(b"\n"):
            packet = packet[:-1]
        output.append(packet)
    return b"".join(output).decode("utf8", "replace")


def summarize(raw):
    text = normalize(raw)
    banks = []
    for base, size in re.findall(r"orig_dram start:\s*(0x[0-9a-f]+)\s+orig_dram size:\s*(0x[0-9a-f]+)", text, re.I):
        base, size = int(base, 16), int(size, 16)
        if size:
            banks.append({"base": hex(base), "size": hex(size)})
    if not banks:
        raise ValueError("No complete nonempty DRAM bank observations")
    free = []
    for base, size in re.findall(r"mblock start:\s*(0x[0-9a-f]+)\s+mblock size:\s*(0x[0-9a-f]+)", text, re.I):
        if int(size, 16):
            free.append({"base": hex(int(base, 16)), "size": hex(int(size, 16))})
    reservations = []
    allowed = "|".join(re.escape(name) for name in NAMES)
    patterns = [
        r"mblock_alloc_range: start:\s*(0x[0-9a-f]+),\s*sz:\s*(0x[0-9a-f]+)[^\r\n]*?name:(" + allowed + r")\b",
        r"mblock_reserve-R start:\s*(0x[0-9a-f]+),\s*sz:\s*(0x[0-9a-f]+)[^\r\n]*?name:(" + allowed + r")\b",
    ]
    for pattern in patterns:
        for base, size, name in re.findall(pattern, text, re.I):
            item = {"name": name, "base": hex(int(base, 16)), "size": hex(int(size, 16))}
            if item not in reservations:
                reservations.append(item)
    overlay = re.findall(r"ovl0,\s*layer=(\d+),[^\r\n]*?dst\(0, 0, (\d+), (\d+)\),pitch=(\d+),fmt=([^, ]+), addr=([0-9a-f]+)", text, re.I)
    if not overlay or len(set(overlay)) != 1:
        raise ValueError("Missing or conflicting active overlay observations")
    layer, width, height, pitch, pixel_format, address = overlay[0]
    fb = {"layer": int(layer), "width": int(width), "height": int(height),
        "stride_bytes": int(pitch), "format": pixel_format, "base": hex(int(address, 16))}
    if fb["stride_bytes"] < fb["width"] * 4 or fb["format"] != "eBGRA8888":
        raise ValueError("Unsupported observed display layout")
    surface_end = int(fb["base"], 16) + fb["stride_bytes"] * fb["height"]
    if not any(item["name"] == "framebuffer" and int(item["base"], 16) <= int(fb["base"], 16)
        and surface_end <= int(item["base"], 16) + int(item["size"], 16) for item in reservations):
        raise ValueError("Visible surface is not covered by the observed framebuffer allocation")
    return {"dram_banks": banks, "preloader_free_mblocks": free,
        "observed_allocation_events": reservations, "active_overlay": fb}


def main(inputs, output):
    observations = [summarize(path.read_bytes()) for path in inputs]
    if any(item != observations[0] for item in observations[1:]):
        raise ValueError("Capture hardware summaries disagree")
    report = {"source": "stock LK/Preloader over selected USB Fastboot",
        "runtime_fdt_captured": False, "complete_runtime_memory_map_proven": False,
        "allocation_events_are_not_a_final_reserved_map": True,
        "consistent_capture_count": len(inputs),
        "private_input_sha256": [hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs],
        **observations[0]}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print("Public hardware summary written; identifiers and raw logs excluded")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    main(args.inputs, args.out)
