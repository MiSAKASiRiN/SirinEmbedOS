#!/usr/bin/env python3
"""Decode host-side GDB stack dumps without executing code on the target.
Usage: read_stack_dump.py /tmp/debug.log /tmp/srnos-before
"""
import json
from pathlib import Path
import re
import sys

log, prefix = sys.argv[1:]
text = Path(log).read_text(errors="replace")
start = text.rindex("SRNOS_STACK_SCAN_BEGIN " + prefix)
end = text.index("SRNOS_STACK_SCAN_END " + prefix, start)
regions = [(0x20000000, Path(prefix + "-dtcm.bin").read_bytes()),
           (0x24000000, Path(prefix + "-d1.bin").read_bytes())]
tasks = {}
for name, priority, low, high in re.findall(r"SRNOS_STACK (.+?) (\d+) (0x[0-9a-f]+) (0x[0-9a-f]+)", text[start:end]):
    low, high = int(low, 16), int(high, 16)
    for base, data in regions:
        if base <= low < high <= base + len(data):
            stack = data[low-base:high-base]
            tasks[name] = dict(priority=int(priority), stack_span_bytes=len(stack),
                               minimum_free_bytes=len(stack)-len(stack.lstrip(b"\xa5")))
            break
    else:
        raise ValueError("Stack outside memory dumps: " + name)
assert tasks, "No tasks found"
print(json.dumps(tasks, indent=2, sort_keys=True))
