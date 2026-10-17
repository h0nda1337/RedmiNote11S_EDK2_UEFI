#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Active USB session: test RAM boot of the matching stock boot image only.

Adapted from the owner's OppoReno5Lite_EDK2_UEFI probe design. No flash, erase,
slot change, unlock or third-party payload is implemented.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time

STOCK_HASH = "f82b804775d3d205b774ace88e3a36c65f9500f5df8a312a370322fc2999cf5d"


def run(command, timeout=15):
    return subprocess.run(command, capture_output=True, timeout=timeout)


def main(image, output):
    if output.exists():
        raise ValueError("Preserve prior evidence; use a fresh output directory")
    if hashlib.sha256(image.read_bytes()).hexdigest() != STOCK_HASH:
        raise ValueError("Image does not match the inspected stock boot file")

    def adb(*args, timeout=15):
        return run(["adb", "-d", *args], timeout)

    def prop(name):
        result = adb("shell", "getprop", name)
        if result.returncode:
            raise RuntimeError("USB property unavailable: " + name)
        return result.stdout.decode().strip()

    if prop("ro.product.device") not in ("miel", "fleur") or prop("ro.board.platform") != "mt6781":
        raise RuntimeError("Unexpected USB device")
    if prop("ro.boot.flash.locked") != "0" or "TKEMIXM" not in prop("ro.build.fingerprint"):
        raise RuntimeError("Unexpected lock state or firmware")
    battery = adb("shell", "dumpsys", "battery").stdout.decode()
    level = re.search(r"\blevel:\s*(\d+)", battery)
    if not level or int(level[1]) < 20:
        raise RuntimeError("Battery below minimum or unavailable")
    serial = adb("get-serialno").stdout.decode().strip()
    if not serial or ":" in serial or "_tcp" in serial:
        raise RuntimeError("Invalid USB identity")
    output.mkdir(parents=True)
    state = {"purpose": "stock-ram-boot-path-probe", "stock_sha256": STOCK_HASH,
        "persistent_flash_commands_sent": False, "lock_commands_sent": False,
        "battery_before": int(level[1]), "ram_execution_proven": False, "results": []}

    def save():
        (output / "probe.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf8")

    def fb(*args, timeout=15):
        result = run(["fastboot", "-s", serial, *args], timeout)
        text = (result.stdout + result.stderr).decode("utf8", "replace")
        state["results"].append({"command": list(args[:2]), "exit_code": result.returncode, "output": text})
        save()
        return result, text

    def getvar(name):
        result, text = fb("getvar", name)
        match = re.search(re.escape(name) + r":\s*([^\r\n]+)", text)
        if result.returncode or not match:
            raise RuntimeError("Fastboot variable unavailable: " + name)
        return match[1].strip()

    state["phase"] = "entering_fastboot"
    save()
    result = adb("reboot", "bootloader")
    if result.returncode:
        raise RuntimeError("USB reboot failed")
    ready = False
    for _ in range(15):
        try:
            result, text = fb("getvar", "product", timeout=3)
            if result.returncode == 0 and re.search(r"product:\s*(fleur|miel)\b", text):
                ready = True
                break
        except subprocess.TimeoutExpired:
            pass
        time.sleep(1)
    if not ready:
        raise RuntimeError("Verified phone did not appear in Fastboot")
    try:
        if getvar("unlocked") != "yes" or getvar("is-userspace") != "no":
            raise RuntimeError("Expected unlocked bootloader Fastboot")
        if int(getvar("max-download-size"), 0) < image.stat().st_size:
            raise RuntimeError("Image exceeds download capacity")
        state["slot_before"] = getvar("current-slot")
        state["phase"] = "stock_ram_boot"
        save()
        print("Testing stock image through fastboot boot; no partition write", flush=True)
        try:
            result, text = fb("boot", str(image), timeout=90)
            state["boot_command_accepted"] = result.returncode == 0
            print(text[:2000], flush=True)
        except subprocess.TimeoutExpired:
            state["boot_command_timeout"] = True
        save()
    finally:
        try:
            result, text = fb("getvar", "product", timeout=3)
            if result.returncode == 0 and re.search(r"product:\s*(fleur|miel)\b", text):
                fb("reboot", timeout=10)
        except subprocess.TimeoutExpired:
            pass
    state["phase"] = "waiting_android"
    save()
    print("Waiting for Android on the USB transport", flush=True)
    deadline = time.monotonic() + 150
    while time.monotonic() < deadline:
        result = adb("shell", "getprop", "sys.boot_completed", timeout=5)
        if result.returncode == 0 and result.stdout.strip() == b"1":
            if prop("ro.product.device") not in ("miel", "fleur"):
                raise RuntimeError("USB device changed")
            state.update(phase="android_returned", boot_reason=prop("ro.boot.bootreason"),
                slot_after=prop("ro.boot.slot_suffix"))
            save()
            print("Android returned; results saved privately", flush=True)
            return
        time.sleep(2)
    state["phase"] = "android_not_observed"
    save()
    raise RuntimeError("Android has not been observed on USB")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    main(args.image, args.out)
