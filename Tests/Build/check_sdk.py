#!/usr/bin/env python3
"""Build-only SDK integration regression; never connects to a board."""
import hashlib
import pathlib
import re
import shutil
import subprocess
import tempfile

SDK = pathlib.Path(__file__).resolve().parents[2]


def run(*args):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"Command failed: {args}\n{result.stdout}")
    return result.stdout


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
    assert address("__sirinos_heap_dtcm_end") - address("__sirinos_heap_dtcm_start") == 32768
    assert address("__sirinos_heap_d1_end") - address("__sirinos_heap_d1_start") == 131072
    assert 0x20000000 <= address("__sirinos_heap_dtcm_start") < address("__sirinos_heap_dtcm_end") <= address("_sstack")
    assert 0x24000000 <= address("_sdata") <= address("_ebss") <= address("__sirinos_heap_d1_start")
    assert address("__sirinos_heap_d1_end") <= address("__srnos_libc_start") < address("__srnos_libc_end") <= 0x24050000
    if "tls_initialized" in symbols:
        assert address("__tls_size") >= 12  # initialized TLS, zero TLS and errno
    assert address("__preinit_array_end") > address("__preinit_array_start")
    if "boot_check" in symbols:
        assert address("__init_array_end") > address("__init_array_start")
    sections = run("starm-readelf", "-SW", str(elf))
    for section in (".sirinos_heap_dtcm", ".sirinos_heap_d1"):
        assert re.search(re.escape(section) + r"\s+NOBITS", sections)


def main():
    for vendor in (SDK / "ThirdParty/FreeRTOS", SDK / "ThirdParty/USBDevice"):
        for line in (vendor / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ", 1)
            assert hashlib.sha256((vendor / name).read_bytes()).hexdigest() == digest, name
    for executable in ("cmake", "ninja", "starm-clang", "starm-nm", "starm-readelf"):
        if not shutil.which(executable):
            raise RuntimeError(f"Missing {executable}; add ST Arm Clang bin to PATH")
    with tempfile.TemporaryDirectory(prefix="SirinEmbedOS integration ") as temp:
        source = pathlib.Path(temp) / "Parent Project"
        shutil.copytree(SDK / "Tests/Build/Parent", source)
        build = pathlib.Path(temp) / "Build Output"
        run("cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
            "-DCMAKE_BUILD_TYPE=Debug", f"-DSIRINEMBEDOS_SOURCE_DIR={SDK}",
            f"-DCMAKE_TOOLCHAIN_FILE={SDK / 'CMake/starm-clang.cmake'}")
        run("cmake", "--build", str(build), "-j", "8")
        for name in ("RobotFirmware", "ManualFirmware", "PrototypeBindings", "ParameterExample", "BringUpExample", "SensorBringUpExample", "INSBringUpExample", "RawBinding", "AllComponents"):
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
    print("PASS: vendor hashes, parent project, nine firmware targets, C++/TLS linkage, config overrides, IRQ and heap layout")
    print("Build/static checks only; no board execution was performed.")


if __name__ == "__main__":
    main()
