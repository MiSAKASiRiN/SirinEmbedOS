#!/usr/bin/env python3
"""SystemMonitor base V2 / cycles V1: explicit CDC1, 1 Hz, read-only telemetry; optional JSONL capture."""
import argparse
import collections
import json
import math
from pathlib import Path
import struct
import sys
import time
try:
    from .imu_telemetry import Decoder, pack, open_port
except ImportError:  # existing standalone script entry
    from imu_telemetry import Decoder, pack, open_port

STREAMS = {1: "tasks", 2: "adc", 3: "cycles"}
ADC_NAMES = ("current_a", "voltage_v", "vbat_v", "vrefint_v", "temperature_c", "vdda_v")
COUNTERS = ("errors", "timeouts", "recoveries", "busy_ppm", "idle_ppm", "monitor_cpu_ppm", "task_count",
            "max_critical_us", "max_wake_late_us", "skipped_cycles", "slot_overflows")


def decode(payload):
    if len(payload) < 14:
        raise ValueError("short sample header")
    stream, sequence, device_us, period, count, size = struct.unpack_from("<HIIHBB", payload)
    expected = {1: 112, 2: 128, 3: 112}.get(stream)
    if expected is None or size != expected or not 1 <= count <= (1 if stream == 2 else 4) or period or len(payload) != 14+size*count:
        raise ValueError("monitor sample shape")
    records = []
    for i in range(count):
        b = payload[14+i*size:14+(i+1)*size]
        if b[0] != (1 if stream == 3 else 2):
            raise ValueError("monitor version")
        if stream == 2:
            if b[1] > 4 or b[2:4] != b"\0\0" or b[58:60] != b"\0\0":
                raise ValueError("ADC state/reserved")
            snapshot_seq, flags, adc_seq = struct.unpack_from("<3I", b, 4)
            updated, start, end = struct.unpack_from("<3Q", b, 16)
            valid, quality = struct.unpack_from("<2I", b, 40)
            values = struct.unpack_from("<6f", b, 60)
            if not all(math.isfinite(v) for v in values):
                raise ValueError("nonfinite ADC value")
            r = dict(snapshot_sequence=snapshot_seq, flags=flags, adc_sequence=adc_seq, state=b[1],
                     updated_us=updated, start_us=start, completed_us=end, valid_fields=valid, quality=quality,
                     raw=struct.unpack_from("<5H", b, 48), **dict(zip(ADC_NAMES, values)))
            r.update(zip(COUNTERS, struct.unpack_from("<11I", b, 84)))
            r["age_us"] = max(0, updated-end) if adc_seq else None
        elif stream == 3:
            if b[1] > 64 or b[3] or b[108:112] != b"\0"*4:
                raise ValueError("cycle slot/reserved")
            names=("period_ms", "completed", "jitter_samples", "execution_min_us", "execution_mean_us",
                   "execution_max_us", "start_late_mean_us", "start_late_max_us", "jitter_min_us",
                   "jitter_max_us", "jitter_abs_mean_us", "deadline_misses", "skipped", "hook_mean_ns", "hook_max_ns")
            r=dict(slot=b[1], flags=b[2], generation=struct.unpack_from("<I", b, 4)[0],
                   name=b[8:24].split(b"\0", 1)[0].decode("utf-8", errors="replace"))
            r["sampled_us"], r["window_us"] = struct.unpack_from("<QQ", b, 24)
            r.update(zip(names, struct.unpack_from("<8I2i5I", b, 40)))
            r["updated_us"], = struct.unpack_from("<Q", b, 100)
            r["age_us"] = max(0, r["updated_us"]-r["sampled_us"]) if r["sampled_us"] else None
        else:
            if b[1] > 64:
                raise ValueError("task slot")
            generation, snapshot_seq = struct.unpack_from("<2I", b, 4)
            stack, = struct.unpack_from("<H", b, 14)
            sampled, window, runtime = struct.unpack_from("<3Q", b, 16)
            r = dict(slot=b[1], flags=b[2], state=b[3], generation=generation, snapshot_sequence=snapshot_seq,
                     priority=b[12], base_priority=b[13], stack_free_words=None if stack == 65535 else stack,
                     sampled_us=sampled, window_us=window, runtime_us=runtime,
                     name=b[60:76].split(b"\0", 1)[0].decode("utf-8", errors="replace"))
            r.update(zip(("cpu_ppm", "switch_count", "ready_count", "max_run_us", "max_ready_us"),
                         struct.unpack_from("<5I", b, 40)))
            r["updated_us"], = struct.unpack_from("<Q", b, 76)
            r.update(zip(("system_flags", "busy_ppm", "idle_ppm", "monitor_cpu_ppm", "task_count", "max_critical_us", "slot_overflows"),
                         struct.unpack_from("<7I", b, 84)))
            r["age_us"] = max(0, r["updated_us"]-sampled)
        if device_us != r["updated_us"] & 0xffffffff:
            raise ValueError("timestamp mismatch")
        records.append(r)
    return dict(stream=STREAMS[stream], report_sequence=sequence, records=records)


def human(report):
    lines = []
    for r in report["records"]:
        if report["stream"] == "adc":
            fields = []
            for bit, name, unit in zip((1, 2, 4, 8, 16, 32), ADC_NAMES, ("A", "V", "V", "V", "°C", "V")):
                value = f"{r[name]:.3f} {unit}" if r["valid_fields"] & bit else "无效/旧值"
                fields.append(f"{name}={value}")
            lines.append(" | ".join(fields) + f" | age={r['age_us']} µs quality=0x{r['quality']:x} errors={r['errors']}")
        elif report["stream"] == "cycles":
            measured=bool(r["flags"] & 4) and not bool(r["flags"] & 16)
            execution=f"{r['execution_min_us']}/{r['execution_mean_us']}/{r['execution_max_us']} µs" if measured else "未测量/过期"
            jitter=f"{r['jitter_min_us']}..{r['jitter_max_us']} µs" if r["flags"] & 8 and measured else "不适用/未测量"
            periodic=bool(r["flags"] & 2)
            late=f"{r['start_late_max_us']} µs" if periodic and measured else "不适用/未测量"
            misses=r["deadline_misses"] if periodic else "不适用"
            skipped=r["skipped"] if periodic else "不适用"
            hook=f"{r['hook_mean_ns']}/{r['hook_max_ns']} ns" if measured else "未测量/过期"
            lines.append(f"cycle {r['slot']}:{r['generation']} {r['name']} wall(min/mean/max)={execution} "
                         f"jitter={jitter} late_max={late} miss={misses} skip={skipped} hook={hook} "
                         f"window={r['window_us']} µs age={r['age_us']} µs flags=0x{r['flags']:x}")
        else:
            cpu = f"{r['cpu_ppm']/10000:.4f}%" if r["flags"] & 2 else "未测量/过期"
            lines.append(f"task {r['slot']}:{r['generation']} {r['name']} CPU={cpu} "
                         f"slice_max={r['max_run_us']} µs ready_max={r['max_ready_us']} µs "
                         f"window={r['window_us']} µs age={r['age_us']} µs stack=未测量")
    return "\n".join(lines)


class DeviceStatus(ValueError):
    def __init__(self, status):
        self.status = status
        super().__init__(f"device status 0x{status:04x}")


class Client:
    def __init__(self, port, receive_report):
        self.port = port
        self.decoder = Decoder()
        self.replies = {}
        self.sequence = 0
        self.receive_report = receive_report
        self.counts = collections.Counter()
        self.active_streams = set()
        self.supported_streams = set()

    def receive(self):
        data = self.port.read(max(1, min(self.port.in_waiting, 4096)))
        for kind, service, message, seq, payload in self.decoder.feed(data):
            if kind == 1:
                self.replies[seq] = (service, message, payload)
            elif (kind, service, message) == (2, 4, 2):
                # Other subscriptions may be active: do not decode IMU streams.
                if len(payload) >= 2 and struct.unpack_from("<H", payload)[0] not in STREAMS:
                    continue
                try:
                    report = decode(payload)
                except ValueError as error:
                    self.decoder.errors[str(error)] += 1
                    continue
                report["received_monotonic_ns"] = time.monotonic_ns()
                self.counts[report["stream"]] += 1
                self.receive_report(report)

    def request(self, service, message, payload=b""):
        self.sequence += 1
        packet = pack(service, message, self.sequence, payload)
        for _ in range(2):
            self.port.write(packet)
            deadline = time.monotonic()+2
            while time.monotonic() < deadline:
                self.receive()
                reply = self.replies.pop(self.sequence, None)
                if reply is not None:
                    if reply[:2] != (service, message) or len(reply[2]) < 2:
                        raise ValueError("response identity")
                    status, = struct.unpack_from("<H", reply[2])
                    if status:
                        raise DeviceStatus(status)
                    return
        raise TimeoutError("device response timeout")

    def subscribe(self, enabled):
        for stream in (STREAMS if enabled else sorted(self.active_streams)):
            try:
                self.request(4, 1, struct.pack("<HIB", stream, 1000000, enabled))
            except DeviceStatus as error:
                if enabled and stream == 3 and error.status == 0x0109:
                    continue  # Older firmware retains the two base streams.
                raise
            if enabled:
                self.active_streams.add(stream)
                self.supported_streams.add(stream)
            else:
                self.active_streams.discard(stream)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="CDC1 device path")
    parser.add_argument("--duration", type=float, default=0, help="seconds, 0 until Ctrl-C")
    parser.add_argument("--output", type=Path, help="new directory for records.jsonl and metadata.json")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    if args.duration < 0 or not math.isfinite(args.duration):
        parser.error("duration must be finite and nonnegative")
    log = port = client = None
    meta = dict(complete=False, port=args.port, units="A,V,°C,us,CPU ppm", stack="not measured",
                timing="CPU includes ISR attribution; cycle wall time includes preemption, blocking and ISR; waits/init/cleanup excluded",
                hook_cost="diagnostic estimate excluding final accounting; board benchmark required")
    try:
        if args.output:
            args.output.mkdir(parents=True, exist_ok=False)
            log = (args.output/"records.jsonl").open("x", encoding="utf-8")
        def report(value):
            if log:
                log.write(json.dumps(value, ensure_ascii=False, allow_nan=False)+"\n")
                log.flush()
            print(json.dumps(value, ensure_ascii=False) if args.json else human(value), flush=True)
        port = open_port(args.port)
        client = Client(port, report)
        client.request(0, 1)
        client.subscribe(True)
        start = time.monotonic()
        while not args.duration or time.monotonic()-start < args.duration:
            client.receive()
        meta["complete"] = True
    except KeyboardInterrupt:
        meta["interrupted"] = True
    except Exception as error:
        meta["error"] = str(error)
        print(str(error), file=sys.stderr)
    finally:
        if client:
            try:
                client.subscribe(False)
            except Exception as error:
                meta["unsubscribe_error"] = str(error)
            meta["reports"] = dict(client.counts)
            meta["decode_errors"] = dict(client.decoder.errors)
            meta["supported_streams"] = [STREAMS[s] for s in sorted(client.supported_streams)]
            if not client.supported_streams or not all(client.counts[STREAMS[s]] for s in client.supported_streams) or client.decoder.errors:
                meta["complete"] = False
        if port:
            port.close()
        if log:
            log.close()
            (args.output/"metadata.json").write_text(json.dumps(meta, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    return 0 if meta["complete"] or meta.get("interrupted") else 1


if __name__ == "__main__":
    sys.exit(main())
