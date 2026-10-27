#!/usr/bin/env python3
"""SirinRMT v1 codec and bounded input simulator. No actuator semantics."""
import argparse
import math
import secrets
import socket
import struct
import time
from imu_telemetry import pack, pack_raw

SERVICE, MESSAGE = 0x21, 1


def packet(kind, session, sequence, body):
    if not 0 < session <= 0xffffffff or not 0 <= sequence <= 0xffffffff:
        raise ValueError("session must be nonzero uint32; sequence must be uint32")
    if kind not in range(7) or len(body) > 16:
        raise ValueError("kind/body")
    return struct.pack("<4sBBII", b"SRMT", 1, kind, session, sequence) + body.ljust(16, b"\0")


def channels(session, sequence, first, values):
    """Channel indices 0..15; signed values in 0.01 percent (-10000..10000)."""
    if not 1 <= len(values) <= 7 or not 0 <= first <= 16-len(values):
        raise ValueError("channel page")
    if any(not isinstance(v, int) or not -10000 <= v <= 10000 for v in values):
        raise ValueError("channel value")
    return packet(1, session, sequence, bytes((first, len(values))) + struct.pack(f"<{len(values)}h", *values))


def buttons(session, sequence, valid, held):
    if not 0 <= valid <= 0xffffffffffffffff or not 0 <= held <= 0xffffffffffffffff or held & ~valid:
        raise ValueError("buttons")
    return packet(2, session, sequence, struct.pack("<QQ", valid, held))


def relative(session, sequence, mask, delta):
    if not 1 <= mask <= 7 or len(delta) != 3:
        raise ValueError("relative axes")
    if any(not isinstance(v, int) or not -0x80000000 <= v <= 0x7fffffff or (not mask & (1 << i) and v) for i, v in enumerate(delta)):
        raise ValueError("relative delta")
    return packet(3, session, sequence, struct.pack("<B3x3i", mask, *delta))


def invalidate(session, sequence):
    return packet(0, session, sequence, b"")


def enable(session, sequence):
    """Explicit one-shot request. The receiving application decides permission."""
    return packet(4, session, sequence, b"")


def disable(session, sequence):
    """Explicit stop request; preserves input data and requires a new enable."""
    return packet(5, session, sequence, b"")


def planar_velocity(session, sequence, vx, vy, wz):
    """Body-frame forward/left m/s, CCW rad/s. Application owns speed limits."""
    values = (vx, vy, wz)
    if any(not math.isfinite(v) or abs(v) > 3.4028234663852886e38 for v in values):
        raise ValueError("velocity must be finite float32")
    return packet(6, session, sequence, struct.pack("<3f", *values))


def wire(payload, sequence, stream=False):
    return (pack if stream else pack_raw)(SERVICE, MESSAGE, sequence, payload, kind=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--udp", metavar="IP:PORT")
    transport.add_argument("--serial", help="HostComm CDC or dedicated message UART")
    transport.add_argument("--hex", action="store_true", help="print payloads without sending")
    parser.add_argument("--baud", type=int, default=115200)
    inputs = parser.add_mutually_exclusive_group()
    inputs.add_argument("--channels", default="0,0,0,0,0", help="1..16 percentages in -100..100")
    inputs.add_argument("--velocity", help="body-frame vx,vy,wz in m/s,m/s,rad/s")
    parser.add_argument("--buttons", type=lambda s: int(s, 0), default=0)
    parser.add_argument("--enable", action="store_true",
                        help="send one explicit ENABLE after 100 ms of input; never auto-rearm")
    parser.add_argument("--disable", action="store_true", help="send one DISABLE and exit (no channel update)")
    parser.add_argument("--session", type=lambda s: int(s, 0),
                        help="existing nonzero session for a one-shot DISABLE")
    parser.add_argument("--sequence", type=lambda s: int(s, 0), default=0,
                        help="first sequence; must follow the current sender when reusing a session")
    parser.add_argument("--local-port", type=int, default=0,
                        help="UDP source port; an active session also requires the same peer port")
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--hz", type=float, default=20.0)
    args = parser.parse_args()
    if args.enable and args.disable:
        parser.error("--enable and --disable are mutually exclusive")
    if args.session is not None and not 0 < args.session <= 0xffffffff:
        parser.error("session must be nonzero uint32")
    if not 0 <= args.sequence <= 0xffffffff or not 0 <= args.local_port <= 65535:
        parser.error("invalid sequence or local port")
    velocity = None
    try:
        if args.velocity is not None:
            velocity = [float(s) for s in args.velocity.split(",")]
            if len(velocity) != 3:
                raise ValueError("velocity needs vx,vy,wz")
            planar_velocity(1, 0, *velocity)
        values = [float(s) for s in args.channels.split(",")]
    except ValueError as exc:
        parser.error(str(exc))
    if (not 1 <= len(values) <= 16 or any(not math.isfinite(v) or abs(v) > 100 for v in values)
            or not 0 < args.seconds <= 60 or not 1 <= args.hz <= 100):
        parser.error("invalid channels, seconds (0..60), or hz (1..100)")
    if args.enable and not args.hex and (args.seconds <= 0.1 or args.hz < 20):
        parser.error("--enable needs seconds > 0.1 and hz >= 20 for input qualification")
    values = [round(v*100) for v in values]
    session, sequence = args.session or secrets.randbelow(0xffffffff)+1, args.sequence
    buttons(session, sequence, 0xffffffffffffffff, args.buttons)  # validate before opening
    endpoint = None
    if args.udp:
        host, port = args.udp.rsplit(":", 1)
        endpoint = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if args.local_port: endpoint.bind(("", args.local_port))
        endpoint.connect((host, int(port)))
        send = endpoint.send
    elif args.serial:
        import serial  # pip install -r Tools/SirinLink/requirements.txt
        endpoint = serial.Serial(args.serial, args.baud, write_timeout=0.2)
        send = endpoint.write
    else:
        send = lambda p: print(p.hex())
    def emit(payload):
        nonlocal sequence
        send(payload if args.hex else wire(payload, sequence, stream=bool(args.serial)))
        sequence = (sequence+1) & 0xffffffff
    try:
        if args.disable:
            emit(disable(session, sequence))
            return
        started = time.monotonic()
        deadline = started+args.seconds
        enable_sent = False
        while True:
            if velocity is not None:
                emit(planar_velocity(session, sequence, *velocity))
            else:
                for first in range(0, len(values), 7):
                    emit(channels(session, sequence, first, values[first:first+7]))
            emit(buttons(session, sequence, 0xffffffffffffffff, args.buttons))
            if args.enable and not enable_sent and (args.hex or time.monotonic()-started >= 0.1):
                emit(enable(session, sequence))
                enable_sent = True
            if args.hex or time.monotonic() >= deadline:
                break
            time.sleep(1/args.hz)
    finally:
        try:
            if not args.disable: emit(invalidate(session, sequence))
        finally:
            if endpoint is not None:
                endpoint.close()


if __name__ == "__main__":
    main()
