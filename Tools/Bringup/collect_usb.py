#!/usr/bin/env python3
"""Collect a bounded, read-only bring-up report from the USB MT6781 phone."""
import argparse
import datetime
import json
from pathlib import Path
import subprocess


PROPERTIES = (
    "ro.product.model", "ro.product.device", "ro.board.platform",
    "ro.product.cpu.abilist", "ro.build.fingerprint",
    "ro.bootimage.build.fingerprint", "ro.build.version.release",
    "ro.boot.flash.locked", "ro.boot.verifiedbootstate",
    "ro.boot.vbmeta.device_state", "ro.boot.slot_suffix",
    "ro.boot.hardware", "ro.boot.product.hardware.sku",
    "ro.boot.dtb_idx", "ro.boot.dtbo_idx",
)


def adb_run(adb, *args):
    # -d selects the single USB transport, even when network devices are present.
    return subprocess.run([adb, "-d", *args], capture_output=True, timeout=15)


def collect(adb, output):
    serial = adb_run(adb, "get-serialno")
    if serial.returncode or not serial.stdout.strip():
        raise RuntimeError("No unique authorized USB device is available")
    props = {}
    for name in PROPERTIES:
        result = adb_run(adb, "shell", "getprop", name)
        if result.returncode:
            raise RuntimeError(f"Cannot read {name} from USB device")
        props[name] = result.stdout.decode("utf-8", "replace").strip()
    if props["ro.product.device"] not in ("miel", "fleur") or props["ro.board.platform"] != "mt6781":
        raise RuntimeError("USB device is not the expected miel/fleur MT6781")

    report = {
        "captured_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "transport": "usb (-d)", "properties": props, "probes": {},
    }
    probes = {
        "identity": ["shell", "id"],
        "kernel": ["shell", "uname -a"],
        "memory": ["shell", "cat /proc/meminfo"],
        "partitions": ["shell", "ls -l /dev/block/by-name"],
        "iomem_access": ["shell", "test -r /proc/iomem"],
        "cmdline_access": ["shell", "test -r /proc/cmdline"],
        "boot_access": ["shell", "test -r /dev/block/by-name/boot_a"],
        "su_present": ["shell", "command -v su"],
        "reserved_node_names": ["shell", "ls /sys/firmware/devicetree/base/reserved-memory"],
    }
    for name, args in probes.items():
        result = adb_run(adb, *args)
        report["probes"][name] = {
            "exit_code": result.returncode,
            "stdout": result.stdout.decode("utf-8", "replace").strip(),
            "stderr": result.stderr.decode("utf-8", "replace").strip(),
        }
    fdt = adb_run(adb, "exec-out", "cat /sys/firmware/fdt")
    report["probes"]["live_fdt"] = {"exit_code": fdt.returncode,
        "stderr": fdt.stderr.decode("utf-8", "replace").strip()}
    if adb_run(adb, "get-serialno").stdout.strip() != serial.stdout.strip():
        raise RuntimeError("USB device changed during collection")
    output.mkdir(parents=True, exist_ok=True)
    if fdt.returncode == 0 and fdt.stdout[:4] == bytes.fromhex("d00dfeed"):
        (output / "live.dtb").write_bytes(fdt.stdout)
        report["probes"]["live_fdt"]["file"] = "live.dtb"
    (output / "usb-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"USB {props['ro.product.model']} / {props['ro.product.device']} / mt6781")
    print(f"Report: {output / 'usb-report.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--out", type=Path, default=Path("Local/Device"))
    args = parser.parse_args()
    collect(args.adb, args.out)


if __name__ == "__main__":
    main()
