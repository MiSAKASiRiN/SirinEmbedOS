#!/usr/bin/env python3
"""Run portable SDK tests. Hardware/RTOS edges use fakes; no board access."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SUITES = (
    "Core", "Kernel", "Algorithms/Control", "Algorithms/Math", "Algorithms/Power",
    "IO/CAN/Router", "IO/SPI", "IO/I2C", "Devices/EEPROM", "Devices/MCP2518FD", "Devices/IMU", "Devices/Sensors", "Services/Indicators", "Serial", "Services/HostLink/Codec",
    "Services/HostLink", "Services/Parameters", "Services/Parameters/HostLink",
    "Services/Storage", "Services/Telemetry", "Services/Logging", "Services/USB", "Services/IMUTelemetry", "Services/INS", "Services/SystemMonitor",
)
failed = []
for suite in SUITES:
    script = ROOT / "Tests" / suite / "run_host_tests.sh"
    result = subprocess.run(["bash", str(script)], cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(f"{'FAIL' if result.returncode else 'PASS'}: {suite}", flush=True)
    if result.returncode:
        failed.append(suite)
        print(result.stdout, flush=True)
if failed:
    sys.exit(1)
print(f"PASS: {len(SUITES)} host suites; no hardware execution")
