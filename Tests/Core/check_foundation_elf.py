#!/usr/bin/env python3
"""Static post-link foundation checks. Does not connect to target hardware."""
import argparse
import pathlib
import re
import struct
import subprocess
import tempfile


def run(*args):
    return subprocess.check_output(args, text=True)


def check(elf):
    symbols = {}
    for line in run("starm-nm", str(elf)).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = (int(fields[0], 16), fields[1])
    def address(name):
        return symbols[name][0]

    assert 0x24000000 <= address("_sdata") <= address("_edata") <= address("_sbss")
    assert address("_sbss") <= address("_ebss") <= address("__sirinos_heap_d1_start")
    assert address("__sirinos_heap_d1_start") < address("__sirinos_heap_d1_end")
    assert address("__sirinos_heap_d1_end") <= address("__srnos_libc_start")
    assert address("_end") == address("__srnos_libc_start")
    assert address("__srnos_libc_end") <= 0x24050000
    assert address("__srnos_libc_end") - address("__srnos_libc_start") == address("SRNOS_LIBC_BYTES")
    assert 0x20000000 <= address("__sirinos_heap_dtcm_start") < address("__sirinos_heap_dtcm_end")
    assert address("__sirinos_heap_dtcm_end") <= address("_sstack")
    assert address("__dtcm_bss_end__") <= address("_sstack")
    assert address("_sstack") + address("SRNOS_MSP_BYTES") == address("_estack") == 0x20020000
    for region in ("__sirinos_heap_dtcm_start", "__sirinos_heap_d1_start", "__dma_nc_start__", "__dma_nc_buf_start__", "__bdma_buf_start__", "__debug_watch_start__", "__sirinos_noinit_start__"):
        assert address(region) % 32 == 0, region
    assert 0x30000000 <= address("__dma_nc_start__") <= address("__debug_watch_end__")
    assert address("__debug_watch_end__") <= address("__sirinos_noinit_start__") < address("__sirinos_noinit_end__") <= 0x30008000
    assert 0x38000000 <= address("__bdma_buf_start__") <= address("__bdma_buf_end__") <= 0x38004000
    sections = run("starm-readelf", "-SW", str(elf))
    bss = re.search(r"\]\s+\.bss\s+NOBITS\s+([0-9a-fA-F]+)", sections)
    assert bss and address("__tls_end") <= int(bss[1], 16)
    # Dead-code elimination may remove libc malloc entirely in a minimal image.
    for name in ("_sbrk", "sbrk"):
        if name in symbols:
            assert symbols[name][1] == "T", name

    disassembly = run("starm-objdump", "-d", str(elf))
    vectors = ["NMI_Handler", "HardFault_Handler", "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler"]
    with tempfile.TemporaryDirectory(prefix="srnos-vectors-") as temp:
        vector_file = pathlib.Path(temp) / "vectors.bin"
        subprocess.run(["starm-objcopy", "--dump-section", f".isr_vector={vector_file}", str(elf)], check=True)
        vector_bytes = vector_file.read_bytes()
        assert struct.unpack_from("<I", vector_bytes)[0] == address("_estack")
        for index, name in enumerate(vectors, 2):
            assert struct.unpack_from("<I", vector_bytes, index * 4)[0] == (address(name) | 1), name
        assert struct.unpack_from("<I", vector_bytes, 12 * 4)[0] == (address("DebugMon_Handler") | 1)
    for name in vectors + ["DebugMon_Handler"]:
        assert symbols[name][1] == "T", name
        body = disassembly.split(f"<{name}>:", 1)[1].split("\n\n", 1)[0]
        lines = [line for line in body.splitlines() if re.match(r"\s*[0-9a-f]+:", line)]
        assert re.search(r"\bmrs\s+r0, msp", lines[0]), (name, lines[0])
        assert re.search(r"\bmrs\s+r1, psp", lines[1]), (name, lines[1])
        assert re.search(r"\bmov\s+r2, lr", lines[2]), (name, lines[2])
        assert "SRNOS_STM32H7_HandleException" in body
    print(f"PASS: foundation memory, TLS, libc arena, retained sections and exception vectors: {elf}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=pathlib.Path)
    check(parser.parse_args().elf)
