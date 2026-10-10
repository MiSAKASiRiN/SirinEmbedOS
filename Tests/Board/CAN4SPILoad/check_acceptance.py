#!/usr/bin/env python3
"""Check decoded final Classic hardware results without accessing the board."""
from pathlib import Path
import sys
import json
b=Path(sys.argv[1])
s=json.loads((b/'classic-final-summary.json').read_text());p=json.loads((b/'classic-final-perf.json').read_text())
checks={
 'classic_1m_8b':s['fd']==0 and all(x['length']==8 for x in s['stages']),
 'all_stages_completed':s['phase']==3 and s['completed']==11 and not s['failure'] and not s['malformed'],
 'all_classic_formats':s['functional_expected']==s['functional_received']==36 and s['functional_bad']==0,
 'all_imu_guards_including_overload':all(x['imu_guard_pass'] for x in p),
 'recovery':p[9]['can_pass'],
 'true_overload_requested':s['stages'][8]['requested_fps']==[6000,6000],
}
for i,seconds in [(5,60),(6,60),(7,60),(10,600)]:
 x=s['stages'][i];y=p[i]
 checks[f'target_{i}_duration_and_load']=x['requested_fps']==[3000,3000] and x['seconds']==seconds
 checks[f'target_{i}_delivery_and_rate']=y['can_pass']
 checks[f'target_{i}_complete_cpu_windows']=len(y['cpu_10s_percent'])==seconds//10
 checks[f'target_{i}_cpu_le_60']=bool(y['cpu_10s_percent']) and max(y['cpu_10s_percent'])<=60
 checks[f'target_{i}_spi_no_errors']=all(not any(q['delta'][k] for k in ['queue_full','queue_expired','errors','timeouts','recoveries']) for q in x['spi'])
 checks[f'target_{i}_no_uncertain_commits']=y['uncertain_rx']==y['uncertain_tx']==0
 checks[f'target_{i}_imu_samples']=x['sensor_samples'][:2]==x['observed_samples']==[seconds*1000]*2 and x['ins_publications']==seconds*1000
out={'pass':all(checks.values()),'checks':checks,'target_cpu_10s_max':max(v for i in [5,6,7,10] for v in p[i]['cpu_10s_percent']),'all_stages_max_imu_period_us':max(max(x['max_imu_period_us']) for x in s['stages']),'all_stages_max_acquisition_us':max(max(x['max_imu_acquisition_us']) for x in s['stages']),'all_stages_max_spi_queue_us':max(q['max_queue_since_boot_us'] for x in s['stages'] for q in x['spi'][:3])}
(b/'acceptance.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
assert out['pass']
