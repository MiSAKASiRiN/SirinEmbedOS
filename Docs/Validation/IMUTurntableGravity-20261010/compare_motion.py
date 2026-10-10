from pathlib import Path
import sys,json,struct,collections
from array import array
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
sys.path.insert(0,str(Path('Tools/HostLink').resolve()))
from imu_telemetry import Decoder,decode_samples
base=Path('build/IMUTurntable27/board');out={}
for run in sys.argv[1:]:
 p=base/run;catalog=json.loads((p/'catalog.json').read_text());sources=collections.defaultdict(lambda:array('d'));imus=collections.defaultdict(lambda:array('d'));decoder=Decoder();formats={1:'<f',2:'<i',3:'<I',4:'<?'}
 with (p/'rx.bin').open('rb') as f:
  while data:=f.read(65536):
   for kind,service,msg,seq,payload in decoder.feed(data):
    if (kind,service,msg)==(2,4,0x12):
     off=16
     for _ in range(payload[12]):
      index,typ,valid,seq,stamp=struct.unpack_from('<HBBIQ',payload,off);off+=16
      value,=struct.unpack_from(formats[typ],payload,off);off+=struct.calcsize(formats[typ]);sources[catalog[str(index)]['name']].extend((stamp*1e-6,float(value),valid,seq))
    elif (kind,service,msg)==(2,4,2):
     stream,samples=decode_samples(payload)
     if stream in (256,257):
      for s in samples:imus[stream].extend((s['end_us']*1e-6,*s['gyro_rad_s']))
 sources={k:np.frombuffer(v,dtype=float).reshape(-1,4) for k,v in sources.items()};imus={k:np.frombuffer(v,dtype=float).reshape(-1,4) for k,v in imus.items()}
 meta=json.loads((p/'analysis.json').read_text());ranges=meta['phase_ranges_us_clock_seconds'];report={};fig,axs=plt.subplots(3,2,figsize=(13,11),layout='constrained')
 for motor,phases in ((1,(6,8)),(2,(12,14)),(3,(18,20))):
  entry={};report[str(motor)]=entry
  def sample(name,t):
   v=sources[f'turntable.motor{motor}.'+name];return np.interp(t,v[:,0],v[:,1])
  bstart,bend=ranges[str({1:5,2:11,3:15}[motor])];tb=np.arange(bstart+1,bend-.1,.005);home=np.median(sample('unwrapped_deg',tb));entry['baseline_encoder_span_deg']=float(np.ptp(sample('unwrapped_deg',tb)))
  for col,phase in enumerate(phases):
   label=('lower_to_upper' if motor==3 else 'forward') if col==0 else 'return'
   st,en=ranges[str(phase)];t=np.arange(st,en,.005);pos=sample('unwrapped_deg',t);speed=sample('speed_dps',t);u=sample('voltage_raw',t)
   target=((8192-4200+2160-64)*360/8192 if motor==3 else home+361) if col==0 else home
   limit=54 if motor==1 else 27;requested=np.clip(4*(target-pos),-limit,limit)
   m=(np.abs(target-pos)>limit/4+3)&(t>st+1)&(t<en-1)
   rec={'encoder_peak_abs_dps':float(max(abs(speed))),'voltage_peak_abs':float(max(abs(u))),'cruise_seconds':float(m.sum()*.005),'encoder_cruise_tracking_rms_dps':float(np.sqrt(np.mean((speed[m]-requested[m])**2)))};entry[label]=rec
   ax=axs[motor-1,col];ax.plot(t-st,speed,'k',label='Encoder 100ms');ax.plot(t-st,requested,'k--',label='Requested')
   for stream,name in ((256,'BMI088'),(257,'LSM6DS3')):
    cal=meta['sequence']['axes'][str(motor)]['segments'][label]['sensors'][name];imu=imus[stream];g=(imu[:,1:4]-cal['bias_rad_s'])@np.array(cal['axis_in_sensor_frame'])*180/np.pi
    g21=np.convolve(g,np.ones(21)/21,mode='same');g51=np.convolve(g,np.ones(51)/51,mode='same');trend=np.convolve(g21,np.ones(501)/501,mode='same')
    v=np.interp(t,imu[:,0],g21);slow=np.interp(t,imu[:,0],trend);ripple=v-slow
    sel=(imu[:,0]>st)&(imu[:,0]<en)
    rec[name]={'peak_abs_51ms_dps':float(max(abs(g51[sel]))),'cruise_ripple_rms_dps':float(np.sqrt(np.mean(ripple[m]**2))), 'cruise_p05_p95_dps':np.percentile(v[m],[5,95]).tolist(), 'startup_over_1p5_target_ms':float(np.count_nonzero((imu[:,0]>=st)&(imu[:,0]<st+1)&(abs(g51)>1.5*limit))), 'startup_peak_time_s':float(imu[np.flatnonzero(sel)[np.argmax(abs(g51[sel]))],0]-st)}
    ax.plot(imu[sel,0]-st,g51[sel],label=name+' 51ms',alpha=.65)
   ax.set(title=f'{run} ID{motor} {label}',xlabel='Seconds in phase',ylabel='deg/s');ax.grid(alpha=.3);ax.legend(fontsize=8)
 out[run]=report;fig.savefig(base/(run+'-speed-quality.png'),dpi=140);plt.close(fig)
(base/'motion-quality-comparison.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
