import json
from pathlib import Path
import re
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from avb_none import encode, verify


class UnsignedAvbTests(unittest.TestCase):
    def setUp(self):
        self.payload = b"ANDROID!" + bytes(4088)
        self.props = {"com.android.build.boot.os_version": "12"}
        self.image = encode(self.payload, 16384, self.props)

    def test_unsigned_hash_and_properties_roundtrip(self):
        report = verify(self.image)
        self.assertTrue(report["unsigned"])
        self.assertEqual(report["properties"], self.props)
        self.assertEqual(report["image_size"], len(self.payload))

    def test_payload_corruption_is_detected(self):
        image = bytearray(self.image)
        image[512] ^= 1
        with self.assertRaisesRegex(ValueError, "digest mismatch"):
            verify(image)

    def test_metadata_flags_cannot_disable_verification(self):
        image = bytearray(self.image)
        offset = struct.unpack_from(">Q", image, len(image)-44)[0]
        struct.pack_into(">I", image, offset+120, 2)
        with self.assertRaisesRegex(ValueError, "no verification-disable flags"):
            verify(image)

    def test_footer_cannot_point_beyond_image(self):
        image = bytearray(self.image)
        struct.pack_into(">Q", image, len(image)-44, len(image)+4096)
        with self.assertRaises(ValueError):
            verify(image)

    def test_no_room_for_footer_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "capacity"):
            encode(self.payload, 4096, self.props)


class ProfileEvidenceTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[3]
        self.evidence = json.loads((root / "Docs/lk-observations.json").read_text())
        text = (root / "Platform/Xiaomi/mielPkg/Include/MielNativeProfile.h").read_text()
        self.values = {name: int(value, 0) for name, value in re.findall(
            r"#define\s+MIEL_NATIVE_(\w+)\s+(0x[0-9a-fA-F]+|\d+)\b", text)}

    def test_display_profile_matches_two_independent_bootloader_captures(self):
        self.assertGreaterEqual(self.evidence["consistent_capture_count"], 2)
        fb = self.evidence["active_overlay"]
        for macro, key in (("FB_WIDTH", "width"), ("FB_HEIGHT", "height"), ("FB_STRIDE", "stride_bytes")):
            self.assertEqual(self.values[macro], fb[key])
        self.assertEqual(self.values["FB_BASE"], int(fb["base"], 16))

    def test_image_and_heap_fit_observed_early_free_memory(self):
        for label in ("FD", "POOL"):
            base, size = self.values[label+"_BASE"], self.values[label+"_BYTES"]
            self.assertTrue(any(int(bank["base"], 16) <= base and base+size <= int(bank["base"], 16)+int(bank["size"], 16)
                for bank in self.evidence["preloader_free_mblocks"]))
            for reservation in self.evidence["observed_allocation_events"]:
                start, length = int(reservation["base"], 16), int(reservation["size"], 16)
                self.assertFalse(base < start+length and start < base+size)

    def test_all_three_probe_surfaces_fit_observed_vram(self):
        vram = next(item for item in self.evidence["observed_allocation_events"] if item["name"] == "framebuffer")
        base, size = int(vram["base"], 16), int(vram["size"], 16)
        step = self.values["FB_BUFFER_BYTES"]
        self.assertEqual(base, self.values["FB_RESERVATION_BASE"])
        self.assertEqual(size, self.values["FB_RESERVATION_BYTES"])
        self.assertEqual(base+step, self.values["FB_BASE"])
        surface = self.values["FB_STRIDE"] * self.values["FB_HEIGHT"]
        for index in range(3):
            self.assertLessEqual(index*step+surface, size)


if __name__ == "__main__":
    unittest.main()
