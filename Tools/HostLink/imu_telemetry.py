#!/usr/bin/env python3
"""SRN-Link V2 dual IMU monitor. stdout only; no device storage commands."""
import argparse
import collections
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import time
import zlib

STREAMS = {0x100: "BMI088", 0x101: "LSM6DS3"}
ALL_STREAMS = {**STREAMS, 0x102: "INS", 0x103: "LIS2MDL", 0x104: "SPA06"}
STREAM_OPTIONS = dict(zip(("bmi", "lsm", "ins", "mag", "pressure"), ALL_STREAMS))
PERIODS = {0x100: 1000, 0x101: 1000, 0x102: 1000, 0x103: 10000, 0x104: 62500}
INS_RECORDS = {0x102: struct.Struct("<4B3I2Q4I15f"),
               0x103: struct.Struct("<BBHIIQQ4h4f"), 0x104: struct.Struct("<BBHIIQQ2i2f")}
PHASES = ("禁用", "探测", "等待温稳", "低动态观测", "收敛", "运行", "降级运行", "离线", "启动失败")
HEADER = struct.Struct("<6BHI")
RECORD = struct.Struct("<BBHIIQQ7hH7f")
STATUS = struct.Struct("<HBBIQHH6I")


def cobs_encode(raw):
    out = bytearray([0]); code_at = 0; code = 1
    for b in raw:
        if b == 0:
            out[code_at] = code; code_at = len(out); out.append(0); code = 1
        else:
            out.append(b); code += 1
            if code == 255:
                out[code_at] = code; code_at = len(out); out.append(0); code = 1
    out[code_at] = code
    return bytes(out)


def cobs_decode(data):
    out = bytearray(); i = 0
    while i < len(data):
        code = data[i]; i += 1
        if not code or i + code - 1 > len(data):
            raise ValueError("COBS block")
        out.extend(data[i:i+code-1]); i += code-1
        if code < 255 and i < len(data):
            out.append(0)
    return bytes(out)


def pack(service, message, sequence, payload=b"", kind=0):
    if len(payload) > 480:
        raise ValueError("payload too large")
    raw = HEADER.pack(0xA5, 2, kind, 0, service, message, len(payload), sequence) + payload
    return b"\0" + cobs_encode(raw + struct.pack("<I", zlib.crc32(raw))) + b"\0"


def unpack(data):
    raw = cobs_decode(data)
    if len(raw) < 16 or len(raw) > 496:
        raise ValueError("length")
    magic, version, kind, flags, service, message, size, seq = HEADER.unpack_from(raw)
    if magic != 0xA5 or version != 2 or kind > 2 or flags & ~1:
        raise ValueError("header")
    if size > 480 or size + 16 != len(raw):
        raise ValueError("length")
    if zlib.crc32(raw[:-4]) != struct.unpack_from("<I", raw, len(raw)-4)[0]:
        raise ValueError("CRC")
    return kind, service, message, seq, raw[12:-4]


class Decoder:
    def __init__(self):
        self.frame = bytearray(); self.started = False; self.dropping = False
        self.errors = collections.Counter()

    def feed(self, data):
        frames = []
        for b in data:
            if b == 0:
                if self.started:
                    if not self.frame and not self.dropping:
                        continue  # repeated leading delimiters keep the next frame armed
                    if self.frame and not self.dropping:
                        try:
                            frames.append(unpack(self.frame))
                        except ValueError as error:
                            self.errors[str(error)] += 1
                    self.started = False
                else:
                    self.started = True
                self.frame.clear(); self.dropping = False
            elif self.started and not self.dropping:
                if len(self.frame) >= 498:
                    self.dropping = True; self.errors["oversize"] += 1
                else:
                    self.frame.append(b)
        return frames


def decode_ins_samples(stream, payload, first_seq, device_us, period, count, size):
    codec = INS_RECORDS[stream]
    if not count or count > min(8, 432 // codec.size) or size != codec.size or len(payload) != 14 + count * size:
        raise ValueError("sample size")
    samples = []
    for i in range(count):
        r = codec.unpack_from(payload, 14 + i * size)
        if r[0] != 1:
            raise ValueError("record version")
        if stream == 0x102:
            if r[1] not in (5, 6) or not r[6] & 1 or not 0 < r[2] <= 3 or r[3] > 3:
                raise ValueError("attitude validity")
            values = r[13:]
            sample = dict(sequence=r[4], epoch=r[5], flags=r[6], phase=r[1], source_mask=r[2], new_sample_mask=r[3],
                          attitude_us=r[7], published_us=r[8], input_sequence=r[9:11], input_age_us=r[11:13],
                          weights=r[13:15], quaternion_wxyz=r[15:19], euler_rad=r[19:22],
                          gyro_rad_s=r[22:25], accel_m_s2=r[25:28])
        else:
            if r[2] or r[1] != 2:
                raise ValueError("record state/reserved")
            sample = dict(sequence=r[3], state=r[1], valid_fields=r[4], start_us=r[5], end_us=r[6])
            if stream == 0x103:
                values = r[11:15]
                sample.update(raw_magnetic=r[7:10], raw_temperature=r[10], magnetic_uT=r[11:14], temperature_c=r[14])
            else:
                values = r[9:11]
                sample.update(raw_pressure=r[7], raw_temperature=r[8], pressure_pa=r[9], temperature_c=r[10])
        if not all(math.isfinite(v) for v in values):
            raise ValueError("nonfinite sample")
        samples.append(sample)
    stamp = samples[0]["attitude_us" if stream == 0x102 else "end_us"]
    if first_seq != samples[0]["sequence"] or device_us != (stamp & 0xffffffff) or period != PERIODS[stream]:
        raise ValueError("sample metadata")
    return stream, samples


def decode_ins_status(payload):
    if len(payload) != 256 or payload[0] != 1 or payload[1] >= len(PHASES):
        raise ValueError("INS status version/size")
    flags, epoch, updated_us = struct.unpack_from("<IIQ", payload, 4)
    result = dict(phase=payload[1], healthy_mask=payload[2], heater_flags=payload[3], flags=flags, epoch=epoch, updated_us=updated_us)
    names = ("cycles", "publications", "dual_cycles", "single_cycles", "predicted_cycles", "missing_cycles", "recoveries",
             "missed_deadlines", "max_execution_us", "max_period_us", "max_copy_us")
    result.update(zip(names, struct.unpack_from("<11I", payload, 20)))
    result["heater_source"] = struct.unpack_from("<b", payload, 64)[0]
    result["hot_mask"] = payload[65]; result["sensor_states"] = list(payload[68:72])
    result.update(zip(("target_c", "feedback_c", "duty"), struct.unpack_from("<3f", payload, 72)))
    result.update(zip(("protection_count", "protection_recoveries", "source_selections"), struct.unpack_from("<3I", payload, 84)))
    result["board_to_robot_wxyz"] = struct.unpack_from("<4f", payload, 96)
    result["streams"] = {}
    for i in range(3):
        at = 112 + 48 * i; stream, enabled = struct.unpack_from("<HB", payload, at)
        entry = dict(stream=stream, enabled=bool(enabled))
        entry.update(zip(("sequence", "depth", "high_water", "enqueued", "overflow", "sent", "failed", "discarded", "capacity"),
                         struct.unpack_from("<9I", payload, at + 4)))
        result["streams"][stream] = entry
    return result


def decode_samples(payload):
    if len(payload) < 14:
        raise ValueError("sample header")
    stream, first_seq, device_us, period, count, size = struct.unpack_from("<HIIHBB", payload)
    if stream in INS_RECORDS:
        return decode_ins_samples(stream, payload, first_seq, device_us, period, count, size)
    if stream not in STREAMS or not count or count > 6 or size != 72 or len(payload) != 14+count*size:
        raise ValueError("sample size")
    samples = []
    for i in range(count):
        r = RECORD.unpack_from(payload, 14+i*size)
        if r[0] != 1 or r[2] or r[14] or r[1] > 6:
            raise ValueError("record version/state/reserved")
        values = r[15:22]
        if not all(math.isfinite(x) for x in values):
            raise ValueError("nonfinite sample")
        samples.append(dict(sequence=r[3], state=r[1], valid_fields=r[4],
                            start_us=r[5], end_us=r[6], raw_accel=r[7:10],
                            raw_gyro=r[10:13], raw_temperature=r[13],
                            accel_m_s2=values[:3], gyro_rad_s=values[3:6], temperature_c=values[6]))
    if first_seq != samples[0]["sequence"] or device_us != (samples[0]["start_us"] & 0xffffffff) or period != 1000:
        raise ValueError("sample metadata")
    return stream, samples


def decode_status(payload):
    if len(payload) != 92 or payload[:4] != b"\x01\x02\0\0":
        raise ValueError("status version/size")
    result = {}
    names = ("stream", "state", "enabled", "sequence", "last_us", "depth", "high_water",
             "enqueued", "overflow", "sent", "failed", "discarded", "samples")
    for i in range(2):
        entry = dict(zip(names, STATUS.unpack_from(payload, 4+i*44)))
        result[entry["stream"]] = entry
    return result


class Client:
    def __init__(self, port, on_samples=None, on_receive=None):
        self.port = port; self.decoder = Decoder(); self.sequence = 0
        self.on_samples = on_samples
        self.on_receive = on_receive
        self.replies = {}; self.counts = collections.Counter(); self.gaps = collections.Counter()
        self.duplicates = collections.Counter(); self.latest = {}; self.previous = {}

    def receive(self):
        data = self.port.read(max(1, min(self.port.in_waiting, 4096)))
        received_ns = time.monotonic_ns()
        if data and self.on_receive is not None:
            self.on_receive(data, received_ns)
        for kind, service, message, sequence, payload in self.decoder.feed(data):
            if kind == 1:
                self.replies[sequence] = (service, message, payload)
                if len(self.replies) > 32:
                    del self.replies[next(iter(self.replies))]
            elif (kind, service, message) == (2, 4, 2):
                try:
                    stream, samples = decode_samples(payload)
                except ValueError as error:
                    self.decoder.errors[str(error)] += 1; continue
                for sample in samples:
                    seq = sample["sequence"]
                    if stream in self.previous:
                        step = (seq - self.previous[stream]) & 0xffffffff
                        if step == 0 or step > 0x80000000:
                            self.duplicates[stream] += 1
                        elif step > 1:
                            self.gaps[stream] += step - 1
                    self.previous[stream] = seq; self.counts[stream] += 1; self.latest[stream] = sample
                if self.on_samples is not None:
                    self.on_samples(stream, samples, received_ns)

    def request(self, service, message, payload=b"", timeout=2):
        self.sequence = (self.sequence + 1) & 0xffffffff
        packet = pack(service, message, self.sequence, payload)
        # Retransmit the SAME sequence once: exercises idempotent request replay.
        for _ in range(2):
            self.port.write(packet)
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                self.receive()
                reply = self.replies.pop(self.sequence, None)
                if reply is None:
                    continue
                if reply[:2] != (service, message) or len(reply[2]) < 2:
                    raise ValueError("response identity")
                status, = struct.unpack_from("<H", reply[2])
                if status != 0:
                    raise ValueError(f"device status 0x{status:04x}")
                return reply[2][2:]
        raise TimeoutError(f"no response {service}/{message}")

    def subscribe(self, enabled=True, streams=None):
        for stream in (STREAMS if streams is None else streams):
            self.request(4, 1, struct.pack("<HIB", stream, PERIODS[stream], enabled))
            self.previous.pop(stream, None)

    def ins_status(self):
        return decode_ins_status(self.request(4, 9))

    def status(self):
        return decode_status(self.request(4, 8))


def open_port(path):
    import serial
    if shutil.which("lsof"):
        occupants = subprocess.run(["lsof", "-t", "--", path], capture_output=True, text=True).stdout.strip()
        if occupants:
            raise RuntimeError(f"port already in use by PID(s): {occupants}")
    port = serial.Serial(path, 115200, timeout=0.05, write_timeout=1, exclusive=True)
    port.dtr = True
    return port


def human_status(name, sample, status, rate, received, gaps):
    states = ("关闭", "初始化", "就绪", "离线", "不支持", "配置错误", "过期")
    state = status.get("state", -1)
    label = states[state] if 0 <= state < len(states) else str(state)
    temperature = "--"
    if sample is not None:
        temperature = f"{sample['temperature_c']:.2f} °C"
        if state != 2:
            temperature += "（旧值）"
    return (f"{name:8s} | 温度 {temperature:>10s} | {label} | {rate:7.1f} Hz | "
            f"收到 {received:,} | 缺序 {gaps} | 设备溢出累计 {status.get('overflow', 0)} | "
            f"发送失败累计 {status.get('failed', 0)}")


def human_ins_status(status):
    source = {0: "BMI088", 1: "LSM6DS3", -1: "无"}.get(status["heater_source"], "未知")
    return (f"INS {PHASES[status['phase']]} | epoch {status['epoch']} | 温度 {status['feedback_c']:.2f} / {status['target_c']:.1f} °C "
            f"({source}) | PWM {status['duty']:.1%} | 温稳 {'是' if status['heater_flags'] & 2 else '否'} "
            f"| 过温保护 {'是' if status['heater_flags'] & 4 else '否'} | flags 0x{status['flags']:x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="CDC1 device path; never auto-selects a port")
    parser.add_argument("--json", action="store_true", help="machine-readable console output")
    parser.add_argument("--duration", type=float, default=0, help="seconds; 0 runs until Ctrl-C")
    parser.add_argument("--streams", nargs="+", choices=STREAM_OPTIONS, default=["bmi", "lsm"],
                        help="optional raw/INS/environment streams; raw defaults and capture CSV remain unchanged")
    args = parser.parse_args()
    selected = {STREAM_OPTIONS[name]: ALL_STREAMS[STREAM_OPTIONS[name]] for name in args.streams}
    needs_ins = any(s >= 0x102 for s in selected)
    start = time.monotonic(); deadline = start + args.duration if args.duration else float("inf")
    try:
        while time.monotonic() < deadline:
            port = None; client = None
            try:
                port = open_port(args.port); client = Client(port)
                client.request(0, 1); client.subscribe(streams=selected)
                print("Subscribed " + ", ".join(selected.values()) + "; raw IMU in sensor axes, INS/environment SI in robot axes; no storage commands.", flush=True)
                last = window_start = time.monotonic(); counts = collections.Counter(); window_counts = collections.Counter()
                status = client.status(); ins_status = client.ins_status() if needs_ins else None
                if ins_status: status.update(ins_status["streams"])
                window_acquired = {s: (ins_status["publications"] if s == 0x102 else status[s].get("samples", status[s]["sequence"])) for s in selected}
                while time.monotonic() < deadline:
                    client.receive(); now = time.monotonic()
                    if now - last >= 1:
                        status = client.status()
                        ins_status = client.ins_status() if needs_ins else None
                        if ins_status:
                            status.update(ins_status["streams"])
                            if not args.json: print(human_ins_status(ins_status), flush=True)
                        for stream, name in selected.items():
                            sample = client.latest.get(stream)
                            rate = (client.counts[stream]-counts[stream])/(now-last)
                            if args.json:
                                print(json.dumps(dict(device=name, rate_hz=rate,
                                    received=client.counts[stream], gaps=client.gaps[stream], duplicates=client.duplicates[stream],
                                    device_status=status[stream], ins_status=ins_status, latest=sample), separators=(",", ":")), flush=True)
                            elif stream >= 0x102:
                                print(f"{name:8s} | {rate:7.1f} Hz | 收到 {client.counts[stream]:,} | 缺序 {client.gaps[stream]} | "
                                      f"溢出 {status[stream]['overflow']} | 发送失败 {status[stream]['failed']}", flush=True)
                                if sample and stream == 0x102:
                                    print("  欧拉角 rad: " + " / ".join(f"{v:+.5f}" for v in sample["euler_rad"]) +
                                          f"  q(wxyz)={sample['quaternion_wxyz']} 来源={sample['source_mask']} 权重={sample['weights']}", flush=True)
                                elif sample and stream == 0x103:
                                    print(f"  磁场 µT={sample['magnetic_uT']} 温度={sample['temperature_c']:.2f} °C", flush=True)
                                elif sample:
                                    print(f"  气压={sample['pressure_pa']:.2f} Pa 温度={sample['temperature_c']:.2f} °C", flush=True)
                            else:
                                print(human_status(name, sample, status[stream], rate, client.counts[stream], client.gaps[stream]), flush=True)
                                if sample:
                                    print("  加速度 m/s²: " + " / ".join(f"{v:+.5f}" for v in sample["accel_m_s2"]) +
                                          "   角速度 rad/s: " + " / ".join(f"{v:+.6f}" for v in sample["gyro_rad_s"]), flush=True)
                                    print(f"  原始 acc={sample['raw_accel']} gyro={sample['raw_gyro']} temp={sample['raw_temperature']}", flush=True)
                        if now-window_start >= 10 and args.json:
                            print(json.dumps(dict(window_seconds=now-window_start,
                                received_hz={ALL_STREAMS[s]:(client.counts[s]-window_counts[s])/(now-window_start) for s in selected},
                                acquired_hz={ALL_STREAMS[s]:(((ins_status["publications"] if s == 0x102 else status[s].get("samples", status[s]["sequence"]))-window_acquired[s])&0xffffffff)/(now-window_start) for s in selected},
                                protocol_errors=dict(client.decoder.errors))), flush=True)
                            window_start=now; window_counts=client.counts.copy()
                            window_acquired={s:(ins_status["publications"] if s == 0x102 else status[s].get("samples", status[s]["sequence"])) for s in selected}
                        counts=client.counts.copy(); last=now
            except (OSError, TimeoutError, ValueError) as error:
                print(f"connection: {error}; will reconnect", file=sys.stderr, flush=True)
                time.sleep(min(1,max(0,deadline-time.monotonic())))
            finally:
                if client and port and port.is_open:
                    try:
                        client.subscribe(False, streams=selected)
                    except (OSError, TimeoutError, ValueError):
                        pass
                if port:
                    port.close()
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
