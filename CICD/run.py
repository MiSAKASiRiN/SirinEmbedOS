#!/usr/bin/env python3
"""Build SDK or an owning application, and collect an explicit artifact set."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

from common import redact


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('sdk', 'application'), required=True)
    parser.add_argument('--source', type=Path, default=Path.cwd())
    parser.add_argument('--sdk', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--target', required=True)
    parser.add_argument('--output', type=Path, default=Path('ci-output'))
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*', args.target):
        parser.error('target must be a single firmware target name')
    source, sdk, output = args.source.resolve(), args.sdk.resolve(), args.output.resolve()
    if output in (source, sdk) or output in source.parents or output in sdk.parents:
        parser.error('output must be a dedicated artifact directory')
    output.mkdir(parents=True, exist_ok=True)
    marker = output / '.srnos-ci-output'
    if not marker.is_file() and any(output.iterdir()):
        parser.error('output must be empty or already managed by this tool')
    marker.touch()
    # Never publish a previous invocation's success or firmware after a failure.
    for name in ('summary.json', 'SHA256SUMS'):
        (output / name).unlink(missing_ok=True)
    for name in ('logs', 'firmware'):
        if (output / name).is_symlink():
            raise ValueError('artifact directories must not be symlinks')
        if (output / name).exists():
            shutil.rmtree(output / name)
        (output / name).mkdir()
    os.environ['SRNOS_CI_SOURCE_ROOT'] = str(source)
    os.environ['SRNOS_CI_SDK_ROOT'] = str(sdk)
    job_id = os.environ.get('CI_JOB_ID', '')
    summary = {'status': 'failed', 'mode': args.mode, 'target': args.target,
               'build_job_id': job_id if job_id.isdecimal() else '', 'checks': [], 'memory': []}
    started = time.monotonic()

    def check(name, command):
        print(f'[CI] {name}', flush=True)
        code = 127
        with (output / 'logs' / f'{name}.log').open('w') as log:
            try:
                process = subprocess.Popen([str(x) for x in command], cwd=source,
                                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                           text=True, errors='replace')
                for line in process.stdout:
                    clean = redact(line)
                    print(clean, end='', flush=True)
                    log.write(clean)
                code = process.wait()
            except OSError as error:
                log.write(redact(f'{type(error).__name__}: {error}\n'))
        summary['checks'].append({'name': name, 'exit_code': code})
        print(f'[CI] {name}: {"PASS" if code == 0 else "FAIL"}', flush=True)
        return code == 0

    try:
        # Isolate build outputs across shell jobs.
        with tempfile.TemporaryDirectory(prefix='srnos-ci-') as scratch:
            work = Path(scratch)
            os.environ['TMPDIR'] = str(work)
            if args.mode == 'sdk' and source != sdk:
                raise ValueError('SDK mode must run from the SDK source root')
            for config in ('Debug', 'Release'):
                build = work / config
                configured = check(f'{config}-configure', ['cmake', '-S', source, '-B', build,
                    '-G', 'Ninja', f'-DCMAKE_BUILD_TYPE={config}',
                    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                    f'-DCMAKE_TOOLCHAIN_FILE={sdk / "CMake/starm-clang.cmake"}'])
                if not configured or not check(f'{config}-build', ['cmake', '--build', build,
                                          '--target', args.target, '-j', os.environ.get('SRNOS_CI_JOBS', '8')]):
                    continue
                elf, map_file = build / f'{args.target}.elf', build / f'{args.target}.map'
                destination = output / 'firmware' / config
                destination.mkdir()
                for path in (elf, map_file):
                    shutil.copyfile(path, destination / path.name)
                for kind, suffix in (('binary', 'bin'), ('ihex', 'hex')):
                    check(f'{config}-{suffix}', ['starm-objcopy', '-O', kind, elf,
                          destination / f'{args.target}.{suffix}'])
                check(f'{config}-size', ['starm-size', elf])
                lines = (output / 'logs' / f'{config}-build.log').read_text().splitlines()
                for i, line in enumerate(lines):
                    if line.lstrip().startswith('Memory region'):
                        summary['memory'].append({'configuration': config, 'text': '\n'.join(lines[i:i+12])})
    except Exception as error:
        summary['checks'].append({'name': 'ci-driver', 'exit_code': 1})
        print(redact(f'[CI] {type(error).__name__}: {error}'), flush=True)
    summary['status'] = 'success' if summary['checks'] and all(c['exit_code'] == 0 for c in summary['checks']) else 'failed'
    summary['duration_seconds'] = round(time.monotonic() - started, 1)
    # Sanitize string values before serialization; redacting encoded JSON could
    # consume escape sequences or replace a secret that resembles JSON syntax.
    def clean(value):
        if isinstance(value, str):
            return redact(value)
        if isinstance(value, list):
            return [clean(item) for item in value]
        if isinstance(value, dict):
            return {key: clean(item) for key, item in value.items()}
        return value
    (output / 'summary.json').write_text(json.dumps(clean(summary), ensure_ascii=False, indent=2) + '\n')
    with (output / 'SHA256SUMS').open('w') as hashes:
        for path in sorted((output / 'firmware').rglob('*')):
            if path.is_file():
                hashes.write(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(output)}\n')
    return 0 if summary['status'] == 'success' else 1


if __name__ == '__main__':
    sys.exit(main())
