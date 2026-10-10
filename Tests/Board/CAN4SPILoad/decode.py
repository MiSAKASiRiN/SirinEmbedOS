#!/usr/bin/env python3
"""Decode a completed CAN1/CAN4 board test, with the verified short-enum ARM ABI."""
import ctypes as C
import importlib.util
import json
from pathlib import Path
import sys
spec=importlib.util.spec_from_file_location('can13_decoder',Path(__file__).parent.parent/'CANFDLoad/decode.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
U=C.c_uint32
class MCP(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'ok mode tec rec error_state nbtp dbtp tdc rx_status tx_status'.split()]
class External(C.LittleEndianStructure):
    _fields_=[('header',U*7)]+[(n,U) for n in 'rx_irq tx_irq int_irq rx_frames tx_frames rx_overflow tx_full rx_errors tx_errors rx_drain_limit tx_drain_limit init_errors module_events isr_int0 isr_int1 rx_irq_no_rxif tx_irq_no_txif int_master_wake last_int_flags last_rx_status last_tx_status rx_hw_overflow rx_status_errors rx_read_errors'.split()]
class SPI(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'accepted completed queue_full queue_expired errors timeouts recoveries max_queue_us max_transfer_us'.split()]+[(n,U*6) for n in ('queue_us','transfer_us')]
class Health(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'ins_cycles ins_missing ins_missed ins_publications ins_phase ins_flags ins_max_execution_us ins_max_period_us'.split()]+[(n,U*4) for n in 'samples errors overruns not_ready recoveries'.split()]+[('period_us',(U*6)*4)]
class Stage(C.LittleEndianStructure):
    _fields_=[('requested_fps',U*2),('length',U),('seconds',U),('producer_ticks',U),('elapsed_us',C.c_uint64)]+[(n,U*2) for n in 'accepted rejected received_active received gaps reordered bad'.split()]+[(n,U) for n in ('cpu_ppm','monitor_ppm','critical_us')]+[(n,base.ClassicStats*2) for n in ('classic_before','classic_after')]+[(n,base.FDStats*2) for n in ('fd_before','fd_after')]+[(n,base.Hardware) for n in ('hardware_before','hardware_after')]+[(n,MCP) for n in ('mcp_before','mcp_after')]+[(n,External) for n in ('external_before','external_after')]+[(n,SPI*4) for n in ('spi_before','spi_after')]+[(n,U*2) for n in ('observed_samples','max_period_us','max_acquisition_us','period_over_1200')]+[(n,Health) for n in ('before','after')]
class Result(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'phase completed failure malformed warmup_ms fd data_rate brs'.split()]+[(n,C.c_int32) for n in ('subscribe_result','create_result','delete_result')]+[(n,U) for n in ('functional_expected','functional_received','functional_bad')]+[('startup',base.Startup),('config',base.Config*2),('stages',Stage*11),('stack_count',U),('stacks',base.Stack*48),('resources',base.Resources)]
assert C.sizeof(Result)==26816 and C.sizeof(Stage)==2320 and C.sizeof(External)==124 and C.sizeof(SPI)==84
assert Result.stages.offset==104 and Result.stacks.offset==0x641c and Result.resources.offset==0x689c

def delta(a,b):
    if isinstance(a,list):return [delta(x,y) for x,y in zip(a,b)]
    if isinstance(a,dict):return {k:delta(a[k],b[k]) for k in a}
    return a-b

def summarize(r):
    out={k:r[k] for k in ('phase','completed','failure','malformed','warmup_ms','fd','data_rate','functional_expected','functional_received','functional_bad','startup','resources')}
    out['stages']=[];out['stacks']=r['stacks'][:r['stack_count']]
    if not r['completed']:
        out['functional_diagnostics']={k:r['stages'][0][k] for k in ('hardware_after','mcp_after','external_after','spi_after','after')}
    for i,s in enumerate(r['stages'][:r['completed']]):
        prefix='fd' if r['fd'] else 'classic';key='tx_expired' if r['fd'] else 'tx_oneshot_expired'
        expired=[s[prefix+'_after'][c][key]-s[prefix+'_before'][c][key] for c in (0,1)]
        rate=[round(v*1e6/s['elapsed_us'],3) for v in s['received_active']]
        h=delta(s['after'],s['before']);e=delta(s['external_after'],s['external_before'])
        # Header, sticky event bitsets and last-value gauges are not counters.
        for k in ('header','module_events','last_int_flags','last_rx_status','last_tx_status'):e.pop(k)
        q=[]
        for name,a,b in zip(('BMI_accel','BMI_gyro','LSM','MCP'),s['spi_after'],s['spi_before']):
            d=delta(a,b)
            q.append(dict(name=name,delta=d,transactions_per_second=round(d['completed']/s['seconds'],2),max_queue_since_boot_us=a['max_queue_us'],max_transfer_since_boot_us=a['max_transfer_us']))
        received=s['received'];accepted=s['accepted']
        out['stages'].append(dict(index=i,requested_fps=s['requested_fps'],length=s['length'],seconds=s['seconds'],producer_ticks=s['producer_ticks'],active_rx_fps=rate,accepted=accepted,rejected=s['rejected'],received=received,expired=expired,
            unexplained_loss=[accepted[c]-received[c]-expired[c] for c in (0,1)],gaps=s['gaps'],reordered=s['reordered'],bad=s['bad'],cpu_percent=s['cpu_ppm']/10000,
            native_rx_lost=s['hardware_after']['rx_hw_lost']-s['hardware_before']['rx_hw_lost'],native_ring_lost=s['hardware_after']['rx_overrun']-s['hardware_before']['rx_overrun'],native_tec=s['hardware_after']['tec'],native_rec=s['hardware_after']['rec'],native_busoff=s['hardware_after']['bus_off'],mcp=s['mcp_after'],external_delta=e,spi=q,
            ins_publications=h['ins_publications'],ins_missing=h['ins_missing'],ins_missed=h['ins_missed'],ins_flags=[s['before']['ins_flags'],s['after']['ins_flags']],sensor_samples=h['samples'],sensor_errors=h['errors'],sensor_overruns=h['overruns'],sensor_not_ready=h['not_ready'],sensor_recoveries=h['recoveries'],sensor_period_histogram=h['period_us'],
            observed_samples=s['observed_samples'],max_imu_period_us=s['max_period_us'],max_imu_acquisition_us=s['max_acquisition_us'],imu_period_over_1200=s['period_over_1200'],
            achieved=accepted==received and not any(s['rejected']+s['gaps']+s['bad']+s['reordered']) and abs(s['producer_ticks']-s['seconds']*1000)<=1 and all(abs(rate[c]-s['requested_fps'][c])<=max(1,s['requested_fps'][c]*.001) for c in (0,1))))
    return out
if __name__=='__main__':
    for name in sys.argv[1:]:
        p=Path(name);raw=p.read_bytes();assert len(raw)==C.sizeof(Result)
        r=base.convert(Result.from_buffer_copy(raw));assert r['phase']==3
        p.with_suffix('.json').write_text(json.dumps(r,indent=2)+'\n')
        s=summarize(r);p.with_name(p.stem+'-summary.json').write_text(json.dumps(s,indent=2)+'\n')
        print(p.stem,'failure',s['failure'],'completed',s['completed'],'functional',s['functional_received'],'/',s['functional_expected'],'bad',s['functional_bad'])
        for x in s['stages']:
            print(x['index'],x['length'],x['requested_fps'],'actual',x['active_rx_fps'],'CPU',x['cpu_percent'],'reject',x['rejected'],'expiry',x['expired'],'loss',x['unexplained_loss'],'IMU',x['sensor_samples'][:2],'INS',x['ins_missing'],x['ins_missed'],'period',x['max_imu_period_us'],'MCP/SPI fps',x['spi'][3]['transactions_per_second'])
