#!/usr/bin/env python3
"""A35 client for the MP257 latest-value devices (ABI v1)."""
import argparse
import contextlib
import json
import os
import struct
import sys

READ = struct.Struct("<65I")
REQUEST = struct.Struct("<19I")
STATUS_OFFSET = 256


def snapshot(fd, offset=0):
    """Try the most recent complete publication. The kernel never waits for a frame."""
    for _ in range(4):
        try:
            raw = os.pread(fd, READ.size, offset)
            return READ.unpack(raw)
        except BlockingIOError:
            continue
    raise BlockingIOError("snapshot changed during all four attempts")


def radio(fd):
    sample = snapshot(fd)
    # A frame IRQ may precede the M0 main loop's next clock publication by one tick.
    delta = (sample[-1] - sample[1]) & 0xffffffff
    age = 0 if delta == 0xffffffff else delta
    flags = sample[3]
    return dict(frame=sample[2], age_ms=age, channels=list(sample[4:20]),
                digital17=bool(flags & 1), digital18=bool(flags & 2),
                lost=bool(flags & 4), failsafe=bool(flags & 8))


def motor(fd):
    sample = snapshot(fd)
    values = struct.unpack("<16i", struct.pack("<16I", *sample[5:21]))
    result = dict(cycle_ms=sample[1], accepted=sample[2], expired=sample[3],
                active_mask=sample[4], dry_run_mdeg=values, measured_count=sample[21])
    if sample[22]:
        angle, rpm, reference, limited, output = struct.unpack("<5i", struct.pack("<5I", *sample[26:31]))
        result.update(profile=sample[22], state=sample[23], fault=sample[24],
                      feedback_age_ms=sample[25], measured_mdeg=angle, rpm=rpm,
                      reference_mdeg=reference, limited_offset_mdeg=limited, output_raw=output)
        if sample[31]:
            rpm, speed, offset, output = struct.unpack("<4i", struct.pack("<4I", *sample[33:37]))
            result["last_speed_trip"] = dict(sources=sample[31], cycle_ms=sample[32], rpm=rpm,
                                             speed_mdeg_s=speed, offset_mdeg=offset, output_raw=output)
    return result


def status(fd, core):
    sample = snapshot(fd, STATUS_OFFSET)
    result = dict(uptime_ms=sample[1], accepted=sample[2], expired=sample[3],
                  rejected=sample[4], active=bool(sample[5]))
    if core == "m33":
        result.update(can_ready_mask=sample[6], can_rx=sample[7:10], can_tx_done=sample[10:13])
    else:
        data = struct.pack("<16I", *sample[18:34])
        result.update(rx_bytes=sample[13], uart_errors=sample[14], frames=sample[15],
                      discarded=sample[16], uart_dry_run=data[:sample[17]].hex())
    return result


@contextlib.contextmanager
def endpoint(core, writer=False):
    if core not in ("m33", "m0"):
        raise ValueError("unknown core")
    fd = os.open("/dev/srnos-" + core, os.O_RDWR if writer else os.O_RDONLY)
    try:
        yield fd
    finally:
        os.close(fd)


def send_targets(fd, targets, ttl_ms):
    if not 1 <= len(targets) <= 16 or not 1 <= ttl_ms <= 5000:
        raise ValueError("expected 1..16 targets and TTL 1..5000 ms")
    payload = struct.pack("<3I16i", 2, ttl_ms, (1 << len(targets)) - 1,
                          *targets, *([0] * (16 - len(targets))))
    os.pwrite(fd, payload, 0)


def send_angles(fd, targets, mask, ttl_ms, *, drive=False):
    """Send an explicit application angle request, or a preview with identical units."""
    if len(targets) != 16 or not 0 < mask <= 0xffff or not 1 <= ttl_ms <= 100:
        raise ValueError("expected 16 slots, a valid mask and TTL 1..100 ms")
    os.pwrite(fd, struct.pack("<3I16i", 5 if drive else 4, ttl_ms, mask, *targets), 0)


def clear_targets(fd):
    """Supersede the last command with an empty idle target."""
    os.pwrite(fd, REQUEST.pack(2, 20, 0, *([0] * 16)), 0)


def send_uart(fd, data, ttl_ms):
    if len(data) > 64 or not 1 <= ttl_ms <= 5000:
        raise ValueError("expected up to 64 bytes and TTL 1..5000 ms")
    os.pwrite(fd, struct.pack("<3I64s", 3, ttl_ms, len(data), data), 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("radio")
    sub.add_parser("motor")
    st = sub.add_parser("status")
    st.add_argument("core", choices=["m33", "m0"])
    target = sub.add_parser("target", help="publish dry-run targets in millidegrees")
    target.add_argument("values", type=int, nargs="+")
    uart = sub.add_parser("uart", help="publish UART dry-run bytes in hex")
    uart.add_argument("hex")
    for command in (target, uart):
        command.add_argument("--ttl", type=int, default=100, help="validity in peer milliseconds")
    args = parser.parse_args()
    if args.cmd in ("target", "uart"):
        with endpoint("m33" if args.cmd == "target" else "m0", writer=True) as fd:
            if args.cmd == "target":
                send_targets(fd, args.values, args.ttl)
            else:
                send_uart(fd, bytes.fromhex(args.hex), args.ttl)
        return
    core = args.core if args.cmd == "status" else ("m0" if args.cmd == "radio" else "m33")
    with endpoint(core) as fd:
        if args.cmd == "radio":
            result = radio(fd)
        elif args.cmd == "motor":
            result = motor(fd)
        else:
            result = status(fd, core)
    print(json.dumps(result))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, struct.error) as exc:
        sys.exit(str(exc))
