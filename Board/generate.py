#!/usr/bin/env python3
"""从当前 checkout 调用 CubeMX，保留生成前快照和日志；不构建或烧录。"""
import argparse
from datetime import datetime
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

BOARD = Path(__file__).resolve().parent
ROOT = BOARD.parent
MAC_CUBEMX = '/Applications/STMicroelectronics/STM32CubeMX.app/Contents/MacOS/STM32CubeMX'


def manifest(directory):
    result = {}
    for base, directories, files in os.walk(directory):
        directories[:] = [name for name in directories if name not in ('build', '.idea', '__pycache__')]
        for name in files:
            path = Path(base) / name
            result[str(path.relative_to(directory))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cubemx', default=os.environ.get('STM32CUBEMX'),
                        help='CubeMX 可执行文件路径；也可设置 STM32CUBEMX')
    parser.add_argument('--timeout', type=int, default=300, help='生成超时秒数，默认 300')
    parser.add_argument('--dry-run', action='store_true', help='仅显示调用计划，不创建文件或启动 CubeMX')
    args = parser.parse_args()
    executable = args.cubemx or (MAC_CUBEMX if Path(MAC_CUBEMX).is_file() else 'STM32CubeMX')
    executable = shutil.which(executable)
    if not executable:
        parser.error('未找到 CubeMX；请通过 --cubemx 或 STM32CUBEMX 指定可执行文件')
    if args.timeout <= 0:
        parser.error('--timeout 必须大于 0')
    ioc = BOARD / 'OMNICTL2_3.ioc'
    if not ioc.is_file():
        parser.error(f'找不到 IOC：{ioc}')
    # macOS CubeMX does not reliably resolve config load against subprocess cwd.
    # Always pass an absolute IOC path, even when cwd is explicitly Board.
    if any(character in ioc.as_posix() for character in ('"', '\n', '\r')):
        parser.error('IOC 路径不能包含双引号或换行')
    commands = f'config load "{ioc.as_posix()}"\nproject generate\nexit\n'
    print(f'CubeMX: {executable}\n工作目录: {BOARD}\n脚本:\n{commands}', flush=True)
    if args.dry_run:
        return 0

    output = ROOT / 'build' / 'cubemx'
    output.mkdir(parents=True, exist_ok=True)
    run = Path(tempfile.mkdtemp(prefix=datetime.now().strftime('%Y%m%d-%H%M%S-'), dir=output))
    script = run / 'generate.mx'
    script.write_text(commands)
    backup = run / 'Board-before'
    shutil.copytree(BOARD, backup, ignore=shutil.ignore_patterns('build', '.idea', '__pycache__'))
    before = manifest(backup)
    print(f'快照及日志: {run}', flush=True)
    status = 1
    try:
        with (run / 'generate.log').open('w') as log:
            process = subprocess.run([executable, '-q', str(script)], cwd=BOARD,
                                     stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
        text = (run / 'generate.log').read_text(errors='replace')
        generation = text.rsplit('\nproject generate\n', 1)
        if process.returncode != 0:
            print(f'CubeMX 退出码: {process.returncode}', file=sys.stderr)
        elif len(generation) != 2 or not re.search(r'^OK\s*$', generation[1], re.M):
            print('未找到 project generate 的 OK 结果，请检查日志。', file=sys.stderr)
        else:
            with (run / 'timing-check.log').open('w') as log:
                check = subprocess.run([sys.executable, str(ROOT / 'Tools/Debug/check_i2c_timing.py')],
                                       cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            status = 0 if check.returncode == 0 else 1
            print('Generate 成功；时序检查' + ('通过。' if status == 0 else '失败，请检查日志。'))
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f'生成失败: {error}', file=sys.stderr)
    finally:
        after = manifest(BOARD)
        changed = sorted(name for name in before.keys() | after.keys() if before.get(name) != after.get(name))
        (run / 'changed-files.txt').write_text('\n'.join(changed) + ('\n' if changed else ''))
        print('Board 内容变化: ' + (', '.join(changed) if changed else '无'))
        print(f'日志与生成前快照保留于 {run}；未自动回滚、构建或烧录。')
    return status


if __name__ == '__main__':
    sys.exit(main())
