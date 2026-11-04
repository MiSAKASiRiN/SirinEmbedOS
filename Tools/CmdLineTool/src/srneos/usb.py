"""USB inventory and one-owner SirinLink sessions."""
from collections import Counter
import contextlib
import json
import os
from pathlib import Path
import plistlib
import queue
import secrets
import struct
import sys
import threading
import time
from .core import Failure, Lock, process, dumps, file_artifact
from ._legacy.imu_telemetry import Decoder, pack

class Recorder:
    """Bounded nonblocking producer; file thread never owns the serial port."""
    def __init__(self,result):
        self.result=result; self.q=queue.Queue(maxsize=2048); self.error=None; self.thread=None
        self.closed=False
        if result.output:
            self.thread=threading.Thread(target=self.worker,daemon=True)
            self.thread.start()
    def worker(self):
        files={}; offsets={'rx':0,'tx':0}
        try:
            for name in ('cdc1_rx.bin','cdc1_tx.bin','io.jsonl','samples.jsonl'):
                files[name]=open(self.result.output/(name+'.partial'),'xb')
            while True:
                item=self.q.get()
                if item is None: break
                kind,stamp,data=item
                if kind in ('rx','tx'):
                    files[f'cdc1_{kind}.bin'].write(data)
                    record=dict(direction=kind,monotonic_ns=str(stamp),offset=offsets[kind],length=len(data))
                    files['io.jsonl'].write((dumps(record)+'\n').encode()); offsets[kind]+=len(data)
                else: files['samples.jsonl'].write((dumps(data)+'\n').encode())
            for name,f in files.items():
                f.flush(); os.fsync(f.fileno()); f.close()
                path=self.result.output/name; partial=self.result.output/(name+'.partial')
                os.link(partial,path); partial.unlink()
                self.result.data['artifacts'].append(file_artifact(path,name))
        except Exception as exc: self.error=exc
        finally:
            for f in files.values():
                with contextlib.suppress(Exception): f.close()
    def check(self):
        if self.error: raise Failure('capture_write_failed',str(self.error),30)
    def emit(self,kind,data):
        self.check()
        if self.thread:
            try: self.q.put_nowait((kind,time.monotonic_ns(),data))
            except queue.Full as exc: raise Failure('capture_overflow','Disk queue full; capture incomplete',30) from exc
    def close(self):
        if self.closed: return
        self.closed=True
        if self.thread:
            try: self.q.put(None,timeout=1)
            except queue.Full: raise Failure('capture_overflow','Writer did not drain',30)
            self.thread.join(3)
            if self.thread.is_alive(): raise Failure('capture_write_timeout','Writer did not finish; recording is incomplete',30)
            self.check()

def inventory():
    from serial.tools import list_ports
    ports=[dict(path=p.device,vid=p.vid,pid=p.pid,serial=p.serial_number,
                interface=p.interface,location=p.location,description=p.description) for p in list_ports.comports()]
    devices=[]; reliable=False
    try:
        if sys.platform=='darwin':
            rc,out,_=process(['ioreg','-a','-r','-c','IOUSBHostDevice'],5)
            if rc==0:
                reliable=True
                def walk(nodes):
                    for n in nodes:
                        if n.get('idVendor')==0x483 and n.get('idProduct')==0x52a4:
                            devices.append(dict(vid=n['idVendor'],pid=n['idProduct'],serial=n.get('USB Serial Number')))
                        walk(n.get('IORegistryEntryChildren',[]))
                walk(plistlib.loads(out))
        elif sys.platform.startswith('linux') and Path('/sys/bus/usb/devices').is_dir():
            reliable=True
            for d in Path('/sys/bus/usb/devices').iterdir():
                if (d/'idVendor').exists() and (d/'idProduct').exists():
                    vid=int((d/'idVendor').read_text(),16); pid=int((d/'idProduct').read_text(),16)
                    if (vid,pid)==(0x483,0x52a4):
                        devices.append(dict(vid=vid,pid=pid,serial=(d/'serial').read_text().strip() if (d/'serial').exists() else None))
        elif os.name=='nt':
            rc,out,_=process(['powershell','-NoProfile','-NonInteractive','-Command',
                "Get-PnpDevice -PresentOnly | Where-Object {$_.InstanceId -match '^USB\\\\VID_0483&PID_52A4\\\\'} | Select-Object -ExpandProperty InstanceId | ConvertTo-Json -Compress"],5)
            if rc==0:
                rows=json.loads(out or b'[]'); rows=[rows] if isinstance(rows,str) else rows
                devices=[dict(vid=0x483,pid=0x52a4,serial=x.rsplit('\\',1)[-1]) for x in rows]; reliable=True
    except (OSError,ValueError,Failure): pass
    return dict(devices=devices,ports=ports,enumeration_reliable=reliable)

def select_port(args,result,channel):
    inv=inventory(); explicit=getattr(args,'cdc'+str(channel),None)
    result.step('usb_inventory',**inv)
    if explicit:
        match=[p for p in inv['ports'] if p['path']==explicit]
        result.data['states']['usb']='enumerated' if match and match[0]['vid'] is not None else 'unknown'
        return explicit
    devices=[d for d in inv['devices'] if not args.usb_serial or d['serial']==args.usb_serial]
    candidates=[p for p in inv['ports'] if (p['vid'],p['pid'])==(0x483,0x52a4) and
                (not args.usb_serial or p['serial']==args.usb_serial)]
    identities={(p['serial'] or p['location']) for p in candidates}
    if len(devices)>1 or len(identities)>1: raise Failure('ambiguous_device','Specify --usb-serial or explicit CDC path',14)
    marker='LOG' if channel==0 else 'SRN-LNK'
    selected=[p for p in candidates if marker in (p['interface'] or '')]
    if len(selected)==1:
        result.data['states']['usb']='enumerated'; return selected[0]['path']
    if candidates or devices:
        result.data['states']['usb']='enumerated'
        result.data['states']['cdc'+str(channel)]='missing' if not candidates else 'unknown'
        raise Failure('cdc_unresolved','USB found; specify --cdc0/--cdc1 because interface mapping is unavailable',20)
    result.data['states']['usb']='not_enumerated' if inv['enumeration_reliable'] else 'unknown'
    raise Failure('usb_not_enumerated' if inv['enumeration_reliable'] else 'usb_enumeration_unknown',
                  'No selected USB device/CDC found',20 if inv['enumeration_reliable'] else 10)

class Session:
    def __init__(self,path,result,timeout=3,port_factory=None):
        self.path=path; self.result=result; self.timeout=timeout; self.factory=port_factory
        self.port=None; self.lock=None; self.decoder=Decoder(); self.sequence=secrets.randbits(31)
        self.handler=lambda frame:None; self.replies={}; self.rec=Recorder(result)
    def __enter__(self):
        import serial
        try:
            self.lock=Lock('serial:'+os.path.realpath(self.path)); self.lock.__enter__()
            try:
                self.port=(self.factory or serial.Serial)(self.path,115200,timeout=0.005,write_timeout=0.2,
                                                         **({'exclusive':True} if os.name!='nt' else {}))
            except (OSError,serial.SerialException) as exc:
                error=str(exc).lower()
                state='permission_denied' if 'permission' in error or 'access is denied' in error else 'busy' if 'busy' in error or 'exclus' in error else 'missing' if 'no such' in error or 'not found' in error else 'unknown'
                self.result.data['states']['cdc1']=state
                raise Failure('cdc_'+state,str(exc),20 if state=='missing' else 14) from exc
            self.port.dtr=True
            self.result.data['states']['cdc1']='open'
            try: hello=self.request(0,1)
            except Failure as exc:
                state='invalid_protocol' if self.decoder.errors else 'no_response'
                self.result.data['states']['sirinlink']=state
                if exc.code in ('response_timeout','protocol_decode'):
                    raise Failure('protocol_'+state,str(exc),21 if state=='no_response' else 22) from exc
                raise
            if len(hello)!=12 or hello[0]!=2 or struct.unpack_from('<H',hello,2)[0]!=480:
                self.result.data['states']['sirinlink']='incompatible'
                raise Failure('protocol_incompatible','Unexpected HELLO capabilities',22)
            self.result.data['states']['sirinlink']='ready'
            self.result.step('hello',payload_hex=hello.hex(),firmware_identity='not_available')
            self.result.data['evidence_level']='hardware'
            return self
        except Exception:
            self.__exit__(*sys.exc_info()); raise
    def send(self,service,message,payload=b'',kind=0,sequence=None):
        if sequence is None:
            self.sequence=(self.sequence+1)&0xffffffff; sequence=self.sequence
        data=pack(service,message,sequence,payload,kind)
        try:
            n=self.port.write(data)
            if n!=len(data): raise OSError('Short serial write')
        except OSError as exc:
            self.result.data['states']['sirinlink']='disconnected'
            raise Failure('serial_write_failed',str(exc),24,execution='unknown') from exc
        self.rec.emit('tx',data)
        return sequence
    def pump(self):
        getattr(self,'cancel_check',lambda:None)()
        self.rec.check()
        try: data=self.port.read(max(1,min(self.port.in_waiting,4096)))
        except OSError as exc:
            self.result.data['states']['sirinlink']='disconnected'
            raise Failure('serial_disconnected',str(exc),24) from exc
        if data: self.rec.emit('rx',data)
        for frame in self.decoder.feed(data):
            kind,service,message,seq,payload=frame
            if kind==1:
                # Only retain replies to this process's active request. No stale/unbounded cache.
                if (service,message,seq)==getattr(self,'pending',None):
                    self.replies.setdefault(seq,[]).append(payload)
            elif kind==2: self.handler(frame)
    def request(self,service,message,payload=b'',timeout=None):
        self.sequence=(self.sequence+1)&0xffffffff; seq=self.sequence
        self.pending=(service,message,seq)
        try:
            self.send(service,message,payload,sequence=seq)
            deadline=time.monotonic()+(timeout or self.timeout)
            while time.monotonic()<deadline:
                self.pump()
                for response in self.replies.pop(seq,[]):
                    if len(response)<2: raise Failure('protocol_decode','Short response status',22)
                    status=struct.unpack_from('<H',response)[0]
                    if status==1: continue
                    if status: raise Failure('device_rejected',f'Device status 0x{status:04x}',23,device_status=status)
                    return response[2:]
            raise Failure('response_timeout','No final matching response; operation may have executed',24,execution='unknown')
        finally: self.pending=None; self.replies.pop(seq,None)
    def wait(self,seconds):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline: self.pump()
    def __exit__(self,*args):
        primary=args[0] is not None
        try:
            if self.port: self.port.close()
            self.result.data['final_state']['usb_released']=True
        finally:
            if self.lock: self.lock.__exit__(*args); self.lock=None
            try: self.rec.close()
            except Failure as exc:
                if primary: self.result.data.setdefault('errors',[]).append(exc.record())
                else: raise
