#!/usr/bin/env python3
"""Repository contracts, independent of target hardware and source Git history."""
from pathlib import Path
import collections
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
scanned = []
for directory in ('Source', 'Include', 'Config', 'CMake', 'Tests', 'Examples', 'Board/Glue', 'Tools/Core'):
    for path in (ROOT / directory).rglob('*'):
        if path.is_file() and path.suffix in ('.c','.cpp','.h','.hpp','.cmake','.s','.S','.py','.sh'):
            text = path.read_text(errors='replace')
            assert not re.search(r'\bOS(?:::|_[A-Za-z0-9_]+)|\bnamespace\s+OS\b', text), path
            assert not re.search(r'#\s*include\s*[<"][^">]*Frameworks/', text), path
            scanned.append(path)
for path in ROOT.rglob('.git'):
    assert path == ROOT / '.git', path
assert not list(ROOT.rglob('.gitmodules'))
assert not any(line.startswith('160000 ') for line in subprocess.check_output(
    ['git','-C',str(ROOT),'ls-files','-s'],text=True).splitlines())
# Build/ contains source fixtures, so an unanchored build/ ignore is forbidden.
probe = subprocess.run(['git','-C',str(ROOT),'check-ignore','Tests/Build/check_sdk.py'],stdout=subprocess.PIPE)
assert probe.returncode == 1, 'Tests/Build must not be ignored'
callbacks = collections.defaultdict(list)
for directory in ('Source', 'Board/Glue', 'Board/Core/Src'):
    for path in (ROOT / directory).rglob('*'):
        if path.suffix not in ('.c','.cpp'): continue
        source = re.sub(r'^\s*#.*$', '', path.read_text(), flags=re.M)
        for name in re.findall(r'\bvoid\s+(HAL_\w+Callback)\s*\([^;{}]*\)\s*\{',source):
            callbacks[name].append(str(path.relative_to(ROOT)))
assert callbacks
for name, owners in callbacks.items(): assert len(owners) == 1, (name, owners)
manifest = json.loads((ROOT / 'Docs/Migration/source-manifest.json').read_text())
assert len(manifest['modules']) == 49
assert len(set(m['module'] for m in manifest['modules'])) == 49
# Confirm every self-maintained implementation participates in explicit CMake.
commands_path=ROOT/'build/Debug/compile_commands.json'
if commands_path.exists():
    commands=json.loads(commands_path.read_text())
    compiled={Path(c['file']).resolve() for c in commands}
    for p in (ROOT/'Source').rglob('*'):
        if p.suffix in ('.c','.cpp'): assert p.resolve() in compiled, f'Unbuilt source: {p}'
    for c in commands:
        assert 'h723vg-v2-prototype' not in c['command'], c['file']
print(f'PASS: {len(scanned)} source/header/tool contracts, {len(callbacks)} unique HAL callbacks, 49 modules, explicit source coverage, no nested Git metadata')
