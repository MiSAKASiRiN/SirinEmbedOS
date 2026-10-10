#!/usr/bin/env python3
"""Decode version-1/2 CAN4 measurements alongside the unchanged main result ABI."""
import ctypes as C
import json
import sys
from pathlib import Path
import decode
U=C.c_uint32
def performance_type(width):
 class Performance(C.LittleEndianStructure):
    _fields_=[('version',U),('stage_count',U),('windows',U*11),('cpu_ppm',(U*60)*11),
              ('owner_before',(U*width)*11),('owner_after',(U*width)*11),('task_names',(C.c_char*16)*48),
              ('task_cpu_sum',(U*48)*11),('task_cpu_samples',(U*48)*11)]
 return Performance
assert C.sizeof(performance_type(8))==8388
assert C.sizeof(performance_type(10))==8564

for path in sys.argv[1:]:
    p=Path(path);raw=p.read_bytes();width=10 if len(raw)==8564 else 8
    v=performance_type(width).from_buffer_copy(raw);assert v.version in (1,2) and v.stage_count==11
    result=decode.base.convert(decode.Result.from_buffer_copy(p.with_name(p.stem.removesuffix('-perf')+'.bin').read_bytes()))
    summary=decode.summarize(result)
    out=[]
    for index,s in enumerate(summary['stages']):
        before=v.owner_before[index];after=v.owner_after[index]
        cpu=[v.cpu_ppm[index][w]/10000 for w in range(v.windows[index])]
        tasks={bytes(v.task_names[t]).split(b'\0')[0].decode():round(v.task_cpu_sum[index][t]/v.task_cpu_samples[index][t]/10000,3)
               for t in range(48) if v.task_cpu_samples[index][t]}
        count=s['received'][0]+s['accepted'][1]
        physical=s['spi'][3]['delta']['completed']
        imu_spi_ok=all(not any(x['delta'][k] for k in ('queue_full','queue_expired','errors','timeouts','recoveries')) for x in s['spi'][:3])
        # The lower-priority observer can close an overload window late. Use
        # measured duration for IMU rate; CAN producer adherence still uses
        # the requested duration and may independently fail.
        expected_samples=result['stages'][index]['elapsed_us']/1000
        imu_rate_ok=all(abs(n-expected_samples)<=1 for n in s['observed_samples']) and abs(s['ins_publications']-expected_samples)<=1
        imu_ok=imu_spi_ok and imu_rate_ok and not any(s['sensor_errors'][:2]+s['sensor_overruns'][:2]) and not s['ins_missing'] and not s['ins_missed'] and max(s['max_imu_period_us'])<=1200 and max(s['max_imu_acquisition_us'])<=200 and max(x['max_queue_since_boot_us'] for x in s['spi'][:3])<=150
        can_ok=s['achieved'] and not any(s['expired']) and not s['external_delta']['rx_hw_overflow'] and not s['native_rx_lost'] and not s['native_ring_lost']
        out.append(dict(index=index,cpu_10s_percent=cpu,task_cpu_percent=tasks,wakeups=after[0]-before[0],rounds=after[1]-before[1],
            rx_service_us=after[2]-before[2],tx_service_us=after[3]-before[3],max_rx_service_us=after[4],max_tx_service_us=after[5],
            dma_launch_attempts=after[8]-before[8] if width==10 else None,spi_all_completed=after[9]-before[9] if width==10 else None,uncertain_rx=after[6]-before[6],uncertain_tx=after[7]-before[7],physical_spi_transactions=physical,spi_transactions_per_frame=round(physical/count,3) if count else None,
            imu_guard_pass=imu_ok,can_pass=can_ok,cpu_pass=max(cpu)<=60 if cpu else None,**{k:s[k] for k in ('requested_fps','seconds','cpu_percent','producer_ticks','max_imu_period_us','max_imu_acquisition_us')}))
    p.with_suffix('.json').write_text(json.dumps(out,indent=2)+'\n')
    for x in out:print(x['index'],x['requested_fps'],'CAN',x['can_pass'],'CPU',x['cpu_percent'],'max10s',max(x['cpu_10s_percent'],default=0),'IMU',x['imu_guard_pass'],'SPI/frame',x['spi_transactions_per_frame'])
