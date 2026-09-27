import contextlib
import csv
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "Tools/HostLink"))
import imu_capture as capture
import imu_telemetry as imu


def sample(seq=1, start=1000):
    return dict(sequence=seq, state=2, valid_fields=19, start_us=start, end_us=start+60,
                raw_accel=(-32768, 2, 32767), raw_gyro=(-1, 3, -7), raw_temperature=-20,
                accel_m_s2=(1.0, 2.0, 9.0), gyro_rad_s=(-0.1, 0.2, -0.3), temperature_c=23.5)


class Port:
    def __init__(self, fail=False):
        self.buffer = bytearray()
        self.enabled = {s: False for s in imu.STREAMS}
        self.sequences = {s: 0 for s in imu.STREAMS}
        self.is_open = True
        self.reads = 0
        self.fail = fail

    @property
    def in_waiting(self):
        if not self.buffer:
            for stream, enabled in self.enabled.items():
                if not enabled:
                    continue
                self.sequences[stream] += 1
                seq = self.sequences[stream]
                record = imu.RECORD.pack(1, 2, 0, seq, 19, seq*1000, seq*1000+60,
                                         -32768, 2, 32767, -1, 3, -7, -20, 0,
                                         1, 2, 9, -.1, .2, -.3, 23.5)
                payload = struct.pack("<HIIHBB", stream, seq, seq*1000, 1000, 1, 72) + record
                self.buffer.extend(imu.pack(4, 2, seq, payload, kind=2))
        return len(self.buffer)

    def read(self, count):
        self.reads += 1
        if self.fail and self.reads > 12:
            if self.fail == "interrupt":
                raise KeyboardInterrupt
            raise OSError("simulated disconnect")
        time.sleep(.001)
        data = bytes(self.buffer[:count])
        del self.buffer[:count]
        return data

    def write(self, packet):
        kind, service, message, seq, payload = imu.unpack(packet[1:-1])
        body = b""
        if (service, message) == (4, 1):
            stream, period, enabled = struct.unpack("<HIB", payload)
            self.enabled[stream] = bool(enabled)
        elif (service, message) == (4, 8):
            body = b"\x01\x02\0\0"
            for stream in imu.STREAMS:
                n = self.sequences[stream]
                body += imu.STATUS.pack(stream, 2, self.enabled[stream], n, n*1000,
                                        0, 1, n, 0, n, 0, 0, n)
        else:
            assert (service, message) == (0, 1)
        self.buffer.extend(imu.pack(service, message, seq, b"\0\0"+body, kind=1))
        return len(packet)

    def close(self):
        self.is_open = False


class AllStreamsPort(Port):
    def __init__(self, fail=False):
        super().__init__(fail)
        self.enabled = {s: False for s in imu.ALL_STREAMS}
        self.sequences = {s: 0 for s in imu.ALL_STREAMS}

    @property
    def in_waiting(self):
        if not self.buffer:
            # Reuse the raw producer, then append the three firmware golden records.
            raw_enabled = self.enabled
            self.enabled = {s: raw_enabled[s] for s in imu.STREAMS}
            super().in_waiting
            self.enabled = raw_enabled
            for stream, name in ((0x102, "ins"), (0x103, "mag"), (0x104, "pressure")):
                if not self.enabled[stream]:
                    continue
                self.sequences[stream] += 1
                seq = self.sequences[stream]
                stamp = seq * imu.PERIODS[stream]
                record = bytearray.fromhex((Path(__file__).parent/f"golden_{name}.hex").read_text())
                struct.pack_into("<I", record, 4, seq)
                if stream == 0x102:
                    struct.pack_into("<QQ", record, 16, stamp, stamp+60)
                else:
                    struct.pack_into("<QQ", record, 12, stamp-60, stamp)
                payload = struct.pack("<HIIHBB", stream, seq, stamp, imu.PERIODS[stream], 1, len(record)) + record
                self.buffer.extend(imu.pack(4, 2, seq, payload, kind=2))
        return len(self.buffer)

    def write(self, packet):
        kind, service, message, seq, payload = imu.unpack(packet[1:-1])
        if (service, message) != (4, 9):
            return super().write(packet)
        body = bytearray(256)
        body[:4] = bytes((1, 5, 3, 3))
        body[64] = 1
        body[68:72] = bytes((2, 2, 2, 2))
        struct.pack_into("<3f", body, 72, 60, 59.75, .25)
        for i, stream in enumerate((0x102, 0x103, 0x104)):
            at = 112+i*48
            n = self.sequences[stream]
            struct.pack_into("<HB", body, at, stream, self.enabled[stream])
            struct.pack_into("<9I", body, at+4, n, 0, 1, n, 0, n, 0, 0, 64)
        self.buffer.extend(imu.pack(service, message, seq, b"\0\0"+body, kind=1))
        return len(packet)


class CaptureTests(unittest.TestCase):
    def test_rows_phases_exact_timestamps_and_wrap(self):
        with tempfile.TemporaryDirectory() as root:
            rec = capture.Recorder(Path(root)/"capture", 1, 2, {})
            rec.start_ns = 10**18
            for seq, t in [(0xffffffff, 1000), (0, 2000), (3, 5000), (3, 5000)]:
                rec.samples(0x100, [sample(seq, t)], 10**18+1_500_000_000)
            rec.samples(0x101, [sample()], 10**18+100)
            rec.stopping = True
            rec.samples(0x101, [sample(2, 2000)], 10**18+100)
            rec.received(b"\0abc\0", 0)
            rec.finish("interrupted", None, None)
            rec.close()
            with (rec.directory/"bmi088.csv").open() as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(len(rows), 4)
            self.assertEqual(rows[0]["host_receive_monotonic_ns"], "1000000001500000000")
            self.assertEqual(rows[0]["raw_accel_x"], "-32768")
            self.assertEqual(rows[0]["temperature_c"], "23.5")
            summary = json.loads((rec.directory/"metadata.json").read_text())
            self.assertFalse(summary["complete"])
            self.assertEqual(summary["streams"]["256"]["gaps"], 2)
            self.assertEqual(summary["streams"]["256"]["duplicates"], 1)
            self.assertEqual(summary["streams"]["257"]["measurement_rows"], 0)
            self.assertEqual((rec.directory/"cdc1_rx.bin").read_bytes(), b"\0abc\0")
            with self.assertRaises(FileExistsError):
                capture.Recorder(rec.directory, 0, 1, {})

    def test_full_cli_and_disconnect_preserve_data(self):
        for fail in (False, "disconnect", "interrupt"):
            with self.subTest(fail=fail), tempfile.TemporaryDirectory() as root:
                path = Path(root)/"run"
                port = Port(fail=fail)
                with patch.object(capture, "open_port", return_value=port), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    code = capture.main(["--port", "fake", "--duration", ".12", "--warmup", ".005", "--output", str(path)])
                self.assertFalse(port.is_open)
                metadata = json.loads((path/"metadata.json").read_text())
                self.assertEqual(metadata["complete"], not fail)
                self.assertEqual(code == 0, not fail)
                decoded_counts = {s: 0 for s in imu.STREAMS}
                for kind, service, message, seq, body in imu.Decoder().feed((path/"cdc1_rx.bin").read_bytes()):
                    if (kind, service, message) == (2, 4, 2):
                        stream, samples = imu.decode_samples(body)
                        decoded_counts[stream] += len(samples)
                for stream, filename in capture.FILES.items():
                    with (path/filename).open() as f:
                        rows = list(csv.DictReader(f))
                        self.assertGreater(len(rows), 0)
                        self.assertEqual(len(rows), decoded_counts[stream])
                        self.assertEqual(len(rows), metadata["streams"][str(stream)]["rows"])
                self.assertGreater((path/"cdc1_rx.bin").stat().st_size, 0)
                if not fail:
                    self.assertFalse(any(port.enabled.values()))
                    self.assertFalse(metadata["quality_issues"])
                elif fail == "disconnect":
                    self.assertIn("disconnect", metadata["error"])
                else:
                    self.assertEqual(code, 130)
                    self.assertEqual(metadata["result"], "interrupted")

    def test_all_stream_capture_and_ins_only(self):
        for names in (("bmi", "lsm", "ins", "mag", "pressure"), ("ins",)):
            for fail in (False, "disconnect"):
                with self.subTest(names=names, fail=fail), tempfile.TemporaryDirectory() as root:
                    path = Path(root)/"run"
                    port = AllStreamsPort(fail)
                    with patch.object(capture, "open_port", return_value=port), contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                        code = capture.main(["--port", "fake", "--duration", "1.1" if not fail and len(names) == 5 else ".08", "--output", str(path), "--streams", *names])
                    meta = json.loads((path/"metadata.json").read_text())
                    self.assertEqual(code == 0, not fail)
                    if not fail:
                        self.assertEqual(meta["quality_issues"], [])
                    self.assertEqual(meta["last_device_status"]["ins_service"]["feedback_c"], 59.75)
                    decoded = {}
                    for kind, service, message, seq, body in imu.Decoder().feed((path/"cdc1_rx.bin").read_bytes()):
                        if (kind, service, message) == (2, 4, 2):
                            sid, samples = imu.decode_samples(body)
                            decoded.setdefault(sid, []).extend(samples)
                    for name in names:
                        sid = imu.STREAM_OPTIONS[name]
                        with (path/capture.ALL_FILES[sid]).open() as f:
                            rows = list(csv.DictReader(f))
                        self.assertGreater(len(rows), 0)
                        self.assertEqual(len(rows), len(decoded[sid]))
                        self.assertEqual(len(rows), meta["streams"][str(sid)]["rows"])
                        self.assertEqual(list(rows[0]), capture.SCHEMAS[sid])
                        if sid == 0x102:
                            self.assertEqual(rows[0]["quaternion_w"], "1.0")
                            self.assertEqual(rows[0]["gyro_x_rad_s"], "0.125")
                        elif sid == 0x103:
                            self.assertEqual(rows[0]["raw_magnetic_x"], "-32768")
                        elif sid == 0x104:
                            self.assertEqual(rows[0]["pressure_pa"], "101325.0")
                    self.assertFalse(port.is_open)
                    if not fail:
                        self.assertFalse(any(port.enabled.values()))
                    if names == ("ins",):
                        self.assertFalse((path/"bmi088.csv").exists())

    def test_epoch_change_is_reported(self):
        with tempfile.TemporaryDirectory() as root:
            rec = capture.Recorder(Path(root)/"run", 0, 0, {}, {0x102: "INS"})
            rec.start_ns = 1
            raw = bytes.fromhex((Path(__file__).parent/"golden_ins.hex").read_text())
            values = imu.INS_RECORDS[0x102].unpack(raw)
            payload = struct.pack("<HIIHBB", 0x102, values[4], values[7] & 0xffffffff, 1000, 1, len(raw)) + raw
            _, samples = imu.decode_samples(payload)
            rec.samples(0x102, samples, 100)
            changed = dict(samples[0], epoch=samples[0]["epoch"]+1, sequence=samples[0]["sequence"]+1,
                           attitude_us=samples[0]["attitude_us"]+1000, published_us=samples[0]["published_us"]+1000)
            rec.samples(0x102, [changed], 200)
            with contextlib.redirect_stderr(io.StringIO()):
                rec.finish("duration_completed", None, None)
            rec.close()
            meta = json.loads((rec.directory/"metadata.json").read_text())
            self.assertEqual(meta["streams"]["258"]["epoch_changes"], 1)
            self.assertIn("INS: epoch_changes=1", meta["quality_issues"])

    def test_invalid_duration(self):
        for value in ("nan", "inf", "-1"):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                capture.arguments(["--port", "fake", "--duration", value])


if __name__ == "__main__":
    unittest.main()
