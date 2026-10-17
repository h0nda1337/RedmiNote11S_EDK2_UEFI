import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import collect_usb
import inspect_stock_boot as stock


def fdt():
    structure = struct.pack(">I", 1) + b"\0" * 4 + struct.pack(">II", 2, 9)
    return struct.pack(">10I", stock.FDT_MAGIC, 72, 56, 72, 40, 17, 16, 0, 0, 16) + b"\0" * 16 + structure


def table(blob):
    return struct.pack(">8I", stock.DTBO_MAGIC, 64 + len(blob), 32, 32, 1, 32, 2048, 0) + struct.pack(
        ">8I", len(blob), 64, 0, 0, 0, 0, 0, 0) + blob


def boot(blob):
    data = bytearray(6144 + len(blob))
    data[:8] = b"ANDROID!"
    struct.pack_into("<10I", data, 8, 32, 0x40080000, 16, 0x47C80000, 0, 0,
        0x4BC80000, 2048, 2, 0)
    struct.pack_into("<IQIIQ", data, 1632, 0, 0, 1660, len(blob), 0x4BC80000)
    data[6144:] = blob
    return data


class StockValidationTests(unittest.TestCase):
    def test_extracts_dt_table_inside_android_v2_boot(self):
        info, sections = stock.parse_boot(boot(table(fdt())))
        self.assertEqual(info["dtb_offset"], 6144)
        self.assertEqual(stock.parse_dtbo(sections["dtb"])[0][1], fdt())

    def test_accepts_raw_fdt_boot(self):
        self.assertEqual(stock.parse_boot(boot(fdt()))[1]["dtb"], fdt())

    def test_rejects_truncated_boot_section(self):
        with self.assertRaises(ValueError):
            stock.parse_boot(boot(fdt())[:-1])

    def test_rejects_unexpected_boot_format(self):
        data = boot(fdt())
        struct.pack_into("<I", data, 40, 4)
        with self.assertRaisesRegex(ValueError, "got v4"):
            stock.parse_boot(data)

    def test_rejects_zero_page_size(self):
        data = boot(fdt())
        struct.pack_into("<I", data, 36, 0)
        with self.assertRaisesRegex(ValueError, "page size"):
            stock.parse_boot(data)

    def test_rejects_dtbo_offset_outside_file(self):
        data = bytearray(table(fdt()))
        struct.pack_into(">I", data, 36, 0xFFFFFF00)
        with self.assertRaises(ValueError):
            stock.parse_dtbo(data)

    def test_rejects_dtbo_overlapping_table(self):
        data = bytearray(table(fdt()))
        struct.pack_into(">I", data, 36, 32)
        with self.assertRaisesRegex(ValueError, "overlaps"):
            stock.parse_dtbo(data)

    def test_rejects_bad_fdt_sections(self):
        data = bytearray(fdt())
        struct.pack_into(">I", data, 12, 99999)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            stock.fdt_size(data)


class UsbSelectionTests(unittest.TestCase):
    @patch("collect_usb.subprocess.run")
    def test_every_adb_call_selects_usb(self, run):
        collect_usb.adb_run("adb", "shell", "id")
        self.assertEqual(run.call_args.args[0], ["adb", "-d", "shell", "id"])

    def fake_adb(self, device):
        def run(_adb, *args):
            if args == ("get-serialno",):
                output = b"LOCAL_TEST_SERIAL\n"
            elif args[:2] == ("shell", "getprop"):
                output = {"ro.product.device": device, "ro.board.platform": "mt6781",
                    "ro.product.model": "2201117SL"}.get(args[2], "").encode()
            else:
                return subprocess.CompletedProcess(args, 1, b"", b"Permission denied")
            return subprocess.CompletedProcess(args, 0, output, b"")
        return run

    def test_refuses_other_usb_phone_without_writing_report(self):
        with tempfile.TemporaryDirectory() as directory, patch(
                "collect_usb.adb_run", side_effect=self.fake_adb("guamp")):
            output = Path(directory) / "report"
            with self.assertRaisesRegex(RuntimeError, "not the expected"):
                collect_usb.collect("adb", output)
            self.assertFalse(output.exists())

    def test_records_denied_access_without_serial_or_fake_dtb(self):
        with tempfile.TemporaryDirectory() as directory, patch(
                "collect_usb.adb_run", side_effect=self.fake_adb("miel")):
            output = Path(directory)
            collect_usb.collect("adb", output)
            text = (output / "usb-report.json").read_text(encoding="utf-8")
            self.assertNotIn("LOCAL_TEST_SERIAL", text)
            self.assertEqual(json.loads(text)["probes"]["live_fdt"]["exit_code"], 1)
            self.assertFalse((output / "live.dtb").exists())


if __name__ == "__main__":
    unittest.main()
