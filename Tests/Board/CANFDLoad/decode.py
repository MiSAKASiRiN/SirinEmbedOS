#!/usr/bin/env python3
"""Decode the fixed ARM result ABI dumped after phase 3; no board access."""
import ctypes as C
import json
from pathlib import Path
import sys
# The production toolchain uses -fshort-enums; small nonnegative enums occupy
# one byte. Validate layouts with the exact ARM compile command, not host ABI.
U=C.c_uint32
I=C.c_int32
class Hardware(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'rx_isr rx_overrun rx_hw_lost tx_enqueued tx_busy bus_off tec rec error_logging tx_pending rx_fifo_lost kernel_hz nominal_bps data_bps frame_format nbtp dbtp tdcr cccr psr'.split()]
class ClassicStats(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'rx_pops rx_routed_raw rx_routed_adapter rx_no_subscriber rx_dropped rx_wake_by_isr rx_wake_by_timeout tx_oneshot_enqueue_ok tx_oneshot_enqueue_full tx_oneshot_expired tx_urgent_accepted tx_periodic_accepted tx_periodic_dropped tx_backend_busy tx_complete_events rx_route_count rx_route_high_water rx_route_lookup_steps rx_route_capacity_rejects tx_adapter_count tx_adapter_high_water tx_oneshot_depth tx_oneshot_high_water'.split()]
class FDStats(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'rx_received rx_delivered rx_no_subscriber rx_rejected rx_wake_by_isr rx_wake_by_timeout tx_enqueued tx_queue_full tx_expired tx_accepted tx_backend_busy tx_complete_events tx_depth tx_high_water route_count'.split()]
class Health(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'ins_cycles ins_missing ins_missed ins_publications ins_phase ins_flags'.split()]+[(n,U*4) for n in ('samples','errors','overruns')]
class Stage(C.LittleEndianStructure):
    _fields_=[('requested_fps',U*2),('length',U),('seconds',U),('producer_ticks',U),('elapsed_us',C.c_uint64)]+[(n,U*2) for n in 'accepted rejected received_active received gaps reordered bad'.split()]+[(n,U) for n in ('cpu_ppm','monitor_ppm','critical_us')]+[(n,ClassicStats*2) for n in ('classic_before','classic_after')]+[(n,FDStats*2) for n in ('fd_before','fd_after')]+[(n,Hardware*2) for n in ('hardware_before','hardware_after')]+[(n,Health) for n in ('before','after')]
class Startup(C.LittleEndianStructure):
    _fields_=[('phase',C.c_uint8),('native_can_ready',C.c_bool),('usb_ready',C.c_bool),('parameter_load_status',I),('identity_load_status',C.c_uint8)]
class Config(C.LittleEndianStructure):
    _fields_=[('mode',C.c_uint8),('nominal_bitrate',U),('data_bitrate',U),('brs',C.c_bool)]
class Stack(C.LittleEndianStructure):
    _fields_=[('name',C.c_char*16),('free_bytes',U),('priority',U)]
class Resources(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'capacity_bytes used_bytes peak_bytes allocation_failures app_threads thread_limit creation_failures kernel_free_bytes kernel_minimum_free_bytes'.split()]
class Result(C.LittleEndianStructure):
    _fields_=[(n,U) for n in 'phase completed failure malformed warmup_ms fd data_rate brs'.split()]+[(n,I) for n in ('subscribe_result','create_result','delete_result')]+[(n,U) for n in ('functional_expected','functional_received','functional_bad')]+[('startup',Startup),('config',Config*2),('stages',Stage*9),('stack_count',U),('stacks',Stack*48),('resources',Resources)]
def convert(value):
    if isinstance(value,C.Array):return [convert(v) for v in value]
    if isinstance(value,C.Structure):return {name:convert(getattr(value,name)) for name,_ in value._fields_}
    if isinstance(value,bytes):return value.decode('ascii')
    return value

def summary(r):
    out={'fd':r['fd'],'data_rate':r['data_rate'],'brs':r['brs'],'failure':r['failure'],'warmup_ms':r['warmup_ms'],
         'functional':{'expected':r['functional_expected'],'received':r['functional_received'],'bad':r['functional_bad']},'stages':[]}
    for s in r['stages'][:r['completed']]:
        prefix='fd' if r['fd'] else 'classic'
        expired='tx_expired' if r['fd'] else 'tx_oneshot_expired'
        delta=lambda name:[s['after'][name][i]-s['before'][name][i] for i in range(4)]
        hw_delta=lambda name:[s['hardware_after'][i][name]-s['hardware_before'][i][name] for i in range(2)]
        out['stages'].append({'requested_fps':s['requested_fps'],'length':s['length'],'seconds':s['seconds'],
            'elapsed_us':s['elapsed_us'],'producer_ticks':s['producer_ticks'],
            'active_rx_fps':[round(n*1e6/s['elapsed_us'],3) for n in s['received_active']],
            'accepted':s['accepted'],'rejected':s['rejected'],'received':s['received'],
            'undelivered':[s['accepted'][i]-s['received'][i] for i in range(2)],
            'gaps':s['gaps'],'reordered':s['reordered'],'bad':s['bad'],'cpu_percent':s['cpu_ppm']/10000,
            'expired':[s[prefix+'_after'][i][expired]-s[prefix+'_before'][i][expired] for i in range(2)],
            'rx_ring_drops':hw_delta('rx_overrun'),'rx_hw_lost':hw_delta('rx_hw_lost'),
            'bus_off':hw_delta('bus_off'),'tec':[h['tec'] for h in s['hardware_after']],
            'rec':[h['rec'] for h in s['hardware_after']],
            'error_logging':[h['error_logging'] for h in s['hardware_after']],
            'ins_publications':s['after']['ins_publications']-s['before']['ins_publications'],
            'ins_missing':s['after']['ins_missing']-s['before']['ins_missing'],
            'ins_missed':s['after']['ins_missed']-s['before']['ins_missed'],
            'ins_flags':[s['before']['ins_flags'],s['after']['ins_flags']],
            'sensor_samples':delta('samples'),'sensor_errors':delta('errors'),'sensor_overruns':delta('overruns')})
    out['stacks']=r['stacks'][:r['stack_count']]
    return out
if __name__=='__main__':
    if len(sys.argv)==1:
        print({t.__name__:C.sizeof(t) for t in (Result,Stage,Hardware,ClassicStats,FDStats,Health,Startup,Config,Stack,Resources)})
        print({n:getattr(Result,n).offset for n,_ in Result._fields_});sys.exit()
    p=Path(sys.argv[1]);raw=p.read_bytes()
    assert len(raw)==C.sizeof(Result),(len(raw),C.sizeof(Result))
    r=convert(Result.from_buffer_copy(raw));assert r['phase']==3,r['phase']
    assert r['completed']<=9 and r['stack_count']<=48
    p.with_suffix('.json').write_text(json.dumps(r,indent=2)+'\n')
    s=summary(r);p.with_name(p.stem+'-summary.json').write_text(json.dumps(s,indent=2)+'\n')
    print(json.dumps(s,indent=2))
