import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fastboot_values import hex_size


class FastbootSizeTests(unittest.TestCase):
    def test_miel_digit_only_partition_size_is_hex(self):
        self.assertEqual(hex_size("4000000"), 67108864)

    def test_optional_prefix_and_case(self):
        for text in ("0x4000000", "0X4000000", "04000000"):
            self.assertEqual(hex_size(text), 67108864)
        self.assertEqual(hex_size("a00000"), 10485760)

    def test_rejects_units_signs_and_partial_numbers(self):
        for text in ("", "-4000000", "4000000 bytes", "0x", " 4000000", "4000000\n", "1"*17):
            with self.assertRaises(ValueError):
                hex_size(text)


if __name__ == "__main__":
    unittest.main()
