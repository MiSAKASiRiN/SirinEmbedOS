from collections import Counter
import struct
import time
from .core import Failure, dumps
from .protocols import catalog, source_sample
from ._legacy.imu_telemetry import decode_samples
from ._legacy.system_monitor import decode as decode_system
STREAMS={'tasks':1,'system':2,'cycles':3,'bmi':0x100,'lsm':0x101,'ins':0x102,'mag':0x103,'pressure':0x104}

class Collector:
    def __init__(self,client,streams='',sources='',hz=50,callback=None):
        self.client=client; self.streams=[x for x in streams.split(',') if x]
        self.sources=[x for x in sources.split(',') if x]; self.hz=hz
        self.active=[]; self.source_ids=[]; self.source_hash=None
        self.counts=Counter(); self.latest={}; self.issues=Counter(); self.sequences={}; self.callback=callback
        self.previous=client.handler; self.names={}; self.targets=set(self.streams+self.sources)
    def start(self):
        if any(s not in STREAMS for s in self.streams): raise Failure('unknown_stream','Unknown stream name')
        self.client.handler=self.receive
        for name in self.streams:
            sid=STREAMS[name]; period=1000000 if sid<0x100 else 1000000//self.hz
            self.client.request(4,1,struct.pack('<HIB',sid,period,1)); self.active.append(sid)
        if self.sources:
            if not 5<=self.hz<=1000 or self.hz%5: raise Failure('invalid_rate','SOURCE V2 rate must be 5..1000 in steps of 5')
            h,items=catalog(self.client)
            chosen=[i for i in items if i['name'] in self.sources]
            if len(chosen)!=len(self.sources): raise Failure('source_not_found','Source name not in directory',23)
            if any(self.hz>i['max_hz'] for i in chosen): raise Failure('invalid_rate','Rate exceeds source maxHz')
            ids=[i['index'] for i in chosen]; self.names={i['index']:i['name'] for i in chosen}
            self.source_hash=h
            self.client.request(4,0x11,struct.pack('<IB',h,len(ids))+b''.join(struct.pack('<HH',i,self.hz) for i in ids))
            self.source_ids=ids
        return self
    def receive(self,frame):
        kind,service,message,seq,payload=frame
        if service!=4: return self.previous(frame)
        try:
            if message==2 and len(payload)>=2:
                sid=struct.unpack_from('<H',payload)[0]
                if sid not in self.active: return
                name=next(k for k,v in STREAMS.items() if v==sid)
                if sid<0x100: record=decode_system(payload)
                else:
                    _,samples=decode_samples(payload); record=dict(stream=name,samples=samples)
                self.accept(name,record)
            elif message==0x12:
                report=source_sample(payload)
                if self.source_hash!=report['catalog_hash']: raise Failure('catalog_changed','SOURCE catalog changed',22)
                for item in report['sources']:
                    if item['index'] not in self.names: continue
                    name=self.names[item['index']]
                    if not item['valid']: self.issues['invalid_source']+=1; continue
                    # SOURCE publishes latest values; gaps are expected, duplicates are not new samples.
                    old=self.sequences.get(name)
                    if old==item['sequence']: continue
                    self.sequences[name]=item['sequence']; self.accept(name,item)
        except Failure as exc:
            if exc.exit_code not in (22,23): raise
            self.issues[str(exc)]+=1
        except (ValueError,struct.error) as exc:
            self.issues[str(exc)]+=1
    def accept(self,name,record):
        record=dict(record,received_monotonic_ns=str(time.monotonic_ns()),topic=name)
        self.counts[name]+=1; self.latest[name]=record
        self.client.rec.emit('sample',record)
        if self.callback: self.callback(record)
    def close(self):
        errors=[]
        for sid in self.active:
            try: self.client.request(4,1,struct.pack('<HIB',sid,1000000,0),timeout=0.5)
            except Failure as exc: errors.append(exc.record())
        if self.source_ids:
            try: self.client.request(4,0x13,struct.pack('<IB',self.source_hash,len(self.source_ids))+b''.join(struct.pack('<H',i) for i in self.source_ids),timeout=0.5)
            except Failure as exc: errors.append(exc.record())
        self.client.handler=self.previous
        self.client.result.step('telemetry_summary',counts=dict(self.counts),quality_issues=dict(self.issues),
                                decode_errors=dict(self.client.decoder.errors),missing=sorted(self.targets-set(self.counts)))
        if errors: raise Failure('unsubscribe_failed','Failed to cancel subscriptions',24,errors=errors)


def capture(client,args,result):
    callback=(lambda record:print(dumps(dict(schema_version=1,type='sample',run_id=result.data['run_id'],sample=record)),flush=True)) if args.jsonl else None
    collector=Collector(client,args.streams,args.sources,args.hz,callback)
    try:
        collector.start()
        deadline=time.monotonic()+(args.timeout if args.action=='read' else args.duration)
        while time.monotonic()<deadline:
            client.pump()
            if args.action=='read' and collector.targets<=set(collector.counts): break
        missing=collector.targets-set(collector.counts)
        result.step('telemetry',latest=collector.latest,counts=dict(collector.counts))
        if args.save:
            if not result.output: raise Failure('output_required','capture saving requires run output directory')
            # Writer finalizes samples after session close; exported by caller.
        if missing: raise Failure('telemetry_missing','No samples from requested topics',24,missing=sorted(missing))
        if collector.issues or client.decoder.errors: raise Failure('telemetry_quality','Malformed/invalid telemetry observed',40)
    finally:
        active_error=__import__('sys').exc_info()[0] is not None
        client.cancel_check=lambda:None
        try: collector.close()
        except Failure as exc:
            if active_error: result.data.setdefault('errors',[]).append(exc.record())
            else: raise
