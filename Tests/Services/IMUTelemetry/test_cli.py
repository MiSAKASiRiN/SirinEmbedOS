import importlib.util
from pathlib import Path
import struct
import unittest
ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("imu", ROOT/"Tools/HostLink/imu_telemetry.py")
imu = importlib.util.module_from_spec(spec); spec.loader.exec_module(imu)

class ProtocolTests(unittest.TestCase):
    def test_human_temperature(self):
        line = imu.human_status("BMI088", {"temperature_c": 59.875}, {"state": 2}, 1000, 12000, 0)
        self.assertIn("59.88 °C", line)
        self.assertIn("12,000", line)
        self.assertIn("就绪", line)
        self.assertIn("旧值", imu.human_status("LSM", {"temperature_c": 63}, {"state": 3}, 0, 0, 0))
        self.assertIn("--", imu.human_status("LSM", None, {}, 0, 0, 0))

    def test_golden(self):
        raw = bytes.fromhex((Path(__file__).parent/"golden_record.hex").read_text())
        payload = struct.pack("<HIIHBB", 0x100, 0x12345678, 0x55667788, 1000, 1, 72) + raw
        stream, samples = imu.decode_samples(payload)
        self.assertEqual(stream, 0x100)
        self.assertEqual(samples[0]["raw_accel"], (-32768, -123, 32767))
        self.assertEqual(samples[0]["temperature_c"], 23.5)
        self.assertEqual(samples[0]["start_us"], 0x1122334455667788)

    def test_all_splits_corruption_recovery(self):
        frame = imu.pack(0, 4, 42, bytes(range(256))+bytes(224))
        for split in range(len(frame)+1):
            d = imu.Decoder()
            decoded = d.feed(frame[:split]) + d.feed(frame[split:])
            self.assertEqual(len(decoded), 1)
            self.assertEqual(decoded[0][3], 42)
        d = imu.Decoder()
        self.assertEqual(len(d.feed(bytes(8)+frame)), 1)
        self.assertEqual(d.feed(b'\0'+b'\x01'*600+b'\0'), [])
        self.assertEqual(len(d.feed(frame)), 1)
        corrupt = bytearray(frame); corrupt[10] ^= 0x40
        self.assertEqual(d.feed(corrupt), [])
        self.assertTrue(d.errors)
        self.assertEqual(len(d.feed(frame)), 1)

if __name__ == '__main__':
    unittest.main()
