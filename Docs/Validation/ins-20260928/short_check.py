import sys,time,json,collections
sys.path.insert(0,'Tools/HostLink')
from imu_telemetry import Client,open_port,ALL_STREAMS
out=open('Docs/Validation/ins-20260928/short-check.jsonl','w')
def record(kind,**kw):
 d=dict(kind=kind,**kw);line=json.dumps(d);print(line,flush=True);out.write(line+'\n');out.flush()
path='/dev/cu.usbmodem355C343932333'
with open_port(path) as port:
 c=Client(port);record('hello',response=c.request(0,1).hex())
 end=time.monotonic()+.3
 while time.monotonic()<end:c.receive()
 record('before_subscribe',counts=dict(c.counts),status=c.ins_status());assert not c.counts
 c.subscribe(streams=ALL_STREAMS)
 end=time.monotonic()+90;last=0
 while time.monotonic()<end:
  c.receive()
  if time.monotonic()-last<1:continue
  last=time.monotonic();st=c.ins_status()
  record('startup',status=st)
  if st['flags']&1:break
 else:raise RuntimeError('INS did not converge within 90s')
 # Independent 10s non-overlapping windows without GDB pauses.
 for window in range(3):
  c.receive();before=c.ins_status();braw=c.status();counts=c.counts.copy();t0=time.monotonic()
  while time.monotonic()-t0<10:c.receive()
  after=c.ins_status();araw=c.status();t1=time.monotonic();seconds=t1-t0
  received={ALL_STREAMS[s]:(c.counts[s]-counts[s])/seconds for s in ALL_STREAMS}
  computed=((after['publications']-before['publications'])&0xffffffff)/((after['updated_us']-before['updated_us'])*1e-6)
  record('window',index=window+1,seconds=seconds,attitude_hz=computed,received_hz=received,
         gaps=dict(c.gaps),duplicates=dict(c.duplicates),errors=dict(c.decoder.errors),status=after,raw_status=araw,latest=c.latest)
  assert computed>=990 and received['INS']>=990
  assert not sum(c.gaps.values()) and not sum(c.duplicates.values()) and not c.decoder.errors
  assert all(v['overflow']==0 and v['failed']==0 for v in after['streams'].values())
 c.subscribe(False,streams=ALL_STREAMS)
 end=time.monotonic()+.2
 while time.monotonic()<end:c.receive()
 counts=c.counts.copy();end=time.monotonic()+.5
 while time.monotonic()<end:c.receive()
 record('cancelled',status=c.ins_status(),counts=dict(c.counts));assert counts==c.counts
 port.dtr=False;time.sleep(.2);port.dtr=True;time.sleep(.1);c=Client(port);c.request(0,1)
 st=c.ins_status();assert all(not s['enabled'] and s['depth']==0 for s in st['streams'].values())
 c.subscribe(streams=[0x102]);t0=time.monotonic()
 while time.monotonic()-t0<1:c.receive()
 record('reconnected',status=c.ins_status(),counts=dict(c.counts),errors=dict(c.decoder.errors));assert c.counts[0x102]>990
 c.subscribe(False,streams=[0x102])
out.close()
