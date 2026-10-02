#!/usr/bin/env python3
"""Reproduce the frozen board-run summary from GDB logs and binary dumps."""
import hashlib
import json
from pathlib import Path
import re
import statistics
import struct

here = Path(__file__).resolve().parent
log = (here / 'gdb.log').read_text()
record = re.findall(r'^\$\d+ = (\{stage = 7, .*\})$', log, re.M)[-1]


def number(text, field):
    return int(re.search(r'\b' + field + r' = (\d+)', text)[1])


def fields(text, names):
    return {name: number(text, name) for name in names.split()}


def block(text, field):
    begin = text.index(field + ' = {') + len(field + ' = ')
    level = 0
    for end in range(begin, len(text)):
        level += (text[end] == '{') - (text[end] == '}')
        if not level:
            return text[begin:end + 1]
    raise ValueError(field)


summary = dict(date='2026-09-28', baseline_commit='ebef330')
summary.update(fields(record, 'stage passed failed elapsed_ms poll_count backup_hash final_hash'))
summary.update({key: re.search(r'\b' + key + r' = (true|false)', record)[1] == 'true'
                for key in ['backup_valid', 'restored', 'restore_skipped']})
assert summary['stage'] == 7 and summary['passed'] == 21 and summary['failed'] == 0
assert summary['backup_valid'] and summary['restore_skipped'] and not summary['restored']
for when in ['before', 'after']:
    bus = block(record, 'bus_' + when)
    summary['bus_' + when] = fields(bus, 'recover_count cb_queue_overflow high_water quarantined')
    for name in ['periodic', 'background']:
        summary['bus_' + when][name] = fields(block(bus, name),
            'accepted completed errors timeouts cancelled queue_full max_queue_us max_transfer_us')
    for name in ['lis', 'spa']:
        sensor = block(record, name + '_' + when)
        summary[name + '_' + when] = fields(sensor,
            'samples errors recoveries initializations max_acquisition_us max_period_us')
        summary[name + '_' + when]['ready'] = 'state = SRNOS_SENSOR_READY' in sensor

for when in ['before', 'after']:
    for kind in ['periodic', 'background']:
        counters = summary['bus_' + when][kind]
        assert counters['accepted'] == counters['completed']
        assert all(counters[key] == 0 for key in ['errors', 'timeouts', 'cancelled', 'queue_full'])
    for sensor in ['lis', 'spa']:
        assert summary[sensor + '_' + when]['ready']
        assert summary[sensor + '_' + when]['errors'] == 0
        assert summary[sensor + '_' + when]['recoveries'] == 0

phase_names = ['unused', 'idle', 'blocking_write', 'async_read', 'async_write',
               'blocking_read', 'boundaries', 'restore_write', 'final_read']
data = (here / 'continuity.bin').read_bytes()
assert len(data) == 576
summary['phases'] = {}
for phase, name in enumerate(phase_names):
    summary['phases'][name] = sensors = {}
    for n, sensor in enumerate(['LIS', 'SPA']):
        row = struct.unpack_from('<IIIIQQ', data, (phase * 2 + n) * 32)
        sensors[sensor] = dict(zip(['observations', 'not_ready', 'samples',
                                   'max_gap_us', 'first_us', 'last_us'], row))
        sensors[sensor]['rate_hz'] = (row[2] - 1) * 1e6 / (row[5] - row[4]) if row[2] > 1 else None
        assert row[1] == 0

core, count, *tail = struct.unpack('<131I', (here / 'scl.bin').read_bytes())
cycles = tail[:count]
assert count == 128 and tail[-1] == 0
median = statistics.median(cycles)
# Exclude the one long interval from the dominant-cluster mean. Keep every raw
# value and report the unfiltered mean too; GPIO sampling cannot identify whether
# the long interval was interruption, a missed edge, stretching or a bus gap.
cluster = [value for value in cycles if value < median * 1.25]
summary['scl'] = dict(core_hz=core, count=count, cycles=cycles,
    median_cycles=median, mean_cycles=statistics.mean(cycles),
    median_hz=core / median, cluster_count=len(cluster),
    cluster_mean_cycles=statistics.mean(cluster), cluster_hz=core / statistics.mean(cluster),
    note='Digital GPIO/DWT observation only; not an analog rise/fall measurement.')

expected = bytearray((i * 73 + (i >> 8) + 191) & 255 for i in range(16384))
for seed, (address, length) in enumerate([(0x3f, 259), (0xff, 259), (0x1ff3, 259), (0x3fff, 1)], 1):
    expected[address:address + length] = bytes((i * 73 + (i >> 8) + seed) & 255 for i in range(length))
final = (here / 'eeprom-final.bin').read_bytes()
assert final == expected
fnv = 2166136261
for value in final:
    fnv = ((fnv ^ value) * 16777619) & 0xffffffff
assert fnv == summary['final_hash']
summary['final_content_matches_expected'] = True
summary['sha256'] = {name: hashlib.sha256((here / name).read_bytes()).hexdigest()
    for name in ['eeprom-before.bin', 'eeprom-final.bin', 'continuity.bin', 'scl.bin']}
# Frozen identities of the actual built/tested images; paths may later be rebuilt.
summary['elf_sha256'] = {
    'Debug': 'cc042d78c69236f0ca63655dae3ff5815cc8dc13d8efa729fffdd6d71cb24cbd',
    'Release': 'da62b24341c1bc10095220c4ebb56681361f4a3ea5fef31968355a813c68c126'}
(here / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print('PASS: 21 board checks, sensor continuity, SCL samples and all 16384 final EEPROM bytes')
