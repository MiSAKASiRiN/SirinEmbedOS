#!/usr/bin/env python3
"""Capture turntable SOURCE V2 and both raw IMUs; optional latched zero command."""
import argparse
import collections
import json
from pathlib import Path
import struct
import sys
import time
import signal
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/HostLink'))
from imu_telemetry import Client, Decoder, open_port
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',required=True)
p.add_argument('--output',type=Path)
p.add_argument('--seconds',type=float,default=1200)
p.add_argument('--stop',action='store_true',help='Request firmware shutdown sequence and wait for zero-output completion')
p.add_argument('--keep-holding',action='store_true',help='Explicitly keep closed-loop support after a successful capture')
a=p.parse_args()
if a.stop and a.keep_holding:p.error('--stop conflicts with --keep-holding')
if a.output is None:
    if not a.stop:p.error('--output is required for capture')
    a.output=ROOT/'build'/('turntable-stop-'+time.strftime('%Y%m%d-%H%M%S'))
signal.signal(signal.SIGTERM,lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
a.output.mkdir(parents=True,exist_ok=False)
sources={}; latest={}; decoder=Decoder(); counts=collections.Counter();invalid=collections.Counter()
formats={1:'<f',2:'<i',3:'<I',4:'<?'}
with (a.output/'rx.bin').open('wb') as raw,open_port(a.port) as port:
    def received(data,host_ns):
        raw.write(data)
        for kind,service,message,seq,payload in decoder.feed(data):
            if (kind,service,message)!=(2,4,0x12):continue
            offset=16
            for _ in range(payload[12]):
                index,typ,valid,seq,stamp=struct.unpack_from('<HBBIQ',payload,offset);offset+=16
                value,=struct.unpack_from(formats[typ],payload,offset);offset+=struct.calcsize(formats[typ])
                name=sources[index]['name'];latest[name]=value
                counts[name]+=1;invalid[name]+=not valid
            assert offset==len(payload)
    client=Client(port,on_receive=received)
    cursor=0;catalog_hash=None
    while cursor!=65535:
        body=client.request(4,0x10,struct.pack('<HB',cursor,8))
        h,total,cursor,count=struct.unpack_from('<IHHB',body);catalog_hash=h;offset=9
        for _ in range(count):
            i,t,size,maximum,hz,version,n,u=struct.unpack_from('<HBBHHBBB',body,offset);offset+=11
            name=body[offset:offset+n].decode();offset+=n
            unit=body[offset:offset+u].decode();offset+=u
            sources[i]=dict(name=name,type=t,max_hz=maximum,unit=unit)
        assert offset==len(body)
    assert total==32 and len(sources)==32
    (a.output/'catalog.json').write_text(json.dumps(sources,indent=2)+'\n')
    request=struct.pack('<IB',catalog_hash,len(sources))+b''.join(struct.pack('<HH',i,min(200,s['max_hz'])) for i,s in sources.items())
    subscribed=False;imu_subscribed=False;status='timeout';phase_since=None;last_phase=None
    started=time.monotonic();next_print=started;parking=False;park_started=None;failure=None
    try:
        client.request(4,0x11,request);subscribed=True
        client.subscribe(streams=(0x100,0x101));imu_subscribed=True
        def park():
            global parking,park_started
            if not parking:
                # Older firmware's message 1 was an immediate zero, never use it here.
                if latest.get('turntable.sequence_revision',0)<17:
                    raise RuntimeError('Controlled landing requires firmware revision >=17')
                client.request(0x40,1);parking=True;park_started=time.monotonic()
        while True:
            try:client.receive()
            except KeyboardInterrupt:
                failure='interrupted';park()
            now=time.monotonic()
            if a.stop and latest.get('turntable.sequence_revision') is not None:park()
            phase=latest.get('turntable.phase')
            if phase!=last_phase:phase_since=now;last_phase=phase
            if now>=next_print:
                print(json.dumps(dict(seconds=round(now-started,2),phase=phase,
                    revision=latest.get('turntable.sequence_revision'),fault=latest.get('turntable.fault'),fault_axis=latest.get('turntable.fault_axis'),fault_measurement=latest.get('turntable.fault_measurement'),raw=[latest.get(f'turntable.motor{i}.encoder_raw') for i in (1,2,3)],
                    speed=[latest.get(f'turntable.motor{i}.speed_dps') for i in (1,2,3)],
                    rpm=[latest.get(f'turntable.motor{i}.feedback_rpm') for i in (1,2,3)],
                    current=[latest.get(f'turntable.motor{i}.current_raw') for i in (1,2,3)],
                    temperature=[latest.get(f'turntable.motor{i}.temperature_c') for i in (1,2,3)],
                    voltage=[latest.get(f'turntable.motor{i}.voltage_raw') for i in (1,2,3)],
                    imu_counts=dict(client.counts))),flush=True)
                next_print=now+1
            if latest.get('turntable.sequence_revision',0)>=21 and phase==0 and now-started>=a.seconds:
                status='telemetry_only';break
            if phase==10 and now-phase_since>2:status='fault';break
            if phase==34 and now-phase_since>3:
                status='timed_release_zero' if latest.get('turntable.sequence_revision',0)>=20 else 'landed_zero'
                break
            if phase==35 and now-phase_since>3:status='landing_incomplete_holding';break
            if phase==22 and now-phase_since>3:
                failure='level_unavailable';park()
            if phase==9 and now-phase_since>6:
                if a.keep_holding:status='hold';break
                park()
            if not parking and now-started>=a.seconds:
                failure='timeout';park()
            if parking and now-park_started>180:
                status='landing_unconfirmed';break
    except KeyboardInterrupt:
        if latest.get('turntable.sequence_revision',0)>=17:client.request(0x40,1)
        status='interrupted_landing_unconfirmed'
    except BaseException:
        # STOP is the only motion-affecting host request, used on capture abort.
        try:
            if latest.get('turntable.sequence_revision',0)>=17:client.request(0x40,1)
        except Exception:pass
        raise
    finally:
        if imu_subscribed:client.subscribe(False,streams=(0x100,0x101))
        if subscribed:client.request(4,0x14,struct.pack('<I',catalog_hash))
    summary=dict(status=status,termination_reason=failure,elapsed_seconds=time.monotonic()-started,latest=latest,
        scalar_counts=dict(counts),invalid=dict(invalid),imu_counts=dict(client.counts),
        imu_gaps=dict(client.gaps),imu_duplicates=dict(client.duplicates),decoder_errors=dict(decoder.errors))
    (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2),flush=True)
    if status not in ('hold','landed_zero','timed_release_zero','telemetry_only') or failure:sys.exit(1)
