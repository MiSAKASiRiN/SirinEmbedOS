"""Independent wire vectors shared with the firmware C encoder test."""
from pathlib import Path
import sys
import struct
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "Tools/HostLink"))
import imu_telemetry as m
root = Path(__file__).parent
expected = {
    0x102: ("golden_ins.hex", 0x12345678, 0x55667788, 1000),
    0x103: ("golden_mag.hex", 5, 1100, 10000),
    0x104: ("golden_pressure.hex", 6, 2100, 62500),
}
for stream, (name, seq, stamp, period) in expected.items():
    raw = bytes.fromhex((root/name).read_text())
    packet = struct.pack("<HIIHBB", stream, seq, stamp, period, 1, len(raw)) + raw
    sid, samples = m.decode_samples(packet)
    assert sid == stream and len(samples) == 1 and samples[0]["sequence"] == seq
    if stream == 0x102:
        assert samples[0]["quaternion_wxyz"] == (1, 0, 0, 0)
        assert samples[0]["gyro_rad_s"] == (.125, -.25, .5)
        assert samples[0]["input_age_us"] == (100, 200)
    elif stream == 0x103:
        assert samples[0]["raw_magnetic"] == (-32768, -123, 32767)
        assert samples[0]["temperature_c"] == 23.5
    else:
        assert samples[0]["raw_pressure"] == -123456 and samples[0]["pressure_pa"] == 101325
    for bad in (packet[:-1], packet[:14]+b'\2'+packet[15:]):
        try: m.decode_samples(bad)
        except ValueError: pass
        else: raise AssertionError("malformed record accepted")
status = bytearray(256);status[0:4] = bytes((1, 5, 3, 3));status[64]=255
struct.pack_into("<3f", status, 72, 60, 59.75, .25)
s = m.decode_ins_status(status)
assert s["heater_source"] == -1 and s["feedback_c"] == 59.75 and s["duty"] == .25
assert "59.75" in m.human_ins_status(s)
assert m.STREAMS == {0x100: "BMI088", 0x101: "LSM6DS3"} # capture compatibility
print("INS CLI: shared vectors, explicit LE signed fields, status and raw capture compatibility passed")
