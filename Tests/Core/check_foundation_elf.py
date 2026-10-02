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
    strong_handlers = {}
    for line in run("starm-nm", str(elf)).splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = (int(fields[0], 16), fields[1])
            if fields[1] == 'T' and fields[2].endswith(('Handler', 'IRQHandler')):
                strong_handlers[fields[2]] = strong_handlers.get(fields[2], 0) + 1
    def address(name):
        return symbols[name][0]

    # The fixed native CAN workers must override driver fallbacks. Weak
    # declarations once weakened both definitions and silently linked no-op
    # notifications: hardware RX succeeded while the worker slept forever.
    for name in ("SRNOS_FDCANOnRxISR", "SRNOS_FDCANOnTxEvent"):
        assert name in symbols and symbols[name][1] == "T", (
            f"{name}: native CAN notification must be a strong router definition")

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
    for region in ("__sirinos_heap_dtcm_start", "__sirinos_heap_d1_start", "__dma_nc_start__", "__dma_nc_buf_start__", "__bdma_buf_start__", "__debug_watch_start__", "__sirinos_noinit_start__", "SRNOS_ITCMBssStart", "SRNOS_ITCMBssEnd", "__itcm_text_load_start__"):
        assert address(region) % 32 == 0, region
    assert 0x30000000 <= address("__dma_nc_start__") <= address("__dma_nc_buf_end__")
    assert address("__dma_nc_buf_end__") <= address("__sirinos_noinit_start__") < address("__sirinos_noinit_end__") <= 0x30008000
    assert 0 == address("__itcm_text_start__") < address("__itcm_text_end__")
    assert address("__itcm_text_end__") <= address("SRNOS_ITCMBssStart") < address("SRNOS_ITCMBssEnd")
    assert address("SRNOS_ITCMBssEnd") <= address("__debug_watch_start__") < address("__debug_watch_end__") <= 0x10000
    # Linker and early-main initialization prerequisites: CPU-only context must live in
    # NOLOAD ITCM, with no DMA or Fault retained object inadvertently moved.
    symbol_table = run("starm-objdump", "-t", str(elf))
    assert re.search(r"\bO\s+\.itcm_bss\s+[0-9a-f]+\s+_ZL5s_ctx$", symbol_table, re.M), "CAN Router context must reside in ITCM"
    for line in symbol_table.splitlines():
        fields = line.split()
        if len(fields) >= 6 and (fields[-1].startswith("SRNOS_DebugDbg") or fields[-1] in ("SRNOS_DebugHostLink", "SRNOS_DebugLog")) and fields[2] == "O":
            assert fields[3] == ".debug_watch", line
    assert 0x38000000 <= address("__bdma_buf_start__") <= address("__bdma_buf_end__") <= 0x38004000
    # Queued SPI TX copies are ordinary D1 data; only staging is DMA-visible.
    for name, size in (("tx_stage", 512), ("rx_stage", 512)):
        if name in symbols:
            assert address(name) % 32 == 0, name
            assert 0x30000000 <= address(name) < address(name) + size <= 0x30008000, name
    for name in ("s_i2c_tx_stage", "s_i2c_rx_stage"):
        if name in symbols:
            assert address(name) % 32 == 0, name
            assert 0x38000000 <= address(name) < address(name) + 256 <= 0x38004000, name
    sections = run("starm-readelf", "-SW", str(elf))
    assert re.search(r"\]\s+\.itcm_bss\s+NOBITS\s+", sections)
    assert re.search(r"\]\s+\.debug_watch\s+NOBITS\s+", sections)
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
        subprocess.run(["starm-objcopy", "--dump-section", f".isr_vector={vector_file}", str(elf), str(pathlib.Path(temp) / "inspected.elf")], check=True)
        vector_bytes = vector_file.read_bytes()
        assert struct.unpack_from("<I", vector_bytes)[0] == address("_estack")
        for index, name in enumerate(vectors, 2):
            assert struct.unpack_from("<I", vector_bytes, index * 4)[0] == (address(name) | 1), name
        assert struct.unpack_from("<I", vector_bytes, 12 * 4)[0] == (address("DebugMon_Handler") | 1)
        # STM32H723 vector indices: core exceptions plus IRQn + 16.
        for index, name in ((11, 'SVC_Handler'), (14, 'PendSV_Handler'),
                            (15, 'SysTick_Handler'), (27, 'DMA1_Stream0_IRQHandler'),
                            (28, 'DMA1_Stream1_IRQHandler'), (71, 'TIM7_IRQHandler')):
            assert strong_handlers.get(name) == 1, name
            assert struct.unpack_from('<I', vector_bytes, index * 4)[0] == (address(name) | 1), name
    for name in vectors + ["DebugMon_Handler"]:
        assert symbols[name][1] == "T", name
        body = disassembly.split(f"<{name}>:", 1)[1].split("\n\n", 1)[0]
        lines = [line for line in body.splitlines() if re.match(r"\s*[0-9a-f]+:", line)]
        assert re.search(r"\bmrs\s+r0, msp", lines[0]), (name, lines[0])
        assert re.search(r"\bmrs\s+r1, psp", lines[1]), (name, lines[1])
        assert re.search(r"\bmov\s+r2, lr", lines[2]), (name, lines[2])
        assert "SRNOS_STM32H7_HandleException" in body
    print(f"PASS: foundation memory, TLS, libc arena, retained sections, exception vectors and strong CAN notifications: {elf}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=pathlib.Path)
    check(parser.parse_args().elf)
