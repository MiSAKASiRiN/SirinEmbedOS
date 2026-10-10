#!/usr/bin/env python3
"""Compare ID1 encoder displacement with bias-corrected raw BMI/LSM gyros."""
import argparse
import collections
from array import array
import json
from pathlib import Path
import struct
import sys
import numpy as np
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/HostLink'))
from imu_telemetry import Decoder,decode_samples
p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);a=p.parse_args()
catalog=json.loads((a.capture/'catalog.json').read_text());sources=collections.defaultdict(lambda:array('d'));imus=collections.defaultdict(lambda:array('d'))
formats={1:'<f',2:'<i',3:'<I',4:'<?'};decoder=Decoder()
with (a.capture/'rx.bin').open('rb') as f:
    while data:=f.read(65536):
        for kind,service,msg,seq,payload in decoder.feed(data):
            if (kind,service,msg)==(2,4,0x12):
                offset=16
                for _ in range(payload[12]):
                    index,typ,valid,seq,stamp=struct.unpack_from('<HBBIQ',payload,offset);offset+=16
                    value,=struct.unpack_from(formats[typ],payload,offset);offset+=struct.calcsize(formats[typ])
                    sources[catalog[str(index)]['name']].extend((stamp*1e-6,float(value),valid,seq))
            elif (kind,service,msg)==(2,4,2):
                stream, samples=decode_samples(payload)
                if stream in (0x100,0x101):
                    for s in samples:imus[stream].extend((s['end_us']*1e-6,*s['gyro_rad_s'],*s['accel_m_s2'],s['sequence']))
sources={name:np.frombuffer(rows,dtype=np.float64).reshape(-1,4) for name,rows in sources.items()}
imus={name:np.frombuffer(rows,dtype=np.float64).reshape(-1,8) for name,rows in imus.items()}
report={'decoder_errors':dict(decoder.errors),'sources':{name:dict(count=len(v),min=float(v[:,1].min()),max=float(v[:,1].max()),last=float(v[-1,1]),invalid=int((v[:,2]==0).sum())) for name,v in sources.items()}}
report['imu_quality']={str(stream):dict(samples=len(v),sequence_discontinuities=int(np.count_nonzero(np.diff(v[:,-1])!=1)),max_sample_interval_ms=float(np.diff(v[:,0]).max()*1000)) for stream,v in imus.items()}
phase=sources['turntable.phase'];ranges={}
for state in np.unique(phase[:,1]).astype(int):
    rows=phase[phase[:,1]==state];ranges[str(state)]=[float(rows[0,0]),float(rows[-1,0])]
report['phase_ranges_us_clock_seconds']=ranges
for i in (1,2,3):
    key=f'turntable.motor{i}.feedback_rpm'
    if key in sources:
        values,counts=np.unique(sources[key][:,1],return_counts=True)
        report['sources'][key]['histogram']={str(int(v)):int(n) for v,n in zip(values,counts)}
revision=int(sources.get('turntable.sequence_revision',np.array([[0,1]]))[-1,1])
# A partial ID3 lift is useful even when protection prevents the ID1 sweep.
# Require a stationary boot window; stop before the next axis or a fault.
if '1' in ranges and '2' in ranges:
    start=ranges['1'][0]
    end=min([ranges[k][0] for k in ('3','10') if k in ranges] or [ranges['2'][1]])-.1
    baseline_start,baseline_end=(ranges.get('0',[start,start])[0]+.05,start-.05) if revision>=3 else (start+.2,start+3)
    stationary=baseline_end-baseline_start>=.5
    for i in (1,2,3):
        raw=sources[f'turntable.motor{i}.encoder_raw']
        rows=raw[(raw[:,0]>baseline_start)&(raw[:,0]<baseline_end)]
        stationary &= len(rows)>100 and np.ptp(rows[:,1])<=2
    if stationary and end>start+4:
        raw=sources['turntable.motor3.encoder_raw']
        encoder_delta=(np.interp(end,raw[:,0],raw[:,1])-np.interp(start,raw[:,0],raw[:,1]))*360/8192
        lift={'start_s':start,'end_s':end,'encoder_delta_deg':float(encoder_delta),
              'baseline_seconds':[baseline_start,baseline_end],
              'scope':'ID3 monotonic lift; gyro integral vector magnitude; not ID1 sweep or calibration'}
        for stream,imu in imus.items():
            baseline=imu[(imu[:,0]>baseline_start)&(imu[:,0]<baseline_end)]
            rows=imu[(imu[:,0]>=start)&(imu[:,0]<=end)]
            bias=baseline[:,1:4].mean(axis=0)
            vector=np.trapezoid(rows[:,1:4]-bias,rows[:,0],axis=0)*180/np.pi
            magnitude=float(np.linalg.norm(vector))
            lift[{256:'BMI088',257:'LSM6DS3'}[stream]]={
                'integral_vector_deg':vector.tolist(),'magnitude_deg':magnitude,
                'difference_deg':magnitude-float(encoder_delta),'bias_rad_s':bias.tolist()}
        report['id3_lift']=lift
# If ID1 homing trips protection, retain a clearly labelled partial comparison.
if '3' in ranges and '10' in ranges and '5' not in ranges:
    start=ranges['3'][0];end=ranges['10'][0]-.3
    stationary=True
    for i in (1,2,3):
        raw=sources[f'turntable.motor{i}.encoder_raw']
        rows=raw[(raw[:,0]>start-.9)&(raw[:,0]<start-.05)]
        stationary &= len(rows)>100 and np.ptp(rows[:,1])<=2
    if stationary and end>start+2:
        enc=sources['turntable.motor1.unwrapped_deg']
        delta=float(np.interp(end,enc[:,0],enc[:,1])-np.interp(start,enc[:,0],enc[:,1]))
        partial={'scope':'Incomplete ID1 homing, not the requested post-home sweep; excludes final 300 ms before fault',
                 'start_s':start,'end_s':end,'encoder_delta_deg':delta,'baseline_duration_s':.85}
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        fig,axs=plt.subplots(2,1,figsize=(10,7),sharex=True,constrained_layout=True)
        for stream,imu in imus.items():
            name={256:'BMI088',257:'LSM6DS3'}[stream]
            baseline=imu[(imu[:,0]>start-.9)&(imu[:,0]<start-.05)]
            bias=baseline[:,1:4].mean(axis=0)
            rows=imu[(imu[:,0]>=start)&(imu[:,0]<=end)];t=rows[:,0]
            gyro=rows[:,1:4]-bias
            integ=np.vstack((np.zeros(3),np.cumsum((gyro[1:]+gyro[:-1])*.5*np.diff(t)[:,None],axis=0)))*180/np.pi
            axis=integ[-1]/np.linalg.norm(integ[-1])*np.sign(delta)
            angle=integ@axis
            encoder=np.interp(t,enc[:,0],enc[:,1])-np.interp(t[0],enc[:,0],enc[:,1])
            partial[name]={'gyro_delta_deg':float(angle[-1]),'encoder_delta_deg':float(encoder[-1]),
                          'difference_deg':float(angle[-1]-encoder[-1]),'estimated_axis':axis.tolist(),
                          'bias_rad_s':bias.tolist(),'samples':len(t),'max_sample_interval_ms':float(np.diff(t).max()*1000)}
            axs[0].plot(t-start,angle,label=name,linewidth=1.1)
            axs[1].plot(t-start,angle-encoder,label=name,linewidth=1.1)
        axs[0].plot(t-start,encoder,'k--',label='ID1 encoder',linewidth=1)
        axs[0].set(title='Partial ID1 homing before protection (NOT completed sweep)',ylabel='Displacement (deg)')
        axs[1].set(xlabel='Time from ID1 homing start (s)',ylabel='Gyro projection minus encoder (deg)')
        for ax in axs:ax.grid(alpha=.3);ax.legend()
        fig.savefig(a.capture/'partial-homing-comparison.png',dpi=160)
        report['id1_partial_homing']=partial
revision=int(sources.get('turntable.sequence_revision',np.array([[0,1]]))[-1,1])
if revision>=2:
    from sequence_analysis import analyze
    report['sequence']=analyze(sources,imus,ranges,a.capture)
    report['sequence']['revision']=revision
if revision<2 and all(str(i) in ranges for i in (5,6,7,8,9)):
    enc=sources['turntable.motor1.unwrapped_deg'];reference={}
    segments={'forward':(ranges['6'][0],ranges['7'][0]),'return':(ranges['8'][0],ranges['9'][0])}
    results={};curves={}
    for stream,imu in imus.items():
        name={256:'BMI088',257:'LSM6DS3'}[stream]
        baseline=imu[(imu[:,0]>ranges['5'][0]+1)&(imu[:,0]<ranges['5'][1]-.1)]
        assert len(baseline)>1000
        bias=baseline[:,1:4].mean(axis=0)
        results[name]={'baseline_samples':len(baseline),'bias_rad_s':bias.tolist()}
        axis=None
        for label,(start,end) in segments.items():
            # Integrate within shared MCU observation time, including settling.
            mask=(imu[:,0]>=start)&(imu[:,0]<=end);segment=imu[mask];t=segment[:,0]
            gyro=segment[:,1:4]-bias
            integ=np.vstack((np.zeros(3),np.cumsum((gyro[1:]+gyro[:-1])*.5*np.diff(t)[:,None],axis=0)))
            delta=np.interp(t[-1],enc[:,0],enc[:,1])-np.interp(t[0],enc[:,0],enc[:,1])
            if axis is None:axis=integ[-1]/np.linalg.norm(integ[-1])*np.sign(delta)
            imu_deg=integ@axis*180/np.pi
            results[name][label]={'encoder_delta_deg':float(delta),'gyro_delta_deg':float(imu_deg[-1]),
                'difference_deg':float(imu_deg[-1]-delta),'relative_error_percent':float((imu_deg[-1]-delta)/delta*100),
                'sample_count':len(t),'duration_s':float(t[-1]-t[0])}
            curves[(name,label)]=(t-t[0],imu_deg,np.interp(t,enc[:,0],enc[:,1])-np.interp(t[0],enc[:,0],enc[:,1]))
        results[name]['estimated_axis_in_sensor_frame']=axis.tolist()
    report['comparison']=results
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig,axs=plt.subplots(2,1,figsize=(10,7),constrained_layout=True)
    for ax,label in zip(axs,('forward','return')):
        for name in ('BMI088','LSM6DS3'):
            t,angle,encoder=curves[(name,label)];ax.plot(t,angle,label=name,linewidth=1.2)
        ax.plot(t,encoder,'k--',label='ID1 encoder',linewidth=1)
        ax.set(title=f'ID1 {label}: encoder vs bias-corrected gyro projection',xlabel='Time (s)',ylabel='Displacement (deg)');ax.grid(alpha=.3);ax.legend()
    fig.savefig(a.capture/'comparison.png',dpi=160)
(a.capture/'analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report.get('comparison',report),indent=2))
