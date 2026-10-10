#!/usr/bin/env python3
"""Compile the pinned HAL's status calculation and callback branch unchanged.

Only this slice of HAL_FDCAN_IRQHandler is needed by the host register model;
this does not emulate arbitration, hardware timing or the entire IRQ handler.
"""
import pathlib
import re
import sys
hal = pathlib.Path(sys.argv[1]).read_text()
def block(marker):
    start = hal.index(marker, hal.index('void HAL_FDCAN_IRQHandler('))
    brace = hal.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (hal[end] == '{') - (hal[end] == '}')
        end += 1
    return hal[start:end]
mask = re.search(r'^#define FDCAN_ERROR_STATUS_MASK .*$', hal, re.M).group(0)
setup = re.search(r'  ErrorStatusITs = hfdcan->Instance->IR & FDCAN_ERROR_STATUS_MASK;\s*ErrorStatusITs &= hfdcan->Instance->IE;', hal).group(0)
pathlib.Path(sys.argv[2]).write_text('#include "stm32h7xx_hal.h"\n'+mask+'\nvoid HAL_FDCAN_IRQHandler(FDCAN_HandleTypeDef *hfdcan) {\nuint32_t ErrorStatusITs;\n'+setup+'\n'+block('if (ErrorStatusITs != 0U)')+'\n'+block('if (hfdcan->ErrorCode != HAL_FDCAN_ERROR_NONE)')+'\n}\n')
