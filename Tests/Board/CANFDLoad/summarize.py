#!/usr/bin/env python3
"""Check real board dumps and summarize achieved rates, without board access."""
import argparse
import json
from pathlib import Path
from decode import Result,convert,summary
import ctypes as C

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=Path)
a=p.parse_args()
all_results={}
for binary in sorted(a.directory.glob('*.bin')):
    if binary.stem not in ('classic','fd1m','fd2m','fd4m','fd5m','fd8m'):continue
    raw=binary.read_bytes();assert len(raw)==C.sizeof(Result),binary
    r=convert(Result.from_buffer_copy(raw))
    assert r['phase']==3 and r['failure']==0 and r['completed']==9,(binary,r['phase'],r['failure'])
    assert r['malformed']==r['functional_bad']==r['subscribe_result']==r['create_result']==r['delete_result']==0,binary
    assert r['functional_expected']==r['functional_received']==(64 if r['fd'] else 36),binary
    assert r['startup']['phase']==3 and r['startup']['native_can_ready'] and r['startup']['usb_ready'],binary
    for config in r['config']:
        assert config==dict(mode=r['fd'],nominal_bitrate=1000000,data_bitrate=r['data_rate'],brs=bool(r['brs'])),(binary,config)
    checks=[]
    for index,s in enumerate(r['stages']):
        assert not any(s['reordered']+s['bad']),(binary,index,'data')
        assert s['before']['ins_flags']&1 and s['after']['ins_flags']&1,(binary,index,'INS invalid')
        for k in ('ins_missing','ins_missed','errors','overruns'):
            assert s['after'][k]==s['before'][k],(binary,index,k)
        # Published/sampled counts must track each actual measurement window.
        nominal=s['seconds']*1000
        assert abs(s['after']['ins_publications']-s['before']['ins_publications']-nominal)<=1,(binary,index,'INS publications')
        for channel in (0,1):
            h=s['hardware_after'][channel]
            assert h['kernel_hz']==80000000 and h['nominal_bps']==1000000,(binary,index,'timing')
            assert h['data_bps']==(r['data_rate'] if r['fd'] else 1000000),(binary,index,'data timing')
            expected_format=0 if not r['fd'] else (768 if r['brs'] else 256)
            assert h['frame_format']==expected_format and h['cccr']&768==expected_format,(binary,index,'frame format')
            assert h['bus_off']==h['tec']==h['rec']==h['error_logging']==h['tx_pending']==0,(binary,index,'hardware')
            assert h['rx_fifo_lost']==0,(binary,index,'FIFO lost')
            for k in ('rx_overrun','rx_hw_lost'):
                assert h[k]==s['hardware_before'][channel][k],(binary,index,k)
            prefix='fd' if r['fd'] else 'classic';key='tx_expired' if r['fd'] else 'tx_oneshot_expired'
            expired=s[prefix+'_after'][channel][key]-s[prefix+'_before'][channel][key]
            assert s['accepted'][channel]-s['received'][channel]==expired,(binary,index,'unexplained loss',expired)
            if not expired:assert s['gaps'][channel]==0,(binary,index,'unexpected gap')
            assert abs(s['after']['samples'][channel]-s['before']['samples'][channel]-nominal)<=1,(binary,index,'IMU rate')
        delivered=s['accepted']==s['received'] and not any(s['rejected']+s['gaps'])
        produced=abs(s['producer_ticks']-nominal)<=1
        # Allow at most one producer batch at either endpoint; draining an
        # arbitrarily large backlog must never turn a slow window into a pass.
        active_rate=all(abs(s['received_active'][ch]-s['requested_fps'][ch]*s['elapsed_us']/1e6)
                        <=max(1,s['requested_fps'][ch]/1000) for ch in (0,1))
        checks.append({'stage':index,'requested_load_achieved':delivered and produced and active_rate})
    s=summary(r);s['checks']=checks
    binary.with_suffix('.json').write_text(json.dumps(r,indent=2)+'\n')
    binary.with_name(binary.stem+'-summary.json').write_text(json.dumps(s,indent=2)+'\n')
    all_results[binary.stem]=s
    print(f'PASS {binary.stem}: function, payload integrity, hardware, queue accounting and IMU; achieved stages {[c["stage"] for c in checks if c["requested_load_achieved"]]}')
for name,index in (('classic',5),('fd8m',6)):
    if name in all_results:
        r=all_results[name];assert r['checks'][index]['requested_load_achieved'],name
        assert r['stages'][index]['accepted']==[180000,180000],name
        print(f'PASS {name}: 60 s, each controller 3000 RX + 3000 TX per second, 360000 wire frames')
(a.directory/'summary.json').write_text(json.dumps(all_results,indent=2)+'\n')
