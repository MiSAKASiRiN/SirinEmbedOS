"""Synthetic unwrapped full-circle/limited-axis analysis regression (NumPy/Matplotlib)."""
import sys
from pathlib import Path
import tempfile
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'Examples/IMUTurntableBringUp'))
from sequence_analysis import analyze
# Long enough stationary windows and two full circles on each continuous axis.
phases=[5,6,7,8,11,12,13,14,15,16,17,18,19,20,9]
durations=[5,121,3,121,5,121,3,121,5,13,3,89,3,76,6]
edges=np.r_[0,np.cumsum(durations)];t=np.arange(0,edges[-1],.001)
position=np.zeros((len(t),3));velocity=np.zeros_like(position);ranges={}
segments={6:(0,361),8:(0,-361),12:(1,361),14:(1,-361),16:(2,-876*360/8192),18:(2,6024*360/8192),20:(2,-5148*360/8192)}
for n,phase in enumerate(phases):
 begin,end=edges[n:n+2];mask=(t>=begin)&(t<end);ranges[str(phase)]=[float(begin),float(end-.01)]
 if phase in segments:
  axis,delta=segments[phase];rate=delta/(end-begin)
  position[:,axis]+=np.clip(t-begin,0,end-begin)*rate
  velocity[mask,axis]=rate
sources={f'turntable.motor{i+1}.unwrapped_deg':np.column_stack((t,position[:,i],np.ones(len(t)),np.arange(len(t)))) for i in range(3)}
# Both IMUs see the same fixed-axis rate plus a removable constant bias.
bias=np.array([.001,-.002,.003]);imu=np.column_stack((t,velocity*np.pi/180+bias,np.zeros((len(t),3)),np.arange(len(t))))
with tempfile.TemporaryDirectory() as tmp:
 report=analyze(sources,{256:imu,257:imu},ranges,Path(tmp))
 assert report['complete']
 for i in (1,2):assert report['axes'][str(i)]['segments']['forward']['at_least_one_turn']
 for axis in report['axes'].values():
  assert abs(axis['encoder_loop_delta_deg'])<.001
  for segment in axis['segments'].values():
   for sensor in segment['sensors'].values():
    assert abs(sensor['difference_deg'])<.04
    assert sensor['sequence_discontinuities']==0
 print('PASS: 361-degree forward/return, wrapped-axis arc, dual-IMU projection and loop closure')
