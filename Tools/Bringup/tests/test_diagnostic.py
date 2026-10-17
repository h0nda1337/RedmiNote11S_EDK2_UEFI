import io
import sys
from pathlib import Path
import unittest
import tempfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import receive_diagnostic as receiver
from own_image import Entry, pack_newc, parse_newc


def packet():
    files = {"runtime.dtb": bytes.fromhex("d00dfeed") + bytes(36),
        "iomem.txt": b"40000000-46ffffff : System RAM\n", "cmdline.txt": b"readout\n",
        "battery.txt": b"49\n", "fb-fix.bin": bytes(80), "fb-var.bin": bytes(160)}
    out = bytearray(b"MIEL-DIAG/1 BEGIN\n")
    for name in receiver.NAMES:
        data = files[name]
        out.extend(f"FILE {name} {len(data)}\n".encode() + data + b"\n")
    out.extend(b"MIEL-DIAG/1 END\n")
    return bytes(out), files


class DiagnosticProtocolTests(unittest.TestCase):
    def test_exact_binary_transfer(self):
        raw, expected = packet()
        self.assertEqual(receiver.receive(io.BytesIO(raw)), expected)

    def test_refuses_other_gadget_protocol(self):
        raw, _ = packet()
        with self.assertRaisesRegex(ValueError, "identity"):
            receiver.receive(io.BytesIO(raw.replace(b"MIEL-DIAG", b"OPPO-DIAG", 1)))

    def test_rejects_oversized_file_before_reading_payload(self):
        raw = b"MIEL-DIAG/1 BEGIN\nFILE runtime.dtb 2097152\n"
        with self.assertRaisesRegex(ValueError, "Excessive"):
            receiver.receive(io.BytesIO(raw))

    def test_refuses_battery_under_minimum(self):
        raw, _ = packet()
        with self.assertRaisesRegex(ValueError, "battery"):
            receiver.receive(io.BytesIO(raw.replace(b"49\n", b"19\n")))

    def test_missing_runtime_fdt_is_not_success(self):
        raw, _ = packet()
        with self.assertRaisesRegex(ValueError, "runtime FDT"):
            receiver.receive(io.BytesIO(raw.replace(bytes.fromhex("d00dfeed"), b"NONE", 1)))

    def test_truncated_transfer_times_out(self):
        with patch("receive_diagnostic.time.monotonic", side_effect=[0, 1, 61]), patch(
                "receive_diagnostic.time.sleep"):
            with self.assertRaisesRegex(ValueError, "timeout"):
                receiver.read_exact(io.BytesIO(b"x"), 2, 60)

    def test_bootloader_return_ends_gadget_wait_for_prompt_restoration(self):
        with tempfile.TemporaryDirectory() as temporary, patch("serial.tools.list_ports.comports", return_value=[]):
            with self.assertRaisesRegex(RuntimeError, "returned to Fastboot"):
                receiver.main(Path(temporary) / "capture", 190, abort_if=lambda: True)


class OwnedArchiveTests(unittest.TestCase):
    def test_minimal_initramfs_roundtrip(self):
        entry = Entry("init", (1, 0o100755, 0, 0, 1, 0, 4, 0, 0, 0, 0, 5, 0), b"ELF!")
        self.assertEqual(parse_newc(pack_newc([entry])), [entry])

    def test_archive_traversal_is_rejected(self):
        entry = Entry("../init", (1, 0o100755, 0, 0, 1, 0, 4, 0, 0, 0, 0, 8, 0), b"ELF!")
        with self.assertRaisesRegex(ValueError, "Unsafe"):
            parse_newc(pack_newc([entry]))


if __name__ == "__main__":
    unittest.main()
