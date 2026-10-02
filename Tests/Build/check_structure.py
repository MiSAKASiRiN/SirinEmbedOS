#!/usr/bin/env python3
"""Repository contracts, independent of target hardware and source Git history."""
from pathlib import Path
import collections
import json
import re
import subprocess
import argparse

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compile-commands', required=True, type=Path,
                    help='Fresh compile_commands.json from the build being verified')
commands_path = parser.parse_args().compile_commands.resolve(strict=True)
private_headers = {p.relative_to(ROOT/'Board/Support').as_posix()
                   for p in (ROOT/'Board/Support').rglob('*.h')}
scanned = []
for directory in ('Source', 'Include', 'Config', 'CMake', 'Tests', 'Examples', 'Board/Support', 'Tools/Core'):
    for path in (ROOT / directory).rglob('*'):
        if path.is_file() and path.suffix in ('.c','.cpp','.h','.hpp','.cmake','.s','.S','.py','.sh'):
            text = path.read_text(errors='replace')
            assert not re.search(r'\bOS(?:::|_[A-Za-z0-9_]+)|\bnamespace\s+OS\b', text), path
            assert not re.search(r'#\s*include\s*[<"][^">]*Frameworks/', text), path
            assert 'Board/' + 'Glue' not in text, path
            if directory == 'Include':
                includes = re.findall(r'#\s*include\s*[<"]([^">]+)', text)
                assert not any('Board/Support' in h or h in private_headers for h in includes), path
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
for directory in ('Source', 'Board/Support', 'Board/Core/Src'):
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
if commands_path.is_file():
    commands=json.loads(commands_path.read_text())
    compiled=collections.Counter((Path(c['directory'])/c['file']).resolve() for c in commands)
    for directory in ('Source', 'Board/Support'):
        for p in (ROOT/directory).rglob('*'):
            if p.suffix in ('.c','.cpp'):
                assert compiled[p.resolve()] == 1, f'Missing or duplicate source: {p}'
    for c in commands:
        assert 'h723vg-v2-prototype' not in c['command'], c['file']
        assert 'Board/' + 'Glue' not in c['command'], c['file']
assert not (ROOT/'Board'/'Glue').exists()
for relative in ('CMakeLists.txt', 'Board/CMakeLists.txt',
                 'Board/generate.py', 'Tools/Debug/check_i2c_timing.py',
                 'Tools/Migration/finalize_ledger.py'):
    assert 'Board/' + 'Glue' not in (ROOT/relative).read_text(), relative
print(f'PASS: {len(scanned)} source/header/tool contracts, {len(callbacks)} unique HAL callbacks, 49 modules, explicit source coverage, no nested Git metadata')

# Managed task ownership: parent examples expose only the SDK contract.
for p in (ROOT/'Source').rglob('*'):
    if p.suffix not in ('.c','.cpp'): continue
    source=p.read_text()
    if p != ROOT/'Source/Kernel/thread.c':
        assert not re.search(r'\b(?:osThreadNew|xTaskCreate\w*|osThreadTerminate|vTaskDelete)\s*\(',source), p
for p in (ROOT/'Examples').rglob('*'):
    if p.suffix not in ('.c','.cpp','.h'): continue
    if p.name=='kernel_test_adapter.c': continue
    assert not re.search(r'#\s*include\s*[<"](?:FreeRTOS|task|cmsis_os2|semphr|queue|event_groups)[.]h',p.read_text()), p
for name in ('Thread.h','Sync.h'):
    assert not re.search(r'FreeRTOS[.]h|cmsis_os2[.]h|TaskHandle_t|osThreadId_t', (ROOT/'Include/SirinEmbedOS/Kernel'/name).read_text()), name
for path in ('Source/Services/SystemMonitor','Source/Diagnostics/Runtime'):
    for p in (ROOT/path).glob('*.c'):
        assert not re.search(r'vTaskSuspendAll|uxTaskGetSystemState|uxTaskGetStackHighWaterMark',p.read_text()),p
print('PASS: unified SDK creation/deletion, public kernel boundary, examples and no monitor table suspension/stack scan')
