import gzip
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_boot import FD_SIZE, inflate, repack
from own_image import AndroidV2, inflate_ramdisk, parse_newc
from test_bringup import boot, fdt, table


class NativeTransportTests(unittest.TestCase):
    def test_bounded_transport_roundtrip(self):
        raw = b"N" * FD_SIZE
        self.assertEqual(inflate(gzip.compress(raw, mtime=0)), raw)

    def test_concatenated_or_truncated_transport_is_rejected(self):
        packed = gzip.compress(bytes(FD_SIZE), mtime=0)
        for raw in (packed + packed, packed[:-1], gzip.compress(bytes(FD_SIZE+1), mtime=0)):
            with self.assertRaises(ValueError):
                inflate(raw)

    def test_repack_retains_stock_tree_and_header_and_has_no_userspace(self):
        original = bytes(boot(table(fdt())))
        changed = repack(original, bytes(FD_SIZE))
        parsed = AndroidV2(changed)
        self.assertEqual(parsed.components["dtb"], AndroidV2(original).components["dtb"])
        self.assertEqual(parse_newc(inflate_ramdisk(parsed.components["ramdisk"])), [])
        self.assertEqual(parsed.header[20:576], original[20:576])
        self.assertEqual(parsed.header[608:1632], original[608:1632])
        self.assertEqual(inflate(parsed.components["kernel"]), bytes(FD_SIZE))

    def test_different_stock_load_address_is_rejected(self):
        original = boot(table(fdt()))
        struct.pack_into("<I", original, 12, 0x48080000)
        with self.assertRaisesRegex(ValueError, "load address"):
            repack(original, bytes(FD_SIZE))


if __name__ == "__main__":
    unittest.main()
