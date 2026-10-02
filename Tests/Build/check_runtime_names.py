#!/usr/bin/env python3
"""Check active SDK names and global symbols, with exact vendor/ABI exceptions."""
import argparse
import collections
import json
from pathlib import Path
import re
import shlex
import subprocess

SDK = Path(__file__).resolve().parents[2]
RUNTIME_ROOTS = ('Include', 'Source', 'Config', 'Board/Support')
CODE_SUFFIXES = {'.c', '.cpp', '.h', '.hpp', '.s', '.S'}

# These four files are Microchip's retained register/driver implementation.
# The SDK-owned SPIAdapter.c beside them is deliberately NOT exempt.
VENDOR_FILES = {
    'Source/Devices/MCP2518FD/drv_canfdspi_api.c',
    'Source/Devices/MCP2518FD/vendor/drv_canfdspi_api.h',
    'Source/Devices/MCP2518FD/vendor/drv_canfdspi_defines.h',
    'Source/Devices/MCP2518FD/vendor/drv_canfdspi_register.h',
}

# A function is exempt only at its actual owner, not because it starts HAL_/USBD_.
# These names are fixed by HAL weak callbacks, vector tables, the USB middleware,
# FreeRTOS hooks, Picolibc retargeting, or Microchip's SPI adapter contract.
ABI_SYMBOLS = {
    'Source/IO/SPI/SPI.c': {
        'HAL_SPI_ErrorCallback', 'HAL_SPI_RxCpltCallback',
        'HAL_SPI_TxCpltCallback', 'HAL_SPI_TxRxCpltCallback',
    },
    'Source/IO/I2C/I2C.c': {
        'HAL_I2C_AbortCpltCallback', 'HAL_I2C_ErrorCallback',
        'HAL_I2C_MasterRxCpltCallback', 'HAL_I2C_MasterTxCpltCallback',
        'HAL_I2C_MemRxCpltCallback', 'HAL_I2C_MemTxCpltCallback',
    },
    'Source/IO/CAN/Native/FDCAN.c': {
        'HAL_FDCAN_ErrorCallback', 'HAL_FDCAN_RxFifo0Callback',
        'HAL_FDCAN_TxBufferCompleteCallback', 'HAL_FDCAN_TxEventFifoCallback',
    },
    'Source/Devices/MCP2518FD/SPIAdapter.c': {'DRV_SPI_TransferData'},
    'Source/Services/Logging/logging_service.c': {'__io_getchar', '__io_putchar'},
    'Source/Kernel/hooks.c': {
        'vApplicationGetIdleTaskMemory', 'vApplicationGetTimerTaskMemory',
        'vApplicationMallocFailedHook', 'vApplicationStackOverflowHook',
        'vApplicationTickHook',
    },
    'Board/Support/Devices/monitor_adc_board.c': {
        'HAL_ADC_ConvCpltCallback', 'HAL_ADC_ErrorCallback',
    },
    'Board/Support/Devices/indicators_board.c': {
        'DMA1_Stream0_IRQHandler', 'DMA1_Stream1_IRQHandler',
    },
    'Board/Support/IO/can_board.cpp': {'HAL_GPIO_EXTI_Callback'},
    'Board/Support/System/fault_handlers.c': {
        'BusFault_Handler', 'DebugMon_Handler', 'HardFault_Handler',
        'MemManage_Handler', 'NMI_Handler', 'UsageFault_Handler',
    },
    'Board/Support/System/libc_locks.c': {
        '__lock___libc_recursive_mutex', '__retarget_lock_acquire',
        '__retarget_lock_acquire_recursive', '__retarget_lock_close',
        '__retarget_lock_close_recursive', '__retarget_lock_init',
        '__retarget_lock_init_recursive', '__retarget_lock_release',
        '__retarget_lock_release_recursive',
    },
    'Board/Support/System/libc_memory.c': {'_sbrk', 'sbrk'},
    'Board/Support/USB/usb_device_board.c': {
        'HAL_PCD_ConnectCallback', 'HAL_PCD_DataInStageCallback',
        'HAL_PCD_DataOutStageCallback', 'HAL_PCD_DisconnectCallback',
        'HAL_PCD_ISOINIncompleteCallback', 'HAL_PCD_ISOOUTIncompleteCallback',
        'HAL_PCD_MspDeInit', 'HAL_PCD_MspInit', 'HAL_PCD_ResetCallback',
        'HAL_PCD_ResumeCallback', 'HAL_PCD_SOFCallback',
        'HAL_PCD_SetupStageCallback', 'HAL_PCD_SuspendCallback',
        'USBD_LL_ClearStallEP', 'USBD_LL_CloseEP', 'USBD_LL_DeInit',
        'USBD_LL_Delay', 'USBD_LL_FlushEP', 'USBD_LL_GetRxDataSize',
        'USBD_LL_Init', 'USBD_LL_IsStallEP', 'USBD_LL_OpenEP',
        'USBD_LL_PrepareReceive', 'USBD_LL_SetUSBAddress', 'USBD_LL_StallEP',
        'USBD_LL_Start', 'USBD_LL_Stop', 'USBD_LL_Transmit',
    },
}

# Fixed linker-script symbols are references, not self-maintained declarations.
LINKER_REFERENCES = {
    'Board/Support/System/libc_memory.c': {'__srnos_libc_start', '__srnos_libc_end'},
}

LEGACY_FILE = re.compile(r'^(?:tskptt|lib|bsp|adp|srn|ntfdcan|drv)_', re.I)
LEGACY_NAME = re.compile(
    r'^(?:tskptt_|lib_|bsp_|adp_|srn_|omximu_|ntfdcan_|drv_spi_|'
    r'SRNOS_(?:LIB|ADP|TSKPTT|NTFDCAN)_|SIRINOS_|__SRNOS)', re.I)
LEGACY_COMPONENT = re.compile(r'(?:^|_)SRN_(?:LOG|LINK)(?:_|$)|DbgSrn(?:Log|Link)')
# Comments and literals describe provenance and fixed wire values, not identifiers.
C_NON_CODE = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
ABI_MACROS = json.loads((Path(__file__).with_name('runtime_names_abi.json')).read_text())


def source_errors(relative, source):
    if relative in VENDOR_FILES:
        return []
    errors = []
    if LEGACY_FILE.match(Path(relative).name):
        errors.append(f'{relative}: legacy self-maintained filename')
    code = C_NON_CODE.sub(lambda match: '\n' * match[0].count('\n'), source)
    allowed_macros = ABI_MACROS.get(relative, {}).get('macros', [])
    for macro in re.findall(r'^\s*#\s*define\s+(\w+)', code, re.M):
        if not macro.startswith('SRNOS_') and macro not in allowed_macros:
            errors.append(f'{relative}: self-maintained macro {macro}')
    # Unscoped enum constants become identifiers in the surrounding C/C++ scope.
    # Scoped enum-class members retain their class-owned vocabulary.
    for enum in re.finditer(r'\benum(?!\s+(?:class|struct)\b)(?:\s+\w+)?(?:\s*:\s*[\w:]+)?\s*\{([^{}]*)\}', code):
        body = re.sub(r'\([^()]*\)', '', enum[1])
        for item in body.split(','):
            name = re.match(r'\s*(\w+)\s*(?:=|$)', item)
            if name and not name[1].startswith('SRNOS_'):
                errors.append(f'{relative}: unscoped enum constant {name[1]}')
    if Path(relative).suffix in ('.h', '.hpp'):
        guards = re.findall(r'^\s*#\s*ifndef\s+(\w+)\s*\n\s*#\s*define\s+\1\s*$', code, re.M)
        for guard in guards:
            if not guard.startswith('SRNOS_'):
                errors.append(f'{relative}: self-maintained header guard {guard}')
    for match in re.finditer(r'\b[A-Za-z_]\w*\b', code):
        name = match[0]
        # This exact supplier entry point is required in the private adapter header.
        if name == 'DRV_SPI_TransferData' and relative == 'Source/Devices/MCP2518FD/spi_adapter_internal.h':
            continue
        if (LEGACY_NAME.match(name) or LEGACY_COMPONENT.search(name)) and name not in (
                ABI_SYMBOLS.get(relative, set()) | LINKER_REFERENCES.get(relative, set())):
            line = code.count('\n', 0, match.start()) + 1
            errors.append(f'{relative}:{line}: legacy identifier {name}')
    return errors


def symbol_allowed(relative, name):
    if name.startswith(('SRNOS_', 'SRNOS::')):
        return True
    # C++ emits these external symbols for valid namespaced SDK classes.
    if re.match(r'^(?:vtable|VTT|typeinfo|typeinfo name) for SRNOS::', name):
        return True
    if re.match(r'^(?:non-virtual|virtual|covariant return) thunk to SRNOS::', name):
        return True
    return name in ABI_SYMBOLS.get(relative, set())


def classify_input(source):
    """Every compile input gets a category; never exempt all of Source/USB etc."""
    if source.parent.name == 'srnos_usb' and source.name in ('usbd_composite.c', 'usbd_cdc_acm.c'):
        if source.name == 'usbd_composite.c':
            upstream = SDK / 'ThirdParty/USBDevice/Class/COMPOSITE/Src/usbd_composite.c'
            changes = (
                ('#include "usbd_ctlreq.h"', '#include "usbd_ctlreq.h"\n#include "usb_cdc_channels.h"'),
                ('char str_buffer[16] = "";', 'const char *str_buffer = NULL;'),
                ('snprintf(str_buffer, sizeof(str_buffer), CDC_ACM_STR_DESC, i);',
                 'str_buffer = SRNOS_USBCdcInterfaceLabel(i);'),
            )
        else:
            upstream = SDK / 'ThirdParty/USBDevice/Class/CDC_ACM/Src/usbd_cdc_acm.c'
            changes = (
                ('desc[2] = cmd_itf;', 'desc[2] = cmd_itf;\n    desc[7] = str_idx;'),
                ('desc[45] = com_itf;', 'desc[45] = com_itf;\n    desc[51] = str_idx;'),
            )
        expected = upstream.read_text()
        for before, after in changes:
            if before not in expected:
                raise ValueError(f'Upstream patch anchor changed: {upstream}: {before}')
            expected = expected.replace(before, after)
        if source.read_text() != expected:
            raise ValueError(f'Generated USB vendor copy differs from reviewed build-time patches: {source}')
        return 'verified build-time USB vendor patch'
    if not source.is_relative_to(SDK):
        return 'parent application'
    relative = source.relative_to(SDK).as_posix()
    if relative in VENDOR_FILES:
        return 'retained Microchip implementation'
    if relative.startswith(('Source/', 'Board/Support/')):
        return 'self-maintained SDK'
    if relative.startswith('Board/Core/') or relative == 'Board/startup_stm32h723xx.s':
        return 'CubeMX generated'
    if relative.startswith('Board/Drivers/'):
        return 'CubeMX vendor HAL/CMSIS'
    if relative.startswith(('ThirdParty/FreeRTOS/', 'ThirdParty/USBDevice/')):
        return 'pinned third-party middleware'
    if relative.startswith(('Examples/', 'Tests/Build/Parent/')):
        return 'application or explicit test adapter'
    raise ValueError(f'Unclassified active compile input: {relative}')


def check_objects(database):
    failures, categories = [], collections.Counter()
    checked = set()
    for command in json.loads(database.read_text()):
        directory = Path(command['directory'])
        source = (directory / command['file']).resolve()
        category = classify_input(source)
        categories[category] += 1
        if category != 'self-maintained SDK':
            continue
        relative = source.relative_to(SDK).as_posix()
        if not source.is_file():
            failures.append(f'{relative}: obsolete compile input; regenerate this compile database first')
            continue
        args = command.get('arguments') or shlex.split(command['command'])
        output = command.get('output')
        if not output:
            output = args[args.index('-o') + 1]
        obj = (directory / output).resolve()
        if obj in checked:
            continue
        checked.add(obj)
        if not obj.exists() or obj.stat().st_mtime_ns < source.stat().st_mtime_ns:
            failures.append(f'{relative}: missing/outdated current object; build this compile database first')
            continue
        result = subprocess.run(['starm-nm', '-g', '--defined-only', '--demangle', str(obj)],
                                text=True, capture_output=True)
        if result.returncode:
            failures.append(f'{relative}: nm failed: {result.stderr.strip()}')
            continue
        for line in result.stdout.splitlines():
            fields = line.split(None, 2)
            if len(fields) == 3 and not symbol_allowed(relative, fields[2]):
                failures.append(f'{relative}: external definition {fields[2]}')
    actual_sources = {(Path(c['directory']) / c['file']).resolve()
                      for c in json.loads(database.read_text())}
    for root in ('Source', 'Board/Support'):
        for source in (SDK / root).rglob('*'):
            if source.suffix in ('.c', '.cpp') and source.resolve() not in actual_sources:
                failures.append(f'{source.relative_to(SDK)}: implementation missing from current compile database')
    return failures, len(checked), categories


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--compile-commands', type=Path,
                      help='Built, current compile_commands.json; inspect only its exact output objects')
    mode.add_argument('--source-only', action='store_true',
                      help='Check filenames/legacy identifiers without claiming linked-symbol coverage')
    arguments = parser.parse_args()
    failures, count = [], 0
    for root in RUNTIME_ROOTS:
        for source in (SDK / root).rglob('*'):
            if source.is_file() and source.suffix in CODE_SUFFIXES:
                count += 1
                failures.extend(source_errors(source.relative_to(SDK).as_posix(), source.read_text()))
    if arguments.compile_commands:
        object_errors, objects, categories = check_objects(arguments.compile_commands.resolve(strict=True))
        failures.extend(object_errors)
    for failure in sorted(set(failures)):
        print('FAIL:', failure)
    if failures:
        raise SystemExit(f'{len(set(failures))} active SDK naming violations')
    print(f'PASS: {count} active SDK source/header names; {len(VENDOR_FILES)} exact Microchip file exceptions')
    if arguments.compile_commands:
        print(f'PASS: {objects} current SDK objects have SRNOS external definitions or exact owner/ABI exceptions')
        print('Compile input classification:', ', '.join(f'{key}={value}' for key, value in sorted(categories.items())))
    else:
        print('Source-only check; external definitions were not examined.')


if __name__ == '__main__':
    main()
