#!/usr/bin/env python3
"""Offline joint-axis identification; never accesses hardware or writes calibration storage."""
import argparse,collections,itertools,json,struct,sys
from array import array
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
from scipy.spatial.transform import Rotation
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/HostLink'))
from imu_telemetry import Decoder,decode_samples
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);a=p.parse_args();directory=a.capture
plan=json.loads(Path(__file__).with_name('axis_calibration_plan.json').read_text());catalog=json.loads((directory/'catalog.json').read_text());dec=Decoder();src=collections.defaultdict(lambda:array('d'));imu=collections.defaultdict(lambda:array('d'));formats={1:'<f',2:'<i',3:'<I',4:'<?'}
with (directory/'rx.bin').open('rb') as f:
 while data:=f.read(65536):
  for kind,service,msg,seq,payload in dec.feed(data):
   if (kind,service,msg)==(2,4,0x12):
    off=16
    for _ in range(payload[12]):
     index,typ,valid,seq,stamp=struct.unpack_from('<HBBIQ',payload,off);off+=16
     value,=struct.unpack_from(formats[typ],payload,off);off+=struct.calcsize(formats[typ]);src[catalog[str(index)]['name']].extend((stamp*1e-6,value,valid))
   elif (kind,service,msg)==(2,4,2):
    stream,samples=decode_samples(payload)
    if stream in (256,257):
     for s in samples:imu[stream].extend((s['end_us']*1e-6,*s['gyro_rad_s'],*s['accel_m_s2'],s['sequence']))
src={k:np.frombuffer(v,dtype=float).reshape(-1,3) for k,v in src.items()};imu={k:np.frombuffer(v,dtype=float).reshape(-1,8) for k,v in imu.items()}
phase=src['turntable.phase'];ranges={int(k):(v[0,0],v[-1,0]) for k in np.unique(phase[:,1]) if len(v:=phase[phase[:,1]==k])}
# Sensor native -> SDK board; see Board/Support/Devices/sensor_frames.c.
transforms={256:np.diag([1,-1,-1]),257:np.array([[0,1,0],[-1,0,0],[0,0,1]])}
home=np.array([350,4850,5140-4200])*360/8192
for i in (0,1):
 valid=src[f'turntable.motor{i+1}.unwrapped_deg'];valid=valid[valid[:,2]!=0]
 if not len(valid):raise RuntimeError('No valid encoder positions')
 home[i]+=360*round((valid[0,1]-home[i])/360)
def q_at(t):
 return np.array([np.interp(t,src[f'turntable.motor{i+1}.unwrapped_deg'][:,0],src[f'turntable.motor{i+1}.unwrapped_deg'][:,1])-home[i] for i in range(3)]).T*np.pi/180

def body_axis(axes,order,joint,q):
 v=np.broadcast_to(axes[joint],(len(q),3)).copy()
 for downstream in order[order.index(joint)+1:]:v=Rotation.from_rotvec(-q[:,downstream,None]*axes[downstream]).apply(v)
 return v
report={'home_unwrapped_deg':home.tolist(),'sequence_revision':int(src['turntable.sequence_revision'][-1,1]),'phase_ranges':ranges,'decoder_errors':dict(dec.errors),'sensors':{},'scope':'Axes expressed in SDK board frame. Joint positive = increasing encoder. Fitted from complete single-joint probes; coupled legs held out.'}
for stream,data in imu.items():
 name={256:'BMI088',257:'LSM6DS3'}[stream];B=transforms[stream];gyro=data[:,1:4]@B.T;observations=[];legs=[]
 def bias_for(ph):
  if ph not in ranges:return None
  st,en=ranges[ph];m=(data[:,0]>=max(st+.2,en-1.2))&(data[:,0]<=en-.1)
  return gyro[m].mean(axis=0) if m.sum()>300 else None
 for k,step in enumerate(plan):
  ph=100+2*k
  if ph not in ranges or ph+1 not in ranges:continue
  # A capture that begins inside a phase cannot provide its complete displacement.
  if phase[0,1]==ph:continue
  st=ranges[ph][0];en=ranges[ph+1][0]
  # Do not integrate a long settled wait: bias uncertainty grows with duration.
  grid=np.arange(st,en,.01);qq=q_at(grid);target=np.deg2rad(step['target_deg'])
  speed=np.column_stack([np.interp(grid,src[f'turntable.motor{i+1}.speed_dps'][:,0],src[f'turntable.motor{i+1}.speed_dps'][:,1]) for i in range(3)])
  near=(np.max(abs(qq-target),axis=1)<np.deg2rad(.5))&(np.max(abs(speed),axis=1)<1)&(grid>st+.5)
  found=np.flatnonzero(np.convolve(near.astype(int),np.ones(30,dtype=int),mode='valid')==30)
  if len(found):en=min(en,grid[found[0]]+.8)
  m=(data[:,0]>=st)&(data[:,0]<=en);t=data[m,0]
  if len(t)<100:continue
  post=(data[:,0]>en+.1)&(data[:,0]<min(en+1.1,ranges[ph+1][1]-.1))
  after=gyro[post].mean(axis=0) if post.sum()>300 else bias_for(ph+1)
  biases=[b for b in (bias_for(ph-1 if k else 5),after) if b is not None]
  if not biases:continue
  bias=np.mean(biases,axis=0);g=gyro[m]-bias;delta=q_at(en)-q_at(st);integral=np.trapezoid(g,t,axis=0);q=np.mean(q_at(t),axis=0)
  leg=dict(step=k,phase=ph,kind=step['kind'],pose=step['pose'],axis=step['axis'],start_s=st,end_s=en,delta_deg=np.rad2deg(delta).tolist(),gyro_integral_board_deg=np.rad2deg(integral).tolist(),bias_board_rad_s=bias.tolist(),q_mean_rad=q.tolist())
  if step['kind'].startswith('probe') and abs(delta[step['axis']-1])>np.deg2rad(15):
   j=step['axis']-1;v=integral/np.linalg.norm(integral)*np.sign(delta[j]);leg['axis_board']=v.tolist();observations.append((j,q,v,k))
  legs.append(leg)
 if len(observations)<9:raise RuntimeError('Insufficient complete probes')
 # Seed from near-default probes or the known coarse encoder-positive directions.
 seed=np.array([[0,0,1],[0,1,0],[-1,0,0]],float)
 for j in range(3):
  vs=[v for jj,q,v,k in observations if jj==j and np.linalg.norm(np.delete(q,j))<.1]
  if vs:seed[j]=np.mean(vs,axis=0);seed[j]/=np.linalg.norm(seed[j])
 candidates=[]
 for order in itertools.permutations(range(3)):
  def residual(x):
   raw=x.reshape(3,3);axes=raw/np.linalg.norm(raw,axis=1)[:,None];r=[]
   for j,q,v,k in observations:r.extend(body_axis(axes,order,j,q[None,:])[0]-v)
   r.extend(np.linalg.norm(raw,axis=1)-1);return r
  fit=least_squares(residual,seed.ravel(),max_nfev=200);axes=fit.x.reshape(3,3);axes/=np.linalg.norm(axes,axis=1)[:,None]
  errs=[np.rad2deg(np.arccos(np.clip(body_axis(axes,order,j,q[None,:])[0]@v,-1,1))) for j,q,v,k in observations]
  candidates.append((float(np.sqrt(np.mean(np.square(errs)))),order,axes,errs))
 candidates.sort(key=lambda x:x[0]);rms,order,axes,errs=candidates[0]
 # Closest proper frame uses X=ID1,Y=ID2,Z=ID3 at defaults. Measured axes remain separately available.
 U,S,Vt=np.linalg.svd(axes.T);R=U@np.diag([1,1,np.linalg.det(U@Vt)])@Vt
 validation=[]
 import matplotlib
 matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 fig,axs=plt.subplots(3,1,figsize=(11,8),sharex=True,layout='constrained')
 for leg in legs:
  if not leg['kind'].startswith('coupled'):continue
  st,en=leg['start_s'],leg['end_s'];t=np.arange(st+.03,en-.03,.01);q=q_at(t);qd=np.gradient(q,t,axis=0);pred=sum(body_axis(axes,order,j,q)*qd[:,j,None] for j in range(3));obs=np.column_stack([np.interp(t,data[:,0],gyro[:,j]) for j in range(3)])-leg['bias_board_rad_s']
  # Compare angles over 200ms windows to avoid quantized encoder differentiation dominating the metric.
  def box(v):return np.column_stack([np.convolve(v[:,j],np.ones(21)/21,mode='same') for j in range(3)])
  ps,gs=box(pred),box(obs);moving=(np.linalg.norm(ps,axis=1)>.05)&(t>st+.2)&(t<en-.2);res=gs[moving]-ps[moving]
  err=np.linalg.norm(res,axis=1)*180/np.pi
  validation.append({'step':leg['step'],'kind':leg['kind'],'velocity_vector_rmse_210ms_dps':float(np.sqrt(np.mean(err**2))),'predicted_vector_rms_dps':float(np.sqrt(np.mean(np.sum(ps[moving]**2,axis=1)))*180/np.pi),'integration_window_s':[float(t[0]),float(t[-1])],'measured_integral_board_deg':np.rad2deg(np.trapezoid(obs,t,axis=0)).tolist(),'predicted_integral_board_deg':np.rad2deg(np.trapezoid(pred,t,axis=0)).tolist()})
  for j,ax in enumerate(axs):ax.plot(t-st,gs[:,j]*180/np.pi,label=leg['kind']+' IMU');ax.plot(t-st,ps[:,j]*180/np.pi,'--',label=leg['kind']+' encoder model');ax.set_ylabel('XYZ'[j]+' deg/s');ax.grid(alpha=.3)
 for ax in axs:ax.legend(fontsize=7)
 axs[0].set_title(name+' held-out coupled motion in SDK board frame');axs[-1].set_xlabel('Seconds in leg');fig.savefig(directory/(name+'-coupled-validation.png'),dpi=140);plt.close(fig)
 report['sensors'][name]={'samples':len(data),'sequence_gaps':int(np.count_nonzero(np.diff(data[:,-1])!=1)),'native_to_board':B.tolist(),'complete_probe_count':len(observations),'outer_to_inner_motor_ids':[j+1 for j in order],'axis_fit_rms_deg':rms,'axis_fit_max_deg':max(errs),'order_candidates':[{'order':[j+1 for j in o],'rms_deg':r} for r,o,_,_ in candidates],'home_positive_axes_board_rows':axes.tolist(),'home_positive_axes_native_rows':(axes@B).tolist(),'axis_pair_angles_deg':[[float(np.rad2deg(np.arccos(np.clip(x@y,-1,1)))) for y in axes] for x in axes],'R_board_from_motor_nearest_orthogonal':R.tolist(),'quaternion_wxyz':np.roll(Rotation.from_matrix(R).as_quat(),1).tolist(),'legs':legs,'coupled_validation':validation}
(directory/'axis-calibration.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:{key:v for key,v in s.items() if key!='legs'} for k,s in report['sensors'].items()},indent=2))
