#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Read bootloader logs over the selected USB transport, then return to Android.

Reboots the phone; never flashes, erases, downloads an image or changes slots.
Raw OEM logs are private and must remain outside version control.
"""
import argparse
import datetime
import json
from pathlib import Path
import re
import subprocess
import time


def run(command, timeout=15):
    return subprocess.run(command, capture_output=True, timeout=timeout)


def adb(*args):
    result = run(["adb", "-d", *args])
    if result.returncode:
        raise RuntimeError("USB ADB command failed: " + result.stderr.decode("utf8", "replace"))
    return result.stdout.decode("utf8", "replace").strip()


def collect(output):
    device = adb("shell", "getprop", "ro.product.device")
    platform = adb("shell", "getprop", "ro.board.platform")
    if device not in ("miel", "fleur") or platform != "mt6781":
        raise RuntimeError("USB phone is not miel/fleur MT6781")
    battery = adb("shell", "dumpsys", "battery")
    match = re.search(r"\blevel:\s*(\d+)", battery)
    if not match or int(match[1]) < 20:
        raise RuntimeError("Battery below 20% or unreadable")
    serial = adb("get-serialno")
    if not serial or ":" in serial or "_tcp" in serial:
        raise RuntimeError("Unexpected USB serial")
    output.mkdir(parents=True, exist_ok=False)
    report = {"started_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "device": device, "platform": platform, "battery_percent": int(match[1]),
        "flash_commands_sent": False, "results": {}}
    print("Rebooting the verified USB MT6781 to its existing bootloader", flush=True)
    adb("reboot", "bootloader")
    ready = False
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        try:
            result = run(["fastboot", "-s", serial, "getvar", "product"], timeout=4)
            text = (result.stdout + result.stderr).decode("utf8", "replace")
            if result.returncode == 0 and re.search(r"product:\s*(fleur|miel)\b", text):
                ready = True
                break
        except subprocess.TimeoutExpired:
            pass
        time.sleep(1)
    if not ready:
        raise RuntimeError("Selected phone did not become available in Fastboot")
    print("Fastboot product verified; reading variables and OEM logs", flush=True)
    commands = {"product": ["getvar", "product"], "current-slot": ["getvar", "current-slot"],
        "unlocked": ["getvar", "unlocked"], "boot_a_size": ["getvar", "partition-size:boot_a"],
        "boot_b_size": ["getvar", "partition-size:boot_b"],
        "lkmsg": ["oem", "lkmsg"], "lpmsg": ["oem", "lpmsg"],
        "pllk-log": ["oem", "dump_pllk_log"]}
    try:
        for name, command in commands.items():
            try:
                result = run(["fastboot", "-s", serial, *command], timeout=15)
                (output / (name + ".txt")).write_bytes(result.stdout + result.stderr)
                report["results"][name] = {"exit_code": result.returncode,
                    "bytes": len(result.stdout) + len(result.stderr)}
                print(f"{name}: exit={result.returncode}, bytes={report['results'][name]['bytes']}", flush=True)
            except subprocess.TimeoutExpired:
                report["results"][name] = {"timeout": True}
                break
    finally:
        result = run(["fastboot", "-s", serial, "reboot"], timeout=10)
        report["return_reboot_exit_code"] = result.returncode
        (output / "session.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf8")
    print("Readout saved privately; Android reboot requested", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    collect(parser.parse_args().out)
