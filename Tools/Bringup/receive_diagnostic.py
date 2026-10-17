#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Receive the owner's MIEL Linux readout over its dedicated USB CDC-ACM identity.

Protocol design adapted from h0nda1337/OppoReno5Lite_EDK2_UEFI. Raw captures are
private. This tool never operates on ADB, Fastboot or phone partitions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

NAMES = ("runtime.dtb", "iomem.txt", "cmdline.txt", "battery.txt", "fb-fix.bin", "fb-var.bin")
USB_IDENTITY = "MIELUEFIDIAG"


def read_exact(stream, size, deadline):
    output = bytearray()
    while len(output) < size:
        if time.monotonic() >= deadline:
            raise ValueError("Diagnostic transfer timeout")
        part = stream.read(size - len(output))
        output.extend(part)
        if not part:
            time.sleep(0.005)
    return bytes(output)


def read_line(stream, deadline):
    line = bytearray()
    while len(line) < 128:
        value = read_exact(stream, 1, deadline)
        line.extend(value)
        if value == b"\n":
            return bytes(line)
    raise ValueError("Excessive protocol line")


def receive(stream):
    deadline = time.monotonic() + 60
    if read_line(stream, deadline) != b"MIEL-DIAG/1 BEGIN\n":
        raise ValueError("Unexpected diagnostic identity")
    files = {}
    for name in NAMES:
        header = read_line(stream, deadline).decode("ascii").strip().split()
        if len(header) != 3 or header[:2] != ["FILE", name] or not header[2].isdigit():
            raise ValueError("Unexpected file header")
        size = int(header[2])
        if size >= 2 * 1024 * 1024:
            raise ValueError("Excessive file size")
        files[name] = read_exact(stream, size, deadline)
        if read_exact(stream, 1, deadline) != b"\n":
            raise ValueError("Invalid file delimiter")
    if read_line(stream, deadline) != b"MIEL-DIAG/1 END\n":
        raise ValueError("Missing completion marker")
    if not 20 <= int(files["battery.txt"].strip()) <= 100:
        raise ValueError("Invalid battery sample")
    if files["runtime.dtb"][:4] != bytes.fromhex("d00dfeed"):
        raise ValueError("A runtime FDT was not captured")
    if len(files["fb-fix.bin"]) != 80 or len(files["fb-var.bin"]) != 160:
        raise ValueError("Unexpected framebuffer ioctl structures")
    return files


def main(output, wait, abort_if=None):
    import serial
    from serial.tools import list_ports
    if output.exists():
        raise ValueError("Use a fresh private capture directory")
    output.mkdir(parents=True)
    print("Waiting for MIELUEFIDIAG USB gadget", flush=True)
    deadline = time.monotonic() + wait
    port = None
    while time.monotonic() < deadline:
        found = [p for p in list_ports.comports() if p.vid == 0x1d6b and p.pid == 0x0104 and p.serial_number == USB_IDENTITY]
        if len(found) > 1:
            raise ValueError("Ambiguous diagnostic USB devices")
        if found:
            port = found[0]
            break
        if abort_if is not None and abort_if():
            raise RuntimeError("Selected phone returned to Fastboot before USB readout")
        time.sleep(1)
    if port is None:
        raise RuntimeError("MIEL diagnostic USB gadget did not appear")
    with serial.Serial(port.device, 115200, timeout=1, write_timeout=3) as stream:
        try:
            stream.reset_input_buffer()
            stream.write(b"READ\n")
            files = receive(stream)
            for name, data in files.items():
                (output / name).write_bytes(data)
            fixed, variable = files["fb-fix.bin"], files["fb-var.bin"]
            report = {"linux_diagnostic_execution_verified": True, "uefi_execution_verified": False,
                "framebuffer_base": hex(struct.unpack_from("<Q", fixed, 16)[0]),
                "framebuffer_size": struct.unpack_from("<I", fixed, 24)[0],
                "stride_bytes": struct.unpack_from("<I", fixed, 48)[0],
                "width": struct.unpack_from("<I", variable, 0)[0],
                "height": struct.unpack_from("<I", variable, 4)[0],
                "bits_per_pixel": struct.unpack_from("<I", variable, 24)[0],
                "files": {name: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for name, data in files.items()}}
            (output / "capture.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
            print(json.dumps({k: v for k, v in report.items() if k != "files"}, indent=2), flush=True)
        finally:
            try:
                stream.write(b"REBOOT\n")
            except (serial.SerialException, OSError):
                # The init also has a bounded local return path. Preserve the
                # original transfer error if the USB connection has vanished.
                pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--wait", type=int, default=160)
    args = parser.parse_args()
    main(args.out, args.wait)
