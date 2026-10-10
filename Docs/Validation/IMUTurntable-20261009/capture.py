#!/usr/bin/env python3
"""Read-only SOURCE V2 board evidence. Sends only LIST/SUBSCRIBE/CANCEL_ALL."""
import argparse
import json
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'Tools/HostLink'))
from imu_telemetry import Client, Decoder, open_port

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', required=True, help='Explicit CDC1 device')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--seconds', type=float, default=30)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
sources = {}
stats = {}
decoder = Decoder()
active = False
catalog_hash = None
formats = {1: '<f', 2: '<i', 3: '<I', 4: '<?'}

with (args.output / 'rx.bin').open('wb') as raw, open_port(args.port) as port:
    def received(data, received_ns):
        raw.write(data)
        for kind, service, message, sequence, payload in decoder.feed(data):
            if not active or (kind, service, message) != (2, 4, 0x12):
                continue
            assert len(payload) >= 16
            h, encoded_us, count = struct.unpack_from('<IQB', payload)
            assert h == catalog_hash
            offset = 16
            for _ in range(count):
                index, typ, valid, seq, sampled_us = struct.unpack_from('<HBBIQ', payload, offset)
                offset += 16
                assert index in sources and typ == sources[index]['type']
                value, = struct.unpack_from(formats[typ], payload, offset)
                offset += struct.calcsize(formats[typ])
                entry = stats.setdefault(index, dict(count=0, invalid=0, first=value,
                    last=value, min=value, max=value, first_sequence=seq,
                    first_sampled_us=sampled_us, first_host_ns=received_ns))
                entry['count'] += 1
                entry['invalid'] += int(not valid)
                entry['last'] = value
                entry['min'] = min(entry['min'], value)
                entry['max'] = max(entry['max'], value)
                entry['last_sequence'] = seq
                entry['last_sampled_us'] = sampled_us
                entry['last_host_ns'] = received_ns
                # HostComm timestamps the packet before reading each cache;
                # the application can publish a newer value in between.
                entry["max_sample_after_encode_us"] = max(
                    entry.get("max_sample_after_encode_us", 0), sampled_us - encoded_us)
            assert offset == len(payload)

    client = Client(port, on_receive=received)
    cursor = 0
    while cursor != 65535:
        body = client.request(4, 0x10, struct.pack('<HB', cursor, 8))
        h, total, cursor, count = struct.unpack_from('<IHHB', body)
        assert catalog_hash is None or catalog_hash == h
        catalog_hash = h
        offset = 9
        for _ in range(count):
            index, typ, size, max_hz, hz, version, name_len, unit_len = struct.unpack_from('<HBBHHBBB', body, offset)
            offset += 11
            name = body[offset:offset+name_len].decode(); offset += name_len
            unit = body[offset:offset+unit_len].decode(); offset += unit_len
            sources[index] = dict(name=name, type=typ, unit=unit, max_hz=max_hz)
        assert offset == len(body)
    expected = {f'turntable.motor{i}.{field}' for i in (1,2,3)
                for field in ('encoder_raw', 'angle_deg', 'online')}
    assert {s['name'] for s in sources.values()} == expected and total == 9
    request = struct.pack('<IB', catalog_hash, len(sources))
    request += b''.join(struct.pack('<HH', index, s['max_hz']) for index, s in sources.items())
    subscribed = False
    try:
        client.request(4, 0x11, request)
        subscribed = True
        active = True
        start = time.monotonic()
        while time.monotonic() - start < args.seconds:
            client.receive()
        elapsed = time.monotonic() - start
    finally:
        active = False
        if subscribed:
            client.request(4, 0x14, struct.pack('<I', catalog_hash))
    for index, entry in stats.items():
        entry.update(sources[index])
        entry['received_hz'] = entry['count'] / elapsed
    summary = dict(port=args.port, elapsed_seconds=elapsed, catalog_hash=catalog_hash,
        decoder_errors=dict(decoder.errors), sources=stats,
        requests=['SOURCE_LIST_V2', 'SOURCE_SUBSCRIBE_V2', 'SOURCE_CANCEL_ALL_V2'])
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary, indent=2), flush=True)
    assert len(stats) == 9 and not decoder.errors
    assert all(s['invalid'] == 0 for s in stats.values())
    assert all(s['min'] is True for s in stats.values() if s['name'].endswith('.online'))
