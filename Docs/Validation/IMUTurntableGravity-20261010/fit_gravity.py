from pathlib import Path
s=Path('build/IMUTurntable27/id3_load_review.py').read_text()
s=s[:s.index(' report={};fig')]
s+='''
 vals=[]
 for phase in (15,17,19,9):
  st,en=meta['phase_ranges_us_clock_seconds'][str(phase)]
  t=np.arange(max(st,en-1),en,.01)
  pos=sample('unwrapped_deg',t);u=sample('voltage_raw',t)
  vals.append((float(np.mean(pos)),float(np.mean(u))))
  print(run,phase,'position/voltage/speed',np.mean(pos),np.mean(u),np.max(abs(sample('speed_dps',t))))
 x=np.deg2rad(np.array(vals)[:,0]); y=np.array(vals)[:,1]
 for offset in (False,True):
  X=np.column_stack([np.cos(x),np.sin(x)]+([np.ones(len(x))] if offset else []))
  c=np.linalg.lstsq(X,y,rcond=None)[0];print('fit cos/sin/offset',c,'residual',y-X@c)
'''
exec(s)
