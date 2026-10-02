#!/usr/bin/env python3
"""Build-only SDK integration regression; never connects to a board."""
import hashlib
import argparse
import json
import pathlib
import re
import shlex
import shutil
import subprocess
import tempfile

SDK = pathlib.Path(__file__).resolve().parents[2]
PUBLIC_INCLUDE = SDK / "Include"


def run(*args):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Command failed: {args}\n{result.stdout}")
    return result.stdout


def compiler_arguments(command):
    return command.get('arguments') or shlex.split(command['command'])


def check_parent_compile_commands(commands, source):
    """Application units must not acquire implementation headers transitively."""
    source = source.resolve()
    checked = 0
    for command in commands:
        unit = pathlib.Path(command['file']).resolve()
        if not (unit.is_relative_to(source) or unit.is_relative_to(SDK / 'Examples')):
            continue
        if unit.name == 'kernel_test_adapter.c':
            # This isolated SDK board-test object deliberately tests native allocation.
            continue
        checked += 1
        args = compiler_arguments(command)
        paths = []
        for index, arg in enumerate(args):
            if arg in ('-I', '-isystem', '-iquote', '-idirafter'):
                paths.append(args[index + 1])
            elif arg.startswith('-I') and len(arg) > 2:
                paths.append(arg[2:])
            elif arg.startswith('-isystem') and len(arg) > len('-isystem'):
                paths.append(arg[len('-isystem'):])
        for path in paths:
            included = (pathlib.Path(command['directory']) / path).resolve()
            if included.is_relative_to(SDK):
                assert included == PUBLIC_INCLUDE, (unit, 'SDK implementation include leaked', included)
        for arg in args:
            assert not re.match(r'-D(?:USE_HAL_DRIVER|STM32H723xx|STM32_THREAD_SAFE_STRATEGY)(?:=|$)', arg), (unit, arg)
    assert checked, 'No application compile commands were checked'
    print(f'PASS: {checked} application compile commands expose only SDK public headers')


def check_public_headers(work, config, keep_going=False):
    """Compile every header alone and inspect the compiler's full include closure."""
    headers = sorted(p for p in (PUBLIC_INCLUDE / 'SirinEmbedOS').rglob('*')
                     if p.suffix in ('.h', '.hpp'))
    assert headers
    work.mkdir()
    common = ['-mcpu=cortex-m7', '-mfpu=fpv5-d16', '-mfloat-abi=hard',
              '-fsyntax-only', '-Werror', '-I', str(PUBLIC_INCLUDE),
              '-DSRNOS_MEMORY_SECTIONS=1', f'-DSRNOS_CONFIG_HEADER="{config}"']
    compiled = 0
    failures = []

    def compile_unit(name, text, language):
        nonlocal compiled
        suffix = '.c' if language == 'c' else '.cpp'
        unit = work / (name + suffix)
        deps = work / (name + suffix + '.d')
        unit.write_text(text)
        compiler = 'starm-clang' if language == 'c' else 'starm-clang++'
        standard = '-std=c11' if language == 'c' else '-std=c++17'
        run(compiler, standard, *common, '-MD', '-MF', str(deps), '-MT', 'public-header', str(unit))
        dependency_text = deps.read_text().replace('\\\n', '')
        dependencies = shlex.split(dependency_text.split(':', 1)[1])
        for value in dependencies:
            path = pathlib.Path(value).resolve()
            if path.is_relative_to(SDK):
                assert path.is_relative_to(PUBLIC_INCLUDE), (unit, 'Public header leaked implementation dependency', path)
        compiled += 1

    def check_unit(name, text, language, label):
        try:
            compile_unit(name, text, language)
        except (RuntimeError, AssertionError) as error:
            if not keep_going:
                raise
            lines = str(error).splitlines()
            diagnostic = next((line for line in lines if ': error:' in line or ': fatal error:' in line), lines[-1])
            failures.append((label, language, diagnostic))
            print(f'FAIL: {label} ({language}): {diagnostic}', flush=True)

    for index, header in enumerate(headers):
        label = header.relative_to(PUBLIC_INCLUDE).as_posix()
        text = f'#include "{label}"\n'
        check_unit(f'header_{index}', text, 'c++', label)
        if header.suffix == '.h':
            check_unit(f'header_{index}', text, 'c', label)
    check_unit('all_headers', ''.join(
        f'#include "{header.relative_to(PUBLIC_INCLUDE).as_posix()}"\n' for header in headers), 'c++', 'all public headers')
    if failures:
        raise RuntimeError(f'{len(failures)} public header compilation/dependency checks failed; {compiled} passed')
    print(f'PASS: {len(headers)} public headers, {compiled} C11/C++17 standalone/aggregate units and dependency closures')


def check_elf(elf):
    symbols = {}
    for line in run("starm-nm", str(elf)).splitlines():
        fields = line.split()
        if len(fields) == 3:
            value, kind, name = fields
            symbols[name] = (int(value, 16), kind)
    for name in ("main", "SRNOS_ApplicationMain", "SVC_Handler", "PendSV_Handler",
                 "SysTick_Handler", "TIM7_IRQHandler", "vPortDefineHeapRegions",
                 "vApplicationStackOverflowHook", "vApplicationMallocFailedHook"):
        assert symbols[name][1] == "T", (name, symbols.get(name))
    address = lambda name: symbols[name][0]
    assert address("__sirinos_heap_dtcm_end") - address("__sirinos_heap_dtcm_start") == 49152
    assert address("__sirinos_heap_d1_end") - address("__sirinos_heap_d1_start") == 131072
    assert 0x20000000 <= address("__sirinos_heap_dtcm_start") < address("__sirinos_heap_dtcm_end") <= address("_sstack")
    assert 0x24000000 <= address("_sdata") <= address("_ebss") <= address("__sirinos_heap_d1_start")
    assert address("__sirinos_heap_d1_end") <= address("__srnos_libc_start") < address("__srnos_libc_end") <= 0x24050000
    if "tls_initialized" in symbols:
        assert address("__tls_size") >= 12  # initialized TLS, zero TLS and errno
    assert address("__preinit_array_end") > address("__preinit_array_start")
    if "boot_check" in symbols:
        assert address("__init_array_end") > address("__init_array_start")
    if "adc_samples" in symbols:
        assert 0x38000000 <= address("adc_samples") < address("__bdma_buf_end__")
        assert address("adc_samples") % 32 == 0
    assert "TSKPTT_HSKP_Task" not in symbols
    sections = run("starm-readelf", "-SW", str(elf))
    for section in (".sirinos_heap_dtcm", ".sirinos_heap_d1"):
        assert re.search(re.escape(section) + r"\s+NOBITS", sections)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--public-headers-only', action='store_true',
                        help='Check every public header and summarize failures without building firmware')
    arguments = parser.parse_args()
    if arguments.public_headers_only:
        with tempfile.TemporaryDirectory(prefix='SRNOS public headers ') as temp:
            config = pathlib.Path(temp) / 'robot_config.h'
            shutil.copyfile(SDK / 'Tests/Build/Parent/robot_config.h', config)
            check_public_headers(pathlib.Path(temp) / 'Headers',
                                 config, keep_going=True)
        return
    for vendor in (SDK / "ThirdParty/FreeRTOS", SDK / "ThirdParty/USBDevice"):
        for line in (vendor / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ", 1)
            assert hashlib.sha256((vendor / name).read_bytes()).hexdigest() == digest, name
    for executable in ("cmake", "ninja", "starm-clang", "starm-clang++", "starm-nm", "starm-readelf"):
        if not shutil.which(executable):
            raise RuntimeError(f"Missing {executable}; add ST Arm Clang bin to PATH")
    with tempfile.TemporaryDirectory(prefix="SirinEmbedOS integration ") as temp:
        source = pathlib.Path(temp) / "Parent Project"
        shutil.copytree(SDK / "Tests/Build/Parent", source)
        build = pathlib.Path(temp) / "Build Output"
        check_public_headers(pathlib.Path(temp) / 'Public Headers', source / 'robot_config.h')
        print(run('python3', str(SDK / 'Tests/Build/check_public_names.py')).strip())
        run("cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", f"-DSIRINEMBEDOS_SOURCE_DIR={SDK}",
            f"-DCMAKE_TOOLCHAIN_FILE={SDK / 'CMake/starm-clang.cmake'}")
        run("cmake", "--build", str(build), "-j", "8")
        commands = json.loads((build / 'compile_commands.json').read_text())
        check_parent_compile_commands(commands, source)
        print(run('python3', str(SDK / 'Tests/Build/check_runtime_names.py'),
                  '--compile-commands', str(build / 'compile_commands.json')).strip())
        for name in ("RobotFirmware", "ManualFirmware", "PrototypeBindings", "ParameterExample", "BringUpExample", "SensorBringUpExample", "INSBringUpExample", "EEPROMBringUpExample", "RawBinding", "AllComponents"):
            check_elf(build / f"{name}.elf")
            run("python3", str(SDK / "Tests/Core/check_foundation_elf.py"), str(build / f"{name}.elf"))
            print(f"PASS: {name}")
        # Capacity overrides must change real link boundaries, and an overflow
        # must fail the link instead of silently overlapping ordinary data.
        run("cmake", "-S", str(source), "-B", str(build),
            "-DSRNOS_MSP_BYTES=12288", "-DSRNOS_LIBC_BYTES=4096")
        run("cmake", "--build", str(build), "--target", "RobotFirmware", "-j", "8")
        run("python3", str(SDK / "Tests/Core/check_foundation_elf.py"), str(build / "RobotFirmware.elf"))
        run("cmake", "-S", str(source), "-B", str(build), "-DSRNOS_LIBC_BYTES=524288")
        overflow = subprocess.run(["cmake", "--build", str(build), "--target", "RobotFirmware"],
                                  text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert overflow.returncode != 0 and "D1 heap/libc/data overflow" in overflow.stdout
        print("PASS: MSP/libc capacity overrides and rejected arena overflow")
    print("PASS: vendor hashes, parent project, ten firmware targets, C++/TLS linkage, config overrides, IRQ and heap layout")
    print("Build/static checks only; no board execution was performed.")


if __name__ == "__main__":
    main()
