#!/usr/bin/env python3
"""Check fixed 60 MHz I2C4 fast-mode timing margins, not physical signal quality.

Timing equations: ST I2C TIMINGR/AN4235; H723 DS13313 rev5 table106 AF=50..80ns.
Assume rise/fall <=300ns, DNF=0. Revisit when clocks/filter/pull-ups change.
"""
from fractions import Fraction as F
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[2]
header = (root / 'Board/Support/IO/i2c_timing.h').read_text()
ioc = (root / 'Board/OMNICTL2_3.ioc').read_text()
generated = (root / 'Board/Core/Src/i2c.c').read_text()
timing = int(re.search(r'#define SRNOS_BOARD_I2C4_TIMING (0x[0-9A-F]+)U', header)[1], 16)
assert timing == int(re.search(r'I2C4.Timing=(0x[0-9A-F]+)', ioc)[1], 16)
assert timing == int(re.search(r'hi2c4.Init.Timing = (0x[0-9A-F]+);', generated)[1], 16)
assert 'RCC.I2C4Freq_Value=60000000' in ioc
assert 'I2C4.I2C_Speed_Mode=I2C_Fast' in ioc
presc, scldel, sdadel, sclh, scll = [(timing >> shift) & mask for shift, mask in [(28,15),(20,15),(16,15),(8,255),(0,255)]]
tclk = F(1000000000, 60000000)
tpresc = (presc+1)*tclk
afmin, afmax, dnf, edge_max = 50, 80, 0, 300
setup = (scldel+1)*tpresc
hold = sdadel*tpresc
hold_min = max(0, edge_max-afmin-(dnf+3)*tclk)
hold_max = 900-edge_max-afmax-(dnf+4)*tclk
assert setup >= edge_max+100
assert hold_min <= hold <= hold_max
sync_min = afmin+(dnf+2)*tclk
low_min = (scll+1)*tpresc+sync_min
high_min = (sclh+1)*tpresc+sync_min
assert low_min >= 1300 and high_min >= 600
period_min = low_min+high_min
assert period_min >= 2500
assert tclk < (low_min-afmin-dnf*tclk)/4 and tclk < high_min
print(json.dumps(dict(timing=hex(timing), kernel_hz=60000000, presc=presc,
                     scldel=scldel, sdadel=sdadel, sclh=sclh, scll=scll,
                     setup_ns=float(setup), hold_ns=float(hold),
                     hold_range_ns=[float(hold_min),float(hold_max)],
                     scl_low_min_ns=float(low_min), scl_high_min_ns=float(high_min),
                     fastest_hz=float(F(1000000000,period_min)),
                     max_rise_fall_assumption_ns=edge_max), indent=2))
