#!/usr/bin/env python3
"""Record selected raw IMU, INS and environmental samples from CDC1 to CSV."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import sys
import time

from imu_telemetry import (Client, STREAMS, ALL_STREAMS, STREAM_OPTIONS, PERIODS,
                           open_port, human_status, human_ins_status)

FILES = {0x100: "bmi088.csv", 0x101: "lsm6ds3.csv"}
FIELDS = ["phase", "host_receive_monotonic_ns", "sequence", "state", "valid_fields",
          "acquisition_start_us", "acquisition_end_us",
          "raw_accel_x", "raw_accel_y", "raw_accel_z",
          "raw_gyro_x", "raw_gyro_y", "raw_gyro_z", "raw_temperature",
          "accel_x_m_s2", "accel_y_m_s2", "accel_z_m_s2",
          "gyro_x_rad_s", "gyro_y_rad_s", "gyro_z_rad_s", "temperature_c"]


ALL_FILES = {**FILES, 0x102: "ins.csv", 0x103: "lis2mdl.csv", 0x104: "spa06.csv"}
INS_FIELDS = ["phase", "host_receive_monotonic_ns", "sequence", "epoch", "flags",
    "ins_phase", "source_mask", "new_sample_mask", "attitude_us", "published_us",
    "bmi_sequence", "lsm_sequence", "bmi_age_us", "lsm_age_us", "bmi_weight", "lsm_weight",
    "quaternion_w", "quaternion_x", "quaternion_y", "quaternion_z",
    "roll_rad", "pitch_rad", "yaw_rad", "gyro_x_rad_s", "gyro_y_rad_s", "gyro_z_rad_s",
    "accel_x_m_s2", "accel_y_m_s2", "accel_z_m_s2"]
SCHEMAS = {0x100: FIELDS, 0x101: FIELDS, 0x102: INS_FIELDS,
    0x103: FIELDS[:7] + ["raw_magnetic_x", "raw_magnetic_y", "raw_magnetic_z", "raw_temperature",
                         "magnetic_x_uT", "magnetic_y_uT", "magnetic_z_uT", "temperature_c"],
    0x104: FIELDS[:7] + ["raw_pressure", "raw_temperature", "pressure_pa", "temperature_c"]}


def sample_row(stream, sample, phase, now_ns):
    prefix = [phase, now_ns, sample["sequence"]]
    if stream == 0x102:
        return prefix + [sample["epoch"], sample["flags"], sample["phase"], sample["source_mask"],
            sample["new_sample_mask"], sample["attitude_us"], sample["published_us"],
            *sample["input_sequence"], *sample["input_age_us"], *sample["weights"],
            *sample["quaternion_wxyz"], *sample["euler_rad"], *sample["gyro_rad_s"], *sample["accel_m_s2"]]
    prefix += [sample["state"], sample["valid_fields"], sample["start_us"], sample["end_us"]]
    if stream in STREAMS:
        return prefix + [*sample["raw_accel"], *sample["raw_gyro"], sample["raw_temperature"],
                         *sample["accel_m_s2"], *sample["gyro_rad_s"], sample["temperature_c"]]
    if stream == 0x103:
        return prefix + [*sample["raw_magnetic"], sample["raw_temperature"],
                         *sample["magnetic_uT"], sample["temperature_c"]]
    return prefix + [sample["raw_pressure"], sample["raw_temperature"], sample["pressure_pa"], sample["temperature_c"]]


def capture_status(client, streams):
    result = client.status() if any(s in STREAMS for s in streams) else {}
    if any(s >= 0x102 for s in streams):
        ins = client.ins_status()
        result.update({s: dict(v) for s, v in ins["streams"].items()})
        for stream, index in ((0x103, 2), (0x104, 3)):
            result[stream]["state"] = ins["sensor_states"][index]
        result["ins_service"] = ins
    return result


def utc_now():
    return datetime.now(timezone.utc).isoformat()


def save_json(path, value):
    """Atomic metadata replacement; sample files are append-only within a new run."""
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8") as f:
        json.dump(value, f, indent=2, ensure_ascii=False, allow_nan=False)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    temporary.replace(path)


class Recorder:
    def __init__(self, directory, warmup, duration, metadata, streams=None):
        self.streams = dict(STREAMS if streams is None else streams)
        self.epochs = {}
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=False)
        self.warmup = warmup
        self.duration = duration
        self.start_ns = None
        self.stopping = False
        self.files = {}
        self.writers = {}
        self.last = {}
        self.first_status = self.last_status = None
        self.stats = {s: dict(rows=0, measurement_rows=0, gaps=0, duplicates=0,
                             out_of_order=0, time_regressions=0, invalid=0, epoch_changes=0,
                             first_device_us=None, last_device_us=None) for s in self.streams}
        self.metadata = dict(metadata, format_version=2 if any(s >= 0x102 for s in self.streams) else 1, started_utc=utc_now(),
                             complete=False, result="in_progress",
                             fields=FIELDS, files={s: ALL_FILES[s] for s in self.streams},
                             fields_by_stream={s: SCHEMAS[s] for s in self.streams},
                             selected_streams=self.streams, warmup_seconds=warmup,
                             duration_seconds=duration,
                             timestamp_contract="Device us are acquisition times for measurements and attitude/publication times for INS; host ns are read-batch arrival times, not sample times.",
                             coordinates="Raw IMU streams: sensor axes. INS and environment SI vectors: robot axes; raw integers: sensor axes. No resampling.")
        save_json(self.directory / "metadata.json", self.metadata)
        try:
            for stream in self.streams:
                name = ALL_FILES[stream]
                f = (self.directory / name).open("x", newline="", encoding="utf-8", buffering=262144)
                self.files[stream] = f
                self.writers[stream] = csv.writer(f)
                self.writers[stream].writerow(SCHEMAS[stream])
            self.raw = (self.directory / "cdc1_rx.bin").open("xb", buffering=262144)
            self.status_file = (self.directory / "status.jsonl").open("x", encoding="utf-8")
        except BaseException:
            self.close()
            raise

    def start(self):
        self.start_ns = time.monotonic_ns()
        self.metadata["subscription_start_monotonic_ns"] = self.start_ns
        save_json(self.directory / "metadata.json", self.metadata)

    def phase(self, now_ns):
        if self.stopping:
            return "stopping"
        if self.start_ns is None:
            return "setup"
        elapsed = (now_ns - self.start_ns) / 1e9
        if elapsed < self.warmup:
            return "warmup"
        if self.duration and elapsed >= self.warmup + self.duration:
            return "stopping"
        return "measurement"

    def received(self, data, now_ns):
        self.raw.write(data)

    def samples(self, stream, samples, now_ns):
        if stream not in self.streams:
            return
        phase = self.phase(now_ns)
        stats = self.stats[stream]
        for sample in samples:
            seq = sample["sequence"]
            start = sample["attitude_us" if stream == 0x102 else "start_us"]
            if stream == 0x102:
                epoch = sample["epoch"]
                if stream in self.epochs and epoch != self.epochs[stream]:
                    stats["epoch_changes"] += 1
                self.epochs[stream] = epoch
            if stream in self.last:
                previous_seq, previous_start = self.last[stream]
                step = (seq - previous_seq) & 0xffffffff
                if step == 0:
                    stats["duplicates"] += 1
                elif step > 0x80000000:
                    stats["out_of_order"] += 1
                elif step > 1:
                    stats["gaps"] += step - 1
                if start <= previous_start:
                    stats["time_regressions"] += 1
            self.last[stream] = seq, start
            if stream == 0x102:
                invalid = not (sample["flags"] & 1) or sample["published_us"] < start
            else:
                required = {0x100: 0x13, 0x101: 0x13, 0x103: 0x14, 0x104: 0x18}[stream]
                invalid = (sample["state"] != 2 or sample["valid_fields"] & required != required
                           or sample["end_us"] < start)
            stats["invalid"] += int(invalid)
            row = sample_row(stream, sample, phase, now_ns)
            self.writers[stream].writerow(row)
            stats["rows"] += 1
            if phase == "measurement":
                stats["measurement_rows"] += 1
                if stats["first_device_us"] is None:
                    stats["first_device_us"] = start
                stats["last_device_us"] = start

    def status(self, kind, value):
        if self.first_status is None:
            self.first_status = value
        self.last_status = value
        self.status_file.write(json.dumps(dict(kind=kind, utc=utc_now(),
            host_monotonic_ns=time.monotonic_ns(), device=value), allow_nan=False) + "\n")

    def flush(self, durable=False):
        for f in [*self.files.values(), getattr(self, "raw", None), getattr(self, "status_file", None)]:
            if f is not None and not f.closed:
                f.flush()
                if durable:
                    os.fsync(f.fileno())

    def finish(self, result, error, client):
        self.flush(durable=True)
        issues = []
        for stream, stats in self.stats.items():
            if not stats["measurement_rows"]:
                issues.append(f"{self.streams[stream]}: no measurement samples")
            for key in ("gaps", "duplicates", "out_of_order", "time_regressions", "invalid", "epoch_changes"):
                if stats[key]:
                    issues.append(f"{self.streams[stream]}: {key}={stats[key]}")
            first, last = stats["first_device_us"], stats["last_device_us"]
            stats["measurement_rate_hz"] = ((stats["measurement_rows"] - 1) * 1e6 / (last - first)
                if first is not None and last > first else None)
            if self.first_status and self.last_status:
                for key in ("overflow", "failed"):
                    delta = (self.last_status[stream][key] - self.first_status[stream][key]) & 0xffffffff
                    if delta:
                        issues.append(f"{self.streams[stream]}: device {key} delta={delta} (check resets)")
                if self.last_status[stream].get("state", 2) != 2:
                    issues.append(f"{self.streams[stream]}: last device state={self.last_status[stream]['state']}")
        if client and client.decoder.errors:
            issues.append("protocol errors; see protocol_errors")
        summary = dict(self.metadata, finished_utc=utc_now(), result=result,
                       complete=result == "duration_completed", error=error,
                       quality_issues=issues,
                       first_device_status=self.first_status, last_device_status=self.last_status,
                       streams=self.stats, protocol_errors=dict(client.decoder.errors) if client else {})
        save_json(self.directory / "metadata.json", summary)
        if issues:
            print("Quality issues: " + "; ".join(issues), file=sys.stderr, flush=True)

    def close(self):
        for f in [*self.files.values(), getattr(self, "raw", None), getattr(self, "status_file", None)]:
            if f is not None:
                f.close()


def seconds(value):
    number = float(value)
    if not math.isfinite(number) or number < 0:
        raise argparse.ArgumentTypeError("must be finite and non-negative")
    return number


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="machine-readable console output")
    parser.add_argument("--port", required=True, help="explicit CDC1 port; never auto-selects")
    parser.add_argument("--duration", type=seconds, default=600, help="measurement seconds AFTER warmup; 0 until Ctrl-C")
    parser.add_argument("--warmup", type=seconds, default=0, help="initial seconds marked warmup (also saved)")
    parser.add_argument("--output", type=Path, help="new directory; refuses to overwrite existing captures")
    parser.add_argument("--note", default="", help="mounting, vibration, room conditions, etc.")
    parser.add_argument("--streams", nargs="+", choices=STREAM_OPTIONS, default=["bmi", "lsm"],
                        help="streams to save; default preserves the two raw IMU CSVs")
    parser.add_argument("--elf", type=Path, help="optional ELF to hash as provenance; does not flash or verify target")
    return parser.parse_args(argv)


def main(argv=None):
    args = arguments(argv)
    selected = {STREAM_OPTIONS[n]: ALL_STREAMS[STREAM_OPTIONS[n]] for n in args.streams}
    output = args.output or Path("captures") / datetime.now(timezone.utc).strftime("imu-%Y%m%d-%H%M%S-%fZ")
    metadata = dict(port=args.port, note=args.note, nominal_period_us=1000 if all(s <= 0x102 for s in selected) else None,
                    protocol="SRN-Link V2 / record v1 / LE",
                    selected_periods_us={s: PERIODS[s] for s in selected},
                    firmware="Unknown: telemetry does not identify the running ELF or read back full sensor configuration.",
                    script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    decoder_sha256=hashlib.sha256(Path(__file__).with_name("imu_telemetry.py").read_bytes()).hexdigest())
    recorder = port = client = None
    result, error, code = "failed", None, 1
    old_sigterm = signal.getsignal(signal.SIGTERM)

    def stop(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    try:
        if args.elf:
            metadata["provided_elf"] = dict(path=str(args.elf.resolve()),
                sha256=hashlib.sha256(args.elf.read_bytes()).hexdigest(), verified_against_target=False)
        recorder = Recorder(output, args.warmup, args.duration, metadata, selected)
        print(f"Saving full samples to: {output.resolve()}", flush=True)
        port = open_port(args.port)
        client = Client(port, on_samples=recorder.samples, on_receive=recorder.received)
        hello = client.request(0, 1)
        recorder.metadata["hello_body_hex"] = hello.hex()
        recorder.status("before_subscription", capture_status(client, selected))
        recorder.start()
        client.subscribe(streams=selected)
        next_status = time.monotonic() + 1
        end = recorder.start_ns / 1e9 + args.warmup + args.duration if args.duration else float("inf")
        previous_counts = {s: 0 for s in selected}
        previous_time = time.monotonic()
        while time.monotonic() < end:
            client.receive()
            now = time.monotonic()
            if now >= next_status:
                status = capture_status(client, selected)
                recorder.status("periodic", status)
                recorder.flush()
                now = time.monotonic()
                dt = now - previous_time
                rates = {selected[s]: round((recorder.stats[s]["rows"] - previous_counts[s]) / dt, 1) for s in selected}
                health = {selected[s]: dict(state=status[s].get("state"), overflow=status[s]["overflow"],
                          failed=status[s]["failed"], gaps=recorder.stats[s]["gaps"]) for s in selected}
                elapsed = (time.monotonic_ns()-recorder.start_ns)/1e9
                phase = recorder.phase(time.monotonic_ns())
                for stream, name in selected.items():
                    sample = client.latest.get(stream)
                    health[name]["temperature_c"] = sample.get("temperature_c") if sample else None
                if args.json:
                    print(json.dumps(dict(phase=phase, elapsed_s=round(elapsed, 1),
                        received_hz=rates, health=health, ins_status=status.get("ins_service"), errors=dict(client.decoder.errors))), flush=True)
                else:
                    phase_label = {"warmup": "预热记录", "measurement": "正式采集"}.get(phase, phase)
                    print(f"\n[{int(elapsed)//60:02d}:{int(elapsed)%60:02d}] {phase_label} | 协议错误 {sum(client.decoder.errors.values())}", flush=True)
                    if "ins_service" in status:
                        print(human_ins_status(status["ins_service"]), flush=True)
                    for stream, name in selected.items():
                        sample = client.latest.get(stream)
                        if stream != 0x102:
                            line = human_status(name, sample, status[stream], rates[name],
                                recorder.stats[stream]["rows"], recorder.stats[stream]["gaps"])
                        else:
                            angles = sample["euler_rad"] if sample else None
                            line = (f"INS | {rates[name]:.1f} Hz | 欧拉角 rad {angles} | "
                                    f"缺序 {recorder.stats[stream]['gaps']} | 设备溢出累计 {status[stream]['overflow']}")
                        print(line + f" | 正式样本 {recorder.stats[stream]['measurement_rows']:,}", flush=True)
                previous_counts = {s: recorder.stats[s]["rows"] for s in selected}
                previous_time, next_status = now, now + 1
        result, code = "duration_completed", 0
    except KeyboardInterrupt:
        result, code = "interrupted", 130
    except (OSError, RuntimeError, ValueError, TimeoutError, ImportError) as exc:
        error = str(exc)
        print(f"Capture stopped: {exc}", file=sys.stderr)
    finally:
        if recorder:
            recorder.stopping = True
        # Do not auto-reconnect: separate sessions must not look continuous.
        try:
            if client and port and port.is_open:
                client.subscribe(False, streams=selected)
                recorder.status("after_unsubscribe", capture_status(client, selected))
        except (OSError, RuntimeError, ValueError, TimeoutError, KeyboardInterrupt) as exc:
            error = (error + "; " if error else "") + f"cleanup: {exc}"
            if result == "duration_completed":
                result, code = "cleanup_failed", 1
        finally:
            if port:
                try:
                    port.close()
                except OSError as exc:
                    error = (error + "; " if error else "") + f"close: {exc}"
                    if result == "duration_completed":
                        result, code = "cleanup_failed", 1
            if recorder:
                try:
                    recorder.finish(result, error, client)
                    print(f"{result}: {output.resolve()} | " + ", ".join(
                        f"{selected[s]}={recorder.stats[s]['measurement_rows']} measurement rows" for s in selected), flush=True)
                finally:
                    recorder.close()
            signal.signal(signal.SIGTERM, old_sigterm)
    return code


if __name__ == "__main__":
    sys.exit(main())
