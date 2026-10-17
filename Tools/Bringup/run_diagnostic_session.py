#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Active USB-only diagnostic test with restoration of the matching stock boot.

Writes only boot_a, after strict host and device checks. No erase, slot change,
vbmeta, preloader, LK, recovery service or user-data operation is implemented.
The minimal init returns to stock Fastboot; the original matching boot file is
restored before Android is requested. Raw observations stay in Local/.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

from avb_none import verify
from own_image import AndroidV2, parse_newc
from probe_stock_ram_boot import STOCK_HASH
from receive_diagnostic import main as receive_usb
from fastboot_values import hex_size
from init_elf import validate_init_elf


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate(stock, candidate, manifest):
    if sha(stock) != STOCK_HASH or len(stock) != 67108864:
        raise ValueError("Unexpected stock restoration file")
    if manifest.get("purpose") != "miel-linux-hardware-readout" or manifest.get("uefi_firmware") is not False:
        raise ValueError("Expected the original MIEL Linux readout manifest")
    if len(candidate) != len(stock) or sha(candidate) != manifest.get("image_sha256"):
        raise ValueError("Diagnostic image size or hash mismatch")
    original, changed = AndroidV2(stock), AndroidV2(candidate)
    for name in ("kernel", "second", "recovery_dtbo", "dtb"):
        if original.components[name] != changed.components[name]:
            raise ValueError("Stock driver or device-tree component changed")
    avb = verify(candidate)
    if avb["properties"] != verify(stock, require_unsigned=False)["properties"]:
        raise ValueError("Stock boot properties were not preserved")
    entries = parse_newc(gzip.decompress(changed.components["ramdisk"]))
    if [entry.name for entry in entries] != ["dev", "proc", "sys", "config", "init"]:
        raise ValueError("Unexpected userspace payload in the diagnostic")
    init = entries[-1]
    if init.fields[1] != 0o100755 or sha(init.data) != manifest.get("init_sha256"):
        raise ValueError("Diagnostic init differs from the reviewed build")
    validate_init_elf(init.data)
    if sha(Path("Tools/Bringup/Diagnostic/init.c").read_bytes()) != manifest.get("init_source_sha256"):
        raise ValueError("Diagnostic source changed since build")
    support = {name: sha((Path("Tools/Bringup/Diagnostic") / name).read_bytes()) for name in ("abi.h", "start.S")}
    if support != manifest.get("init_support_source_sha256"):
        raise ValueError("Diagnostic entry or ABI changed since build")
    if manifest.get("return_mode_requested") != "bootloader (Linux RESTART2)":
        raise ValueError("Diagnostic must request return to the existing bootloader")
    return init.data


def main(stock_path, image_path, output, native_mode=None):
    if output.exists():
        raise ValueError("Use a fresh private session directory")
    stock, image = stock_path.read_bytes(), image_path.read_bytes()
    manifest = json.loads(image_path.with_name("build.json").read_text())
    if native_mode is not None:
        from native_boot import validate as validate_native
        def validate_selected(stock, image, manifest):
            validate_native(stock, image, manifest, native_mode)
        init = None
    else:
        validate_selected = validate
        init = validate(stock, image, manifest)
    validate_selected(stock, image, manifest)

    def adb(*args, timeout=10):
        return subprocess.run(["adb", "-d", *args], capture_output=True, timeout=timeout)

    def prop(name):
        result = adb("shell", "getprop", name)
        if result.returncode:
            raise RuntimeError("USB ADB property unavailable: " + name)
        return result.stdout.decode("utf8", "replace").strip()

    if prop("ro.product.device") != "miel" or prop("ro.board.platform") != "mt6781":
        raise RuntimeError("Unexpected USB phone")
    expected_fingerprint = verify(stock, require_unsigned=False)["properties"]["com.android.build.boot.fingerprint"]
    if prop("ro.bootimage.build.fingerprint") != expected_fingerprint or prop("ro.boot.slot_suffix") != "_a":
        raise RuntimeError("Stock file or active slot does not match the USB phone")
    if prop("ro.boot.flash.locked") != "0":
        raise RuntimeError("Bootloader is not unlocked")
    battery = adb("shell", "dumpsys", "battery").stdout.decode()
    match = re.search(r"\blevel:\s*(\d+)", battery)
    if not match or int(match[1]) < 20:
        raise RuntimeError("Battery below minimum or unavailable")
    serial = adb("get-serialno").stdout.decode().strip()
    if not serial or ":" in serial or "_tcp" in serial:
        raise RuntimeError("Unexpected USB serial")
    output.mkdir(parents=True)
    restore_path = output / "matching-stock-boot.img"
    restore_path.write_bytes(stock)
    if sha(restore_path.read_bytes()) != STOCK_HASH:
        raise RuntimeError("Local restoration copy did not verify")
    state = {"phase": "host_and_android_preflight_passed", "battery_before": int(match[1]),
        "stock_sha256": STOCK_HASH, "candidate_sha256": sha(image),
        "phone_partition": "boot_a", "other_partition_commands": False,
        "stock_restored": False, "linux_capture_complete": False,
        "uefi_execution_verified": False, "native_mode": native_mode, "results": []}

    def save():
        (output / "session.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf8")

    def fb(*args, timeout=15):
        result = subprocess.run(["fastboot", "-s", serial, *args], capture_output=True, timeout=timeout)
        text = (result.stdout + result.stderr).decode("utf8", "replace")
        state["results"].append({"command": list(args[:2]), "exit_code": result.returncode, "output": text})
        save()
        return result, text

    def ready(seconds):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            try:
                result, text = fb("getvar", "product", timeout=3)
                if result.returncode == 0 and re.search(r"product:\s*miel\b", text):
                    return True
            except subprocess.TimeoutExpired:
                pass
            time.sleep(1)
        return False

    def variable(name):
        result, text = fb("getvar", name)
        match = re.search(re.escape(name)+r":\s*([^\r\n]+)", text)
        if result.returncode or not match:
            raise RuntimeError("Required Fastboot variable unavailable: " + name)
        return match[1].strip()

    save()
    # Exercise this exact ELF on the phone's own kernel before touching boot_a.
    # The reviewed getpid guard exits immediately with status 2 outside PID 1.
    if init is not None:
        init_check = output / "reviewed-init.elf"
        init_check.write_bytes(init)
        remote_check = "/data/local/tmp/miel-diag-exec-check"
        try:
            if adb("push", str(init_check), remote_check).returncode or adb("shell", "chmod", "700", remote_check).returncode:
                raise RuntimeError("Could not prepare the unprivileged ELF load check")
            result = adb("shell", remote_check)
            if result.returncode != 2 or result.stdout.strip() or result.stderr.strip():
                raise RuntimeError("Phone kernel rejected the diagnostic ELF or PID 1 guard")
            state["android_elf_load_verified"] = True
            state["non_pid1_exit_code"] = result.returncode
            save()
        finally:
            adb("shell", "rm", "-f", remote_check)
    print("Host image validated; entering verified USB bootloader", flush=True)
    result = adb("reboot", "bootloader")
    if result.returncode or not ready(45):
        raise RuntimeError("Verified USB bootloader did not appear")
    written = False
    try:
        if variable("unlocked") != "yes" or variable("current-slot") != "a" or variable("is-userspace") != "no":
            raise RuntimeError("Bootloader state differs from the reviewed active-slot plan")
        if hex_size(variable("partition-size:boot_a")) != len(image):
            raise RuntimeError("Unexpected boot_a geometry")
        # Validate again immediately before sending the sole diagnostic write.
        validate_selected(stock, image_path.read_bytes(), manifest)
        state["phase"] = "writing_temporary_diagnostic_boot_a"
        save()
        print("Writing reviewed temporary image to boot_a; matching stock restoration is prepared", flush=True)
        written = True
        result, text = fb("flash", "boot_a", str(image_path), timeout=90)
        if result.returncode:
            raise RuntimeError("Diagnostic write failed; restoring stock")
        state["phase"] = "waiting_native_test_return" if native_mode else "waiting_original_linux_readout"
        save()
        result, text = fb("reboot", timeout=15)
        if result.returncode:
            raise RuntimeError("Diagnostic reboot was not acknowledged")
        if native_mode:
            print("Native " + native_mode + " test started; observing return to selected Fastboot", flush=True)
            native_start = time.monotonic()
            state["native_bootloader_return_observed"] = ready(120 if native_mode == "shell" else 35)
            state["native_return_elapsed_seconds"] = round(time.monotonic() - native_start, 3)
            save()
            return_to_android = True
        else:
            return_to_android = False
        try:
            def returned_bootloader():
                try:
                    result, text = fb("getvar", "product", timeout=0.5)
                    return result.returncode == 0 and bool(re.search(r"product:\s*miel\b", text))
                except subprocess.TimeoutExpired:
                    return False

            if not return_to_android:
                receive_usb(output / "capture", 190, abort_if=returned_bootloader)
                state["linux_capture_complete"] = True
        except Exception as error:
            state["capture_error"] = str(error)
            print("Readout did not complete; waiting for the init's bootloader return", flush=True)
        save()
    finally:
        if written:
            state["phase"] = "waiting_fastboot_for_stock_restoration"
            save()
            print("Waiting for selected USB Fastboot to restore stock boot_a", flush=True)
            if not ready(240):
                state["phase"] = "bootloader_return_requires_attention"
                save()
                raise RuntimeError("Stock restoration awaits the selected USB bootloader")
            if sha(restore_path.read_bytes()) != STOCK_HASH:
                state["phase"] = "stock_restoration_copy_hash_mismatch"
                save()
                raise RuntimeError("Restoration copy changed; refusing an unverified stock write")
            result, text = fb("flash", "boot_a", str(restore_path), timeout=90)
            if result.returncode:
                state["phase"] = "stock_restoration_requires_attention"
                save()
                raise RuntimeError("Stock boot restoration failed; phone remains in Fastboot")
            state["stock_restored"] = True
            save()
        if ready(3):
            fb("reboot", timeout=15)
    state["phase"] = "waiting_android_after_restoration"
    save()
    deadline = time.monotonic()+150
    while time.monotonic() < deadline:
        result = adb("shell", "getprop", "sys.boot_completed", timeout=5)
        if result.returncode == 0 and result.stdout.strip() == b"1":
            if prop("ro.bootimage.build.fingerprint") != expected_fingerprint or prop("ro.boot.slot_suffix") != "_a":
                raise RuntimeError("Restored Android metadata mismatch")
            state.update(phase="stock_android_returned", boot_reason=prop("ro.boot.bootreason"))
            save()
            print("Matching stock boot restored; Android boot completed on USB", flush=True)
            return
        time.sleep(2)
    raise RuntimeError("Restored Android has not yet been observed on USB")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stock", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--execute-temporary-boot-test", action="store_true")
    parser.add_argument("--native-mode", choices=("handoff", "mmu", "shell"))
    args = parser.parse_args()
    if not args.execute_temporary_boot_test:
        parser.error("This active-session operation requires the explicit execution flag")
    main(args.stock, args.image, args.out, args.native_mode)
