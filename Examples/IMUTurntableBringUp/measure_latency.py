#!/usr/bin/env python3
"""Capture MCU-timestamped motor/IMU data and CPU; optional existing recipe, then hold.
No EEPROM writes, no arbitrary motor commands, and no automatic force-off.
"""
import argparse,collections,json,secrets,struct,sys,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/HostLink'))
from imu_telemetry import Client,open_port,Decoder,pack,decode_samples,decode_ins_status
from system_monitor import decode as decode_monitor
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',required=True);p.add_argument('--output',type=Path,required=True)
p.add_argument('--motion',action='store_true');p.add_argument('--timeout',type=float,default=900)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
raw=(a.output/'rx.bin').open('wb');monitor=(a.output/'monitor.jsonl').open('w');jobs=(a.output/'jobs.jsonl').open('w')
catalog={};latest={};counts=collections.Counter();job=0;active=False;result='in_progress'
class MeasurementClient(Client):
 def receive(self):
  data=self.port.read(max(1,min(self.port.in_waiting,16384)))
  if not data:return
  raw.write(data)
  for kind,s,m,seq,b in self.decoder.feed(data):
   if kind==1:self.replies[seq]=(s,m,b)
   elif (kind,s,m)==(2,4,2):
    stream=struct.unpack_from('<H',b)[0]
    if stream in (1,2,3):
     report=decode_monitor(b);monitor.write(json.dumps(report)+'\n')
     if stream==2:
      latest['cpu']=report['records'][-1]['busy_ppm']/10000
    else:
     stream,samples=decode_samples(b);counts[stream]+=len(samples)
   elif (kind,s,m)==(2,4,0x12):
    at=16
    for _ in range(b[12]):
     i,t,valid,seq,stamp=struct.unpack_from('<HBBIQ',b,at);at+=16
     fmt={1:'<f',2:'<i',3:'<I',4:'<?'}[t];v=struct.unpack_from(fmt,b,at)[0];at+=struct.calcsize(fmt)
     latest[catalog[i]['name']]=v
     self.device_us=stamp
 def once(self,payload):
  self.sequence+=1;seq=self.sequence;self.port.write(pack(0x40,2,seq,payload));end=time.monotonic()+2
  while time.monotonic()<end:
   self.receive()
   if seq in self.replies:
    s,m,b=self.replies.pop(seq);code=struct.unpack_from('<H',b)[0]
    if code:raise RuntimeError(f'command status {code:x}')
    return b[2:]
  raise TimeoutError('command outcome unconfirmed; no automatic retry')
with open_port(a.port) as port:
 c=MeasurementClient(port);subscribed=False
 def status():
  for attempt in range(8):
   try:
    body=c.request(0x40,2,struct.pack('<BI',2,job) if job else b'\0');break
   except ValueError as exc:
    if '0x0106' not in str(exc) or attempt==7:raise
    c.receive() # Published snapshot can be BUSY during the 1 ms writer.
  values=struct.unpack('<9I9f4I',body)
  return dict(zip(('magic','version','job','phase','step','total','records','error','saved','elapsed','temperature','target_temperature','p1','p2','p3','t1','t2','t3','store1','store2','rev1','rev2'),values))
 def cancel():
  c.once(struct.pack('<BI',5,job))
 try:
  initial=status();assert initial['phase'] in (0,10),initial
  cursor=0
  while cursor!=65535:
   b=c.request(4,0x10,struct.pack('<HB',cursor,8));cat_hash,total,cursor,n=struct.unpack_from('<IHHB',b);at=9
   for _ in range(n):
    i,t,size,maximum,hz,version,namelen,unitlen=struct.unpack_from('<HBBHHBBB',b,at);at+=11
    name=b[at:at+namelen].decode();at+=namelen;unit=b[at:at+unitlen].decode();at+=unitlen
    catalog[i]=dict(name=name,type=t,max_hz=maximum,unit=unit)
  (a.output/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
  rates={}
  for i,s in catalog.items():
   name=s['name']
   if name.endswith(('.encoder_raw','.unwrapped_deg','.voltage_raw','.feedback_rpm','.current_raw')):rates[i]=500
   elif name in ('turntable.phase','turntable.fault','turntable.sequence_revision'):rates[i]=min(50,s['max_hz'])
  assert sum(rates.values())<=8000
  c.request(4,0x11,struct.pack('<IB',cat_hash,len(rates))+b''.join(struct.pack('<HH',i,hz) for i,hz in rates.items()))
  for stream in (0x100,0x101,0x102,1,2,3):c.request(4,1,struct.pack('<HIB',stream,1000 if stream>=0x100 else 1000000,1))
  subscribed=True;started=time.monotonic();next_status=started;next_print=started;ready_since=None;finished=None;last=None
  while time.monotonic()-started<a.timeout:
   c.receive();now=time.monotonic()
   if now>=next_status:
    s=status();s['device_us_latest_scalar']=getattr(c,'device_us',None);jobs.write(json.dumps(s)+'\n');jobs.flush();next_status=now+.25
    if not job:
     ins=decode_ins_status(c.request(4,9))
     if ins['phase']==6:
      if ready_since is None:ready_since=now
     else:ready_since=None
     if ready_since and now-ready_since>=10:
      if not a.motion:result='idle_complete';break
      job=secrets.randbits(32) or 1;active=True;c.once(struct.pack('<BII',1,job,0x54544332));print('motion started',job,flush=True)
    else:
     if s['phase']==7 and finished is None:
      cancel();finished=now;result='recipe_complete_holding'
     elif s['phase'] in (10,11) and finished is None:
      finished=now;result='aborted_holding' if s['phase']==10 else 'motor_fault'
     if finished and now-finished>=20:break
    last=s
   if now>=next_print:
    print(json.dumps(dict(seconds=round(now-started,1),state=last,values=latest,counts=dict(counts))),flush=True);next_print=now+5
  else:
   if active:cancel()
   result='timeout_cancel_requested'
 except BaseException:
  result='capture_error_cancel_requested'
  if active:
   try:cancel()
   except Exception:pass
  raise
 finally:
  if subscribed:
   for stream in (0x100,0x101,0x102,1,2,3):
    try:c.request(4,1,struct.pack('<HIB',stream,1000 if stream>=0x100 else 1000000,0))
    except Exception:pass
   try:c.request(4,0x14,struct.pack('<I',cat_hash))
   except Exception:pass
  summary=dict(result=result,job=job,latest=latest,counts=dict(counts),errors=dict(c.decoder.errors))
  (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
  print(json.dumps(summary),flush=True);raw.close();monitor.close();jobs.close()
