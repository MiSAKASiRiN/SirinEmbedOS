"""Finite one-process USB plans and cooperative cancellation."""
import copy
import contextlib
import os
from pathlib import Path
import re
import tempfile
import tomllib
from .core import Failure, Lock
from .rmt import Sender, validate
from .telemetry import Collector,STREAMS

class Control:
    def __init__(self,run_id):
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,100}',run_id): raise Failure('invalid_run_id','Invalid run ID')
        self.root=Path(tempfile.gettempdir())/('srneos-control-'+str(getattr(os,'getuid',lambda:0)()))
        self.root.mkdir(mode=0o700,exist_ok=True); self.path=self.root/(run_id+'.cancel')
        self.lock=Lock('run:'+run_id); self.ignore=False
    def __enter__(self): self.lock.__enter__(); return self
    def check(self):
        if not self.ignore and self.path.exists(): raise KeyboardInterrupt()
    def __exit__(self,*args):
        with contextlib.suppress(FileNotFoundError): self.path.unlink()
        self.lock.__exit__(*args)

def cancel(run_id):
    control=Control(run_id)
    try:
        with control.lock: pass
    except Failure as exc:
        if exc.code!='resource_busy': raise
        try: control.path.open('x').close()
        except FileExistsError: pass
        return
    raise Failure('run_not_active','No active srneos owner for this run ID',13)

def load_plan(args,config):
    doc=tomllib.loads(Path(args.plan).read_text())
    if doc.get('schema_version')!=1 or set(doc)-{'schema_version','steps','capture'}: raise Failure('invalid_plan','Plan supports schema_version, capture, steps')
    steps=doc.get('steps',[])
    if not steps or len(steps)>100: raise Failure('invalid_plan','Plan needs 1..100 steps')
    seen=set(); prepared=[]; enable_count=0; total=0; motion_kind=None
    for item in steps:
        if set(item)-{'id','operation','duration','vx','vy','wz','values','enable','payload_hex','command_id'}: raise Failure('invalid_plan','Unknown step field')
        if not isinstance(item.get('id'),str) or item['id'] in seen: raise Failure('invalid_plan','Steps need unique IDs')
        seen.add(item['id']); op=item.get('operation')
        if op not in ('move','channels','enable','wait','exec'): raise Failure('unsupported_operation',str(op),22)
        a=copy.copy(args); a.action='enable' if op=='wait' else op
        for key in ('duration','vx','vy','wz','values','enable'):
            if key in item: setattr(a,key,item[key])
        if op in ('move','channels','enable','wait'):
            vals,kind=validate(a,config.get('rmt',{})); total+=a.duration
            wants_enable=op=='enable' or a.enable
            if op=='wait': kind=motion_kind or kind
            if motion_kind is not None and motion_kind!=kind and enable_count:
                raise Failure('invalid_plan','Cannot change motion type after ENABLE')
            motion_kind=kind
            enable_count+=bool(wants_enable)
            prepared.append((item['id'],op,a,vals,kind,wants_enable))
        else:
            if enable_count: raise Failure('invalid_plan','EXEC after ENABLE would interrupt input keepalive')
            try: payload=int(item['command_id']).to_bytes(2,'little')+bytes.fromhex(item.get('payload_hex',''))
            except (KeyError,ValueError,OverflowError) as exc: raise Failure('invalid_plan','Invalid exec ID/payload') from exc
            if len(payload)>256: raise Failure('payload_too_large','EXEC input exceeds 256 bytes')
            total+=args.timeout; prepared.append((item['id'],op,payload))
    if enable_count>1 or total>60: raise Failure('invalid_plan','Plan permits at most one ENABLE and at most 60 seconds of steps')
    capture=doc.get('capture',{})
    if set(capture)-{'streams','sources','hz'}: raise Failure('invalid_plan','Unknown capture field')
    streams=capture.get('streams',''); sources=capture.get('sources',''); hz=capture.get('hz',50)
    if not isinstance(streams,str) or not isinstance(sources,str) or not isinstance(hz,int) or not 1<=hz<=1000:
        raise Failure('invalid_plan','Capture streams/sources must be comma-separated strings; hz=1..1000')
    if any(s not in STREAMS for s in streams.split(',') if s) or sources and (hz<5 or hz%5):
        raise Failure('invalid_plan','Invalid capture stream/rate')
    return prepared,capture

def execute(client,args,result,config,prepared,capture):
    sender=Sender(client,args,result); collector=Collector(client,**capture) if capture else None
    primary=None
    try:
        if collector: collector.start()
        for step in prepared:
            name,op=step[:2]
            if op=='exec':
                # No blocking requests while input authority is held; cannot starve input keepalive.
                if sender.enable_sent: raise Failure('invalid_plan','EXEC after ENABLE is unsupported; use telemetry observation')
                payload=client.request(1,1,step[2]); result.step(name,response_hex=payload.hex())
            else:
                _,_,a,vals,kind,wants_enable=step
                if op=='wait': vals=[0]*len(sender.values); kind=sender.kind
                sender.start(vals,kind,wants_enable)
                sender.stream(vals,a.duration); result.step(name,execution='sent',validation='not_evaluated')
        if collector:
            missing=collector.targets-set(collector.counts)
            if missing or collector.issues or client.decoder.errors:
                raise Failure('capture_incomplete','Plan telemetry did not meet collection requirements',40,
                              missing=sorted(missing),quality_issues=dict(collector.issues))
    except BaseException as exc: primary=exc; raise
    finally:
        client.cancel_check=lambda:None
        try: sender.stop()
        except Failure as exc:
            if primary: result.data.setdefault('errors',[]).append(exc.record())
            else: primary=exc
        if collector:
            try: collector.close()
            except Failure as exc:
                if primary: result.data.setdefault('errors',[]).append(exc.record())
                else: primary=exc
        if primary and __import__('sys').exc_info()[0] is None: raise primary
