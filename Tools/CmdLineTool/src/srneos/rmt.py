"""Bounded SirinRMT input simulation, not actuator authorization."""
import math
import secrets
import time
from .core import Failure
from ._legacy import sirin_rmt as codec

class Sender:
    def __init__(self,client,args,result):
        self.client=client; self.args=args; self.result=result
        self.session=secrets.randbelow(0xffffffff)+1; self.sequence=0
        self.kind='velocity'; self.values=[0.,0.,0.]; self.enable_sent=False
        self.max_gap=0.; self.previous=None; self.sent=0
        self.engaged=False
    def emit(self,payload):
        seq=self.sequence; self.sequence=(seq+1)&0xffffffff
        self.client.send(0x21,1,payload,kind=2,sequence=seq); self.sent+=1
    def target(self,values):
        self.engaged=True
        now=time.monotonic()
        if self.previous is not None: self.max_gap=max(self.max_gap,now-self.previous)
        self.previous=now
        if self.kind=='velocity': self.emit(codec.planar_velocity(self.session,self.sequence,*values))
        else:
            for start in range(0,len(values),7): self.emit(codec.channels(self.session,self.sequence,start,values[start:start+7]))
        self.emit(codec.buttons(self.session,self.sequence,0xffffffffffffffff,0))
    def stream(self,values,seconds):
        deadline=time.monotonic()+seconds; next_send=time.monotonic(); period=1/self.args.hz
        while time.monotonic()<deadline:
            now=time.monotonic()
            if now>=next_send:
                if now-next_send>max(0.1,3*period): raise Failure('rmt_deadline','Input scheduling deadline missed',24)
                self.target(values); next_send=now+period
            self.client.pump()
    def start(self,values,kind,enable):
        if kind!=self.kind and self.enable_sent: raise Failure('motion_kind_change','Change input type only after stopping')
        self.kind=kind; self.values=values
        self.stream([0]*len(values),self.args.qualification_ms/1000)
        if enable:
            if self.enable_sent: raise Failure('enable_duplicate','Only one ENABLE per run')
            self.emit(codec.enable(self.session,self.sequence)); self.enable_sent=True
    def stop(self):
        if not self.engaged:
            self.result.data['final_state']['stop']='not_requested'
            return
        errors=[]
        for operation in (lambda:self.target([0]*len(self.values)),
                          lambda:self.emit(codec.disable(self.session,self.sequence)),
                          lambda:self.stream([0]*len(self.values),self.args.stop_settle_ms/1000),
                          lambda:self.emit(codec.invalidate(self.session,self.sequence))):
            try: operation()
            except Exception as exc: errors.append(str(exc))
        self.result.data['final_state']['stop']='unconfirmed' if errors else 'sent'
        self.result.step('rmt',session=self.session,next_sequence=self.sequence,frames_sent=self.sent,
            enable_sent=self.enable_sent,max_target_gap_seconds=self.max_gap,validation='not_evaluated',stop_errors=errors)
        if errors: raise Failure('stop_unconfirmed','Stop could not be fully sent',24,errors=errors)

def validate(args,config):
    if not 0<args.duration<=min(60,config.get('max_duration_seconds',60)): raise Failure('invalid_duration','RMT duration must be positive and within configured maximum (<=60 s)')
    if not 1<=args.hz<=100 or (args.enable or args.action=='enable') and args.hz<20: raise Failure('invalid_rate','RMT requires 1..100 Hz; enable needs >=20 Hz')
    if not 100<=args.qualification_ms<=5000 or not 20<=args.stop_settle_ms<=1000: raise Failure('invalid_timing','qualification_ms=100..5000, stop_settle_ms=20..1000')
    if args.action=='channels':
        vals=[float(v) for v in args.values.split(',')]
        if not 1<=len(vals)<=16 or any(not math.isfinite(v) or abs(v)>100 for v in vals): raise Failure('invalid_channels','Expected 1..16 percentages in -100..100')
        return [round(v*100) for v in vals],'channels'
    vals=[args.vx,args.vy,args.wz] if args.action=='move' else [0.,0.,0.]
    try: codec.planar_velocity(1,0,*vals)
    except ValueError as exc: raise Failure('invalid_velocity',str(exc)) from exc
    for key,value in zip(('max_abs_vx_m_s','max_abs_vy_m_s','max_abs_wz_rad_s'),vals):
        if not math.isfinite(value): raise Failure('invalid_velocity','Velocity must be finite')
        limit=config.get(key)
        if value and not args.dry_run and (limit is None or not math.isfinite(limit) or limit<=0 or abs(value)>limit):
            raise Failure('motion_limit','Nonzero movement requires matching positive limits in [rmt] configuration')
    return vals,'velocity'

def run(client,args,result,config):
    vals,kind=validate(args,config); sender=Sender(client,args,result)
    primary=None
    try:
        sender.start(vals,kind,args.enable or args.action=='enable')
        sender.stream(vals,args.duration)
    except BaseException as exc: primary=exc; raise
    finally:
        client.cancel_check=lambda:None
        try: sender.stop()
        except Failure as exc:
            if primary: result.data.setdefault('errors',[]).append(exc.record())
            else: raise
