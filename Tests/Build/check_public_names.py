#!/usr/bin/env python3
"""Check public macro names and compiler-parsed API declarations; no firmware build."""
import json
from pathlib import Path
import re
import subprocess
import tempfile

SDK = Path(__file__).resolve().parents[2]
INCLUDE = SDK / 'Include'
PUBLIC = INCLUDE / 'SirinEmbedOS'


def main():
    failures = set()
    headers = sorted(p for p in PUBLIC.rglob('*') if p.suffix in ('.h', '.hpp'))
    # Linker-provided placement symbols are a fixed ABI, not public SDK functions.
    linker_symbols = set(re.findall(r'extern char (__\w+)\[\];',
                                   (PUBLIC / 'Core/Memory/MemorySections.h').read_text()))
    with tempfile.TemporaryDirectory(prefix='SRNOS API names ') as temp:
        for header in headers:
            for macro in re.findall(r'^\s*#\s*define\s+(\w+)', header.read_text(), re.M):
                if not macro.startswith('SRNOS_'):
                    failures.add(f'{header.relative_to(SDK)}: macro {macro}')
            unit = Path(temp) / ('unit.cpp' if header.suffix == '.hpp' else 'unit.c')
            unit.write_text(f'#include "{header.relative_to(INCLUDE).as_posix()}"\n')
            compiler = 'starm-clang++' if header.suffix == '.hpp' else 'starm-clang'
            standard = '-std=c++17' if header.suffix == '.hpp' else '-std=c11'
            result = subprocess.run([compiler, standard, '-fsyntax-only', '-I', str(INCLUDE),
                                     '-Xclang', '-ast-dump=json', str(unit)],
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if result.returncode:
                failures.add(f'{header.relative_to(SDK)}: cannot parse declarations: {result.stderr.strip()}')
                continue
            tree = json.loads(result.stdout)

            def declarations(nodes, inherited_file=None):
                current_file = inherited_file
                for node in nodes:
                    location = node.get('loc', {})
                    location = location.get('spellingLoc', location)
                    if 'file' in location:
                        current_file = Path(location['file']).resolve()
                    kind, name = node.get('kind'), node.get('name', '')
                    is_public = current_file is not None and current_file.is_relative_to(PUBLIC)
                    if is_public and name and not node.get('isImplicit'):
                        if kind == 'NamespaceDecl':
                            if name != 'SRNOS':
                                failures.add(f'{current_file.relative_to(SDK)}: global namespace {name}')
                        elif kind in ('FunctionDecl', 'VarDecl', 'TypedefDecl', 'TypeAliasDecl',
                                      'RecordDecl', 'CXXRecordDecl', 'EnumDecl', 'EnumConstantDecl'):
                            if not name.startswith('SRNOS_') and name not in linker_symbols:
                                failures.add(f'{current_file.relative_to(SDK)}: {kind} {name}')
                    # C++ members live under the already validated namespace/type.
                    # C enumerators and extern-C blocks still expose global names.
                    if kind in ('LinkageSpecDecl', 'EnumDecl'):
                        declarations(node.get('inner', []), current_file)
            declarations(tree.get('inner', []))
    for failure in sorted(failures):
        print('FAIL:', failure)
    if failures:
        raise SystemExit(f'{len(failures)} public API naming violations')
    print(f'PASS: public macros and compiler-parsed C/C++ declarations in {len(headers)} headers use SRNOS names')


if __name__ == '__main__':
    main()
