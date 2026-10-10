#!/usr/bin/env python3
"""Create reviewable tables from decode.py summaries; never equate run completion with load success."""
import json
from pathlib import Path
import sys

def collect(directory):
    summaries={p.name.removesuffix('-summary.json'):json.loads(p.read_text())
               for p in sorted(directory.glob('*-summary.json'))}
    assert summaries
    rows=['# 完整负载窗口', '', '速率按活动窗口接收计；丢失是接纳减最终接收减TTL，需结合硬件证据。方向均为1→4 / 4→1；CPU包含全部板载服务。Aligned仅为测试专用采样点配置。', '',
          '| 镜像/档 | B | 秒 | 请求帧/s | 实际帧/s | CPU % | 生产周期/期望 | 拒绝 | TTL | 丢失 | MCP硬件RX溢出事件 | 达标 |',
          '|---|---:|---:|---|---|---:|---|---|---|---|---:|---|']
    observations={}
    for name, r in summaries.items():
        assert r['phase']==3 and r['malformed']==0 and r['functional_bad']==0, name
        for s in r['stages']:
            assert all(s['accepted'][i]==s['received'][i]+s['expired'][i]+s['unexplained_loss'][i] for i in (0,1))
            assert all(v>=0 for v in s['unexplained_loss'])
            assert not any(s['bad']+s['reordered']), (name,s['index'])
            pair=lambda v:' / '.join(str(x) for x in v)
            rows.append(f"| {name}/{s['index']} | {s['length']} | {s['seconds']} | {pair(s['requested_fps'])} | {pair(s['active_rx_fps'])} | {s['cpu_percent']:.2f} | {s['producer_ticks']}/{s['seconds']*1000} | {pair(s['rejected'])} | {pair(s['expired'])} | {pair(s['unexplained_loss'])} | {s['external_delta']['rx_hw_overflow']} | {'是' if s['achieved'] else '否'} |")
        if not r['stages']: continue
        stages=r['stages']
        observations[name]={
            'windows':len(stages), 'duration_seconds':sum(s['seconds'] for s in stages),
            'max_imu_completion_period_us':[max(s['max_imu_period_us'][i] for s in stages) for i in (0,1)],
            'max_imu_acquisition_us':[max(s['max_imu_acquisition_us'][i] for s in stages) for i in (0,1)],
            'imu_period_over_1500':[sum(s['imu_period_over_1500'][i] for s in stages) for i in (0,1)],
            'imu_samples':[sum(s['sensor_samples'][i] for s in stages) for i in (0,1)],
            'ins_publications':sum(s['ins_publications'] for s in stages),
            'ins_missing':sum(s['ins_missing'] for s in stages),
            'ins_missed':sum(s['ins_missed'] for s in stages),
            'sensor_errors':[sum(s['sensor_errors'][i] for s in stages) for i in range(4)],
            'sensor_overruns':[sum(s['sensor_overruns'][i] for s in stages) for i in range(4)],
            'environment_rate_range_hz':[[min(s['sensor_samples'][i]/s['seconds'] for s in stages),max(s['sensor_samples'][i]/s['seconds'] for s in stages)] for i in (2,3)],
            'spi_error_totals':{k:sum(q['delta'][k] for s in stages for q in s['spi']) for k in ('queue_full','queue_expired','errors','timeouts','recoveries')},
            'spi_max_queue_since_boot_us':[max(s['spi'][i]['max_queue_since_boot_us'] for s in stages) for i in range(4)],
            'spi_max_transfer_since_boot_us':[max(s['spi'][i]['max_transfer_since_boot_us'] for s in stages) for i in range(4)],
            'peak_mcp_transactions_per_second':max(s['spi'][3]['transactions_per_second'] for s in stages),
            'min_stack_free_bytes':{s['name']:s['free_bytes'] for s in r['stacks']},
            'resources':r['resources']}
    (directory/'loads.md').write_text('\n'.join(rows)+'\n')
    (directory/'summary.json').write_text(json.dumps(summaries,indent=2)+'\n')
    (directory/'observations.json').write_text(json.dumps(observations,indent=2)+'\n')
    print(f'PASS: {len(summaries)} results; {sum(len(s["stages"]) for s in summaries.values())} windows; byte/sequence and accounting consistency checked. See achieved for per-window load verdicts.')
    for n,o in observations.items():print(n,json.dumps({k:o[k] for k in ('max_imu_completion_period_us','max_imu_acquisition_us','spi_max_queue_since_boot_us','spi_error_totals','ins_missing','ins_missed')}))

if __name__=='__main__':collect(Path(sys.argv[1]))
