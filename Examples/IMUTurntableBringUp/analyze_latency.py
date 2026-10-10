#!/usr/bin/env python3
"""Estimate apparent MCU-observation delays; not CAN wire latency or transfer-function identification."""
import argparse,collections,json,struct,sys
from array import array
from pathlib import Path
import numpy as np
from scipy.signal import savgol_filter
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Tools/HostLink'))
from imu_telemetry import Decoder,decode_samples
p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('--smoothing-ms',type=int,choices=(21,31,51),default=31);a=p.parse_args();d=a.capture
window=a.smoothing_ms;suffix='' if window==31 else f'-smooth{window}'
catalog=json.loads((d/'catalog.json').read_text());dec=Decoder();src=collections.defaultdict(lambda:array('d'));imu=collections.defaultdict(lambda:array('d'))
with (d/'rx.bin').open('rb') as f:
 while data:=f.read(65536):
  for kind,s,m,seq,b in dec.feed(data):
   if (kind,s,m)==(2,4,0x12):
    at=16
    for _ in range(b[12]):
     i,typ,valid,sq,stamp=struct.unpack_from('<HBBIQ',b,at);at+=16;fmt={1:'<f',2:'<i',3:'<I',4:'<?'}[typ];v=struct.unpack_from(fmt,b,at)[0];at+=struct.calcsize(fmt)
     src[catalog[str(i)]['name']].extend((stamp/1e6,v,valid))
   elif (kind,s,m)==(2,4,2) and struct.unpack_from('<H',b)[0] in (256,257,258):
    stream,ss=decode_samples(b)
    for sample in ss:
     if stream in (256,257):imu[stream].extend((sample['end_us']/1e6,*sample['gyro_rad_s'],sample['sequence']))
src={k:np.frombuffer(v,float).reshape(-1,3) for k,v in src.items()};src={k:v[v[:,2]!=0] for k,v in src.items()};imu={k:np.frombuffer(v,float).reshape(-1,5) for k,v in imu.items()}
t0=max(v[0,0] for v in imu.values());t1=min(v[-1,0] for v in imu.values());t=np.arange(t0+.1,t1-.1,.001)
def interp(name):
 s=src[name];return np.interp(t,s[:,0],s[:,1])
positions=np.column_stack([interp(f'turntable.motor{i}.unwrapped_deg') for i in (1,2,3)])
# Symmetric derivatives/smoothing do not intentionally add a causal filter delay.
v=savgol_filter(positions,window,3,deriv=1,delta=.001,axis=0)
u=np.column_stack([interp(f'turntable.motor{i}.voltage_raw') for i in (1,2,3)])
current=np.column_stack([interp(f'turntable.motor{i}.current_raw') for i in (1,2,3)])
rpm=np.column_stack([interp(f'turntable.motor{i}.feedback_rpm')*6 for i in (1,2,3)])
g={k:savgol_filter(np.column_stack([np.interp(t,x[:,0],x[:,j]) for j in (1,2,3)])*180/np.pi,window,3,axis=0) for k,x in imu.items()}
# Isolate single-axis moves, excluding coupled static transitions and random motion.
legs=[]
for ax in range(3):
 mask=(abs(v[:,ax])>5)&(np.max(abs(np.delete(v,ax,axis=1)),axis=1)<2)
 idx=np.flatnonzero(mask)
 if not len(idx):continue
 groups=np.split(idx,np.flatnonzero(np.diff(idx)>300)+1)
 for group in groups:
  if len(group)<400 or abs(positions[group[-1],ax]-positions[group[0],ax])<15:continue
  lo=max(0,group[0]-300);hi=min(len(t),group[-1]+500)
  if np.max(abs(np.delete(v[lo:hi],ax,axis=1)))>8:continue
  legs.append(dict(axis=ax+1,start=float(t[lo]),end=float(t[hi-1]),lo=int(lo),hi=int(hi),delta_deg=float(positions[group[-1],ax]-positions[group[0],ax])))
# Positive lag: y appears later than x. Fit a vector gain+offset at each trial lag.
def lag_fit(x,y,lo,hi,limit=.08):
 ts=t[lo:hi:2];ys=y[lo:hi:2];ys=ys[:,None] if ys.ndim==1 else ys
 yc=ys-ys.mean(axis=0);base=float(np.sum(yc*yc));scores=[];lags=np.arange(-limit,limit+.0001,.0005)
 for lag in lags:
  xs=np.interp(ts-lag,t,x);xc=xs-xs.mean();gain=(xc@yc)/max(float(xc@xc),1e-9);scores.append(float(np.sum((yc-xc[:,None]*gain)**2)))
 best=int(np.argmin(scores));xs=np.interp(ts-lags[best],t,x);xc=xs-xs.mean();gain=(xc@yc)/max(float(xc@xc),1e-9)
 return dict(lag_ms=float(lags[best]*1000),r2=1-scores[best]/max(base,1e-9),gain=gain.tolist(),at_boundary=best in (0,len(lags)-1),rmse=float(np.sqrt(scores[best]/len(ts))))
for leg in legs:
 ax=leg['axis']-1;lo,hi=leg['lo'],leg['hi'];leg['imu']={str(k):lag_fit(v[:,ax],val,lo,hi) for k,val in g.items()}
 leg['rpm_vs_encoder']=lag_fit(savgol_filter(v[:,ax],51,3),savgol_filter(rpm[:,ax],51,3),lo,hi)
 # High-pass removes slow back-EMF/gravity drift. Still closed-loop correlation,
 # so negative/weak/leg-dependent values must not be interpreted as causal delay.
 ux=savgol_filter(u[:,ax],11,3)-savgol_filter(u[:,ax],301,3)
 iy=savgol_filter(current[:,ax],11,3)-savgol_filter(current[:,ax],301,3)
 leg['current_vs_command']=lag_fit(ux,iy,lo,hi,.04)
 leg.pop('lo');leg.pop('hi')

def quant(values):
 if not len(values):return None
 return dict(n=len(values),median=float(np.median(values)),p10=float(np.percentile(values,10)),p90=float(np.percentile(values,90)),minimum=float(min(values)),maximum=float(max(values)))
summary={}
for ax in (1,2,3):
 chosen=[l for l in legs if l['axis']==ax];s={}
 for k in (256,257):s[f'{k}_vs_encoder_ms']=quant([l['imu'][str(k)]['lag_ms'] for l in chosen if l['imu'][str(k)]['r2']>.97 and not l['imu'][str(k)]['at_boundary']])
 qualified=[l['current_vs_command']['lag_ms'] for l in chosen if l['current_vs_command']['r2']>.5 and not l['current_vs_command']['at_boundary']]
 s['current_vs_command_ms']=quant(qualified) if len(qualified)>=3 else None
 s['current_vs_command_qualified_legs']=len(qualified)
 s['current_vs_command_note']='Closed-loop correlation only; fewer than three qualifying legs is insufficient for a repeatable delay estimate.'
 s['rpm_vs_encoder_ms']=quant([l['rpm_vs_encoder']['lag_ms'] for l in chosen if l['rpm_vs_encoder']['r2']>.85 and not l['rpm_vs_encoder']['at_boundary']])
 summary[str(ax)]=s
mon=[json.loads(s) for s in (d/'monitor.jsonl').read_text().splitlines()];cpu=[];task=collections.defaultdict(list);cycles=collections.defaultdict(list);seen_cycles=set()
for report in mon:
 for r in report['records']:
  if report['stream']=='adc' and r['flags']&2:cpu.append((r['updated_us']/1e6,r['busy_ppm']/10000))
  elif report['stream']=='tasks' and r['flags']&2:task[r['name']].append(r['cpu_ppm']/10000)
  elif report['stream']=='cycles' and r['flags']&4:
   key=(r['slot'],r['generation'],r['sampled_us'])
   if key not in seen_cycles:cycles[r['name']].append(r);seen_cycles.add(key)
cpu=np.array(cpu);cpu=cpu[cpu[:,0]>=t[0]+3];moving=np.max(abs(v),axis=1)>3
cpureport={'all_percent':quant(cpu[:,1].tolist()),'moving_percent':quant([c for tm,c in cpu if np.interp(tm,t,moving.astype(float))>.5]),'stationary_percent':quant([c for tm,c in cpu if np.interp(tm,t,moving.astype(float))<.5]),'tasks_percent':{k:quant(v) for k,v in task.items()},'cycles':{k:dict(windows=len(rs),sampled_window_seconds=sum(r['window_us'] for r in rs)/1e6,completed=sum(r['completed'] for r in rs),execution_mean_us_median=float(np.median([r['execution_mean_us'] for r in rs])),execution_max_us=max(r['execution_max_us'] for r in rs),late_max_us=max(r['start_late_max_us'] for r in rs),jitter_abs_mean_us_median=float(np.median([r['jitter_abs_mean_us'] for r in rs])),jitter_range_us=[min(r['jitter_min_us'] for r in rs),max(r['jitter_max_us'] for r in rs)],misses=sum(r['deadline_misses'] for r in rs),skipped=sum(r['skipped'] for r in rs)) for k,rs in cycles.items()}}
report=dict(smoothing_ms=window,cpu_note='CPU = 1 - Idle from FreeRTOS runtime accounting; ISR time is attributed to interrupted task. First three capture seconds excluded. Moving/holding labels use the latest sampled velocity, so transition windows can mix states. Cycle reports rotate through tasks; only observed windows are summarized.',scope='App-published motor timestamps (500 Hz) versus raw IMU acquisition-end timestamps (1 kHz); relative observation lag, includes driver/filter/mechanical effects. Positive lag means second signal is later. No CAN TX-complete or shaft sensor hardware timestamp.',summary=summary,legs=legs,cpu=cpureport,decoder_errors=dict(dec.errors),imu_sequence_gaps={str(k):int(np.sum(np.maximum(0,np.diff(val[:,-1])-1))) for k,val in imu.items()},scalar_period_us={k:quant((np.diff(val[:,0])*1e6).tolist()) for k,val in src.items()})
(d/f'latency-analysis{suffix}.json').write_text(json.dumps(report,indent=2)+'\n')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,axs=plt.subplots(4,1,figsize=(12,10),layout='constrained')
for ax in range(3):axs[0].plot(t-t[0],v[:,ax],label=f'ID{ax+1}',lw=.7)
axs[0].set(ylabel='Encoder speed (deg/s)',title='Turntable measured timing and CPU');axs[0].legend();axs[0].grid(alpha=.3)
for k,label in ((256,'BMI088'),(257,'LSM6DS3')):
 xs=[];ys=[]
 for l in legs:
  r=l['imu'][str(k)]
  if r['r2']>.97 and not r['at_boundary']:xs.append(l['axis']+(-.08 if k==256 else .08));ys.append(r['lag_ms'])
 axs[1].scatter(xs,ys,label=label)
axs[1].set(ylabel='IMU minus encoder lag (ms)',xticks=[1,2,3],xlabel='Motor ID');axs[1].legend();axs[1].grid(alpha=.3)
for l in legs:
 r=l['rpm_vs_encoder']
 if r['r2']>.85 and not r['at_boundary']:axs[2].scatter(l['axis'],r['lag_ms'],color='tab:purple')
axs[2].set(ylabel='RPM minus encoder lag (ms)',xticks=[1,2,3],xlabel='Motor ID');axs[2].grid(alpha=.3)
axs[3].plot(cpu[:,0]-t[0],cpu[:,1]);axs[3].set(xlabel='MCU time from capture start (s)',ylabel='CPU busy (%)');axs[3].grid(alpha=.3)
fig.savefig(d/f'latency-cpu{suffix}.png',dpi=150)
print(json.dumps(dict(summary=summary,cpu=cpureport['all_percent'],cycles=cpureport['cycles'].get('TurntableView'),gaps=report['imu_sequence_gaps']),indent=2))
