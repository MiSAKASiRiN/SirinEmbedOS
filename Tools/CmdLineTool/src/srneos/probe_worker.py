"""Isolated SEGGER DLL worker. Only invoked by an explicit CLI operation."""
import base64
import json
import sys
from .core import Failure, sha
from .images import FLASH, SECTOR

def execute(request, link_factory=None):
    state={'jlink_probe':'not_checked','target':'not_checked'}
    final=dict(target_run_state='not_checked',probe_released=None,usb_released=None)
    response=dict(states=state,final_state=final,steps=[],reads=[])
    link=None; connected=False; opened=False; halted_by_us=False; before=None
    def step(name,**kw): response['steps'].append(dict(name=name,status='ok',details=kw))
    try:
        if link_factory is None:
            import pylink
            library=pylink.library.Library(dllpath=request['library']) if request.get('library') else None
            link=pylink.JLink(lib=library,log=lambda s:print(s,file=sys.stderr),
                detailed_log=lambda s:None,error=lambda s:print(s,file=sys.stderr),
                warn=lambda s:print(s,file=sys.stderr),unsecure_hook=lambda *a:pylink.enums.JLinkFlags.DLG_BUTTON_NO)
        else: link=link_factory()
        for cmd in ('SuppressGUI = 1','DisableAutoUpdateFW','InhibitConnectRetries = 1'):
            if link.exec_command(cmd)<0: raise Failure('unsupported_policy',f'J-Link rejected {cmd}',13)
        candidates=[int(p.SerialNumber) for p in link.connected_emulators()]
        step('probe_list',serials=candidates)
        selected=request.get('probe')
        if selected is None:
            if len(candidates)>1: raise Failure('ambiguous_device','Specify --probe',14)
            selected=candidates[0] if candidates else None
        if selected not in candidates:
            state['jlink_probe']='offline'
            raise Failure('probe_offline','Selected J-Link not found',11)
        state['jlink_probe']='enumerated'
        try: link.open(serial_no=selected); opened=True
        except Exception as exc:
            state['jlink_probe']='unknown'
            raise Failure('probe_unavailable',str(exc),14) from exc
        state['jlink_probe']='online'
        step('probe_open',serial=selected,library_version=str(link.version))
        if request['operation']=='connect' and not request.get('target'): return response
        link.set_tif(1) # SEGGER SWD
        try:
            link.connect(request.get('device','STM32H723VG'),speed=request.get('speed',1000))
            if not link.target_connected(): raise RuntimeError('Target connection not established')
        except Exception as exc:
            state['target']='offline'
            raise Failure('target_offline',str(exc),12) from exc
        connected=True; state['target']='online'
        before='halted' if link.halted() else 'running'
        step('target_initial_state',state=before)
        chip=link.memory_read32(0x5C001000,1)[0]&0xfff
        if chip!=0x483:
            state['target']='mismatch'
            raise Failure('target_mismatch',f'Expected H723 family DEV_ID 0x483, got {chip:#x}',13)
        uid=''.join(f'{v:08X}' for v in link.memory_read32(0x1FF1E800,3))
        size=link.memory_read16(0x1FF1E880,1)[0]
        if size!=1024:
            state['target']='mismatch'
            raise Failure('target_mismatch','Expected 1024 KiB Flash',13,flash_kib=size)
        if request.get('expected_uid') and request['expected_uid'].replace(' ','').upper()!=uid:
            state['target']='mismatch'
            raise Failure('target_mismatch','Target UID does not match expectation',13)
        step('identity',device_id=chip,uid=uid,flash_kib=size)
        if request['operation']=='connect': return response
        if request.get('halt') or request['operation']=='flash':
            if not link.halt() or not link.halted(): raise Failure('halt_failed','Could not halt target',13)
            halted_by_us=before=='running'
        def read(a,n,width=1):
            out=bytearray()
            while len(out)<n:
                count=min(16384,n-len(out))
                words=getattr(link,'memory_read'+str(width*8))(a+len(out),count//width)
                part=b''.join(v.to_bytes(width,'little') for v in words)
                if len(part)!=count: raise Failure('short_read','Incomplete target memory read',24)
                out.extend(part)
            return bytes(out)
        if request['operation']=='flash':
            segments=[(s['address'],base64.b64decode(s['data'])) for s in request['segments']]
            sectors={}
            for a,b in segments:
                for start in range(FLASH+((a-FLASH)//SECTOR)*SECTOR,a+len(b),SECTOR):
                    sectors.setdefault(start,None)
            policy=request.get('erase_policy','reject')
            coverage={s:bytearray(SECTOR) for s in sectors}
            for a,b in segments:
                for start in sectors:
                    lo=max(start,a); hi=min(start+SECTOR,a+len(b))
                    if lo<hi: coverage[start][lo-start:hi-start]=b'\x01'*(hi-lo)
            if policy=='reject' and any(0 in c for c in coverage.values()):
                raise Failure('partial_sector','Image does not cover whole erase sectors; choose --erase-policy preserve or allow',13)
            originals={s:read(s,SECTOR) for s in sectors}
            if request.get('backup'):
                from pathlib import Path
                from .core import save_new
                for s,b in originals.items(): save_new(Path(request['backup'])/f'{s:08x}.bin',b)
                step('backup',directory=request['backup'],sectors=len(originals))
            for s in sectors:
                sectors[s]=bytearray(originals[s] if policy=='preserve' else b'\xff'*SECTOR)
            for a,b in segments:
                for s,buf in sectors.items():
                    lo=max(s,a); hi=min(s+SECTOR,a+len(b))
                    if lo<hi: buf[lo-s:hi-s]=b[lo-a:hi-a]
            for s,b in sorted(sectors.items()):
                try: link.flash(bytes(b),s)
                except Exception as exc: raise Failure('flash_failed',str(exc),32,may_be_partial=True) from exc
            # Verify all affected sectors after all programming has completed.
            for s,b in sorted(sectors.items()):
                actual=read(s,len(b))
                if actual!=bytes(b):
                    raise Failure('verify_mismatch','Independent sector readback mismatch',31,address=hex(s),expected=sha(b),actual=sha(actual))
                step('flash_sector_verified',address=hex(s),bytes=len(b),sha256=sha(actual))
            # reset only after every sector independently verified
            link.reset(halt=True)
            if request['after']=='run': link.restart()
            observed='halted' if link.halted() else 'running'
            step('after_flash',requested=request['after'],observed=observed)
            if observed!=('running' if request['after']=='run' else 'halted'):
                raise Failure('after_state_mismatch','Requested final execution state was not observed',24)
        else:
            for item in request['reads']:
                import struct
                from .images import TYPES
                width=min(4,struct.calcsize(TYPES[item['type']]))
                b=read(item['address'],item['length'],width)
                response['reads'].append(dict(**item,data=base64.b64encode(b).decode(),sha256=sha(b)))
                step('read',address=hex(item['address']),bytes=len(b),sha256=sha(b),
                     consistency='halted' if request.get('halt') or before=='halted' else 'live_non_atomic')
    except Failure as exc:
        response['error']=exc.record(); response['exit_code']=exc.exit_code
    except Exception as exc:
        response['error']=Failure('jlink_error',str(exc),10 if link is None else 24).record()
        response['exit_code']=10 if link is None else 24
    finally:
        if link and opened:
            try:
                if connected:
                    if halted_by_us and request['operation']!='flash': link.restart()
                    step('before_disconnect',state='halted' if link.halted() else 'running')
                    final['target_run_state']='unknown' # DLL close can affect execution; no post-close observation
                link.close(); final['probe_released']=True
            except Exception as exc:
                final['probe_released']=False
                response.setdefault('error',Failure('cleanup_failed',str(exc),24).record())
                response.setdefault('exit_code',24)
    return response

if __name__=='__main__':
    request=json.load(sys.stdin)
    print(json.dumps(execute(request)),flush=True)
