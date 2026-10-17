import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from init_elf import validate_init_elf


def fixture():
    raw = bytearray(0x1004)
    raw[:7] = b"\x7fELF\x02\x01\x01"
    struct.pack_into("<HHIQQ", raw, 16, 2, 183, 1, 0x400100, 64)
    struct.pack_into("<HHH", raw, 52, 64, 56, 2)
    struct.pack_into("<IIQQQQQQ", raw, 64, 1, 5, 0, 0x400000, 0, 0x200, 0x200, 65536)
    struct.pack_into("<IIQQQQQQ", raw, 120, 1, 6, 0x1000, 0x411000, 0, 4, 8192, 65536)
    return raw


class InitElfTests(unittest.TestCase):
    def test_file_backed_writable_bss_is_accepted(self):
        self.assertTrue(validate_init_elf(fixture())["all_loads_file_backed"])

    def test_failed_phone_layout_is_rejected_before_packaging(self):
        raw = fixture()
        struct.pack_into("<IIQQQQQQ", raw, 120, 1, 6, 0xfff0, 0x41fff0, 0, 0, 0x200000, 65536)
        with self.assertRaisesRegex(ValueError, "file-backed"):
            validate_init_elf(raw)

    def test_out_of_file_load_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "load bounds"):
            validate_init_elf(fixture()[:-1])

    def test_interpreter_is_rejected(self):
        raw = fixture()
        struct.pack_into("<I", raw, 120, 3)
        with self.assertRaisesRegex(ValueError, "dynamic loader"):
            validate_init_elf(raw)

    def test_entry_in_bss_is_rejected(self):
        raw = fixture()
        struct.pack_into("<Q", raw, 24, 0x411004)
        with self.assertRaisesRegex(ValueError, "file-backed entry"):
            validate_init_elf(raw)


if __name__ == "__main__":
    unittest.main()
