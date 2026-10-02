import json, pathlib, shutil
p=pathlib.Path('build/can13-board-20260928'); dst=pathlib.Path('Docs/Validation/CAN13-ADC-20260928')
x=json.loads((p/'steady.json').read_text()); assert (x['phase'],x['completed'],x['failure'],x['malformed'])==(3,9,0,0)
rows=[]
for i,s in enumerate(x['stages']):
 assert s['accepted']==s['received']
 assert not any(s['gaps']+s['bad']+s['reordered'])
 for h in s['hardware_after']:
  assert h['nominal_bps']==1000000
  assert not any(h[k] for k in ['rx_overrun','bus_off','tec','rec','error_logging','tx_pending','rx_fifo_lost'])
 for a,b in zip(s['router_before'],s['router_after']):
  assert all(a[k]==b[k] for k in ['rx_dropped','tx_oneshot_expired'])
 a,b=s['before'],s['after']
 assert a['ins_flags']&1 and b['ins_flags']&1
 assert b['ins_publications']-a['ins_publications']==s['seconds']*1000
 assert a['ins_missing']==b['ins_missing'] and a['ins_missed']==b['ins_missed']
 assert a['errors']==b['errors']==[0]*4 and a['overruns']==b['overruns']==[0]*4
 rows.append(dict(stage=i,requested_fps=s['requested_fps'],elapsed_us=s['elapsed_us'],received_active=s['received_active'],actual_received_fps=sum(s['received_active'])*1e6/s['elapsed_us'],accepted=s['accepted'],received_after_drain=s['received'],rejected=s['rejected'],cpu_percent=s['cpu_ppm']/10000,monitor_percent=s['monitor_ppm']/10000,monitor_critical_us=s['critical_us'],ins_phase=[a['ins_phase'],b['ins_phase']],ins_flags=[a['ins_flags'],b['ins_flags']],ins_publications_delta=b['ins_publications']-a['ins_publications'],sensor_samples_delta=[v-u for u,v in zip(a['samples'],b['samples'])]))
out=dict(warmup_ms=x['warmup_ms'],total_delivered=sum(sum(s['accepted']) for s in x['stages']),total_rejected=sum(sum(s['rejected']) for s in x['stages']),checks='PASS: accepted delivered; sequence, hardware, TTL, sensor counters; INS valid and 1 kHz publication',stages=rows)
(p/'steady-summary.json').write_text(json.dumps(out,indent=2)+'\n')
for f in ['steady.json','steady-summary.json','summarize_steady.py']:
 shutil.copy2(p/f,dst/f)
shutil.copy2(p/'final-restored/metadata.json',dst/'final-restored-metadata.json')
print(json.dumps(out,indent=2))
