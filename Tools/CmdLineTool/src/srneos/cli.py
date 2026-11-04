"""srneos public entry point."""
import argparse
import contextlib
import importlib.util
import json
import math
import os
from pathlib import Path
import signal
import struct
import sys
import time
import tomllib
from . import __version__
from .core import Failure, Result, dumps, save_new, Lock

DEFAULTS=dict(config=None,json=False,jsonl=False,output=None,save=None,timeout=10.,dry_run=False,
    probe=None,device='STM32H723VG',speed=1000,jlink_library=None,expected_uid=None,target=False,
    cdc0=None,cdc1=None,usb_serial=None,handshake=False,list=False,image=None,address=None,length=None,
    type='u32',count=1,halt=False,allow_mmio=False,request_file=None,script=None,expected_sha256=None,
    after=None,erase_policy='reject',backup=None,duration=10.,hz=50,streams='',sources='',require_data=False,
    command_id=None,name=None,payload_hex=None,payload_file=None,channel=None,generation=1,deadline_us=50000,
    stable_id=None,value=None,enable=False,vx=0.,vy=0.,wz=0.,values='0,0,0,0,0',qualification_ms=100,
    stop_settle_ms=100,manifest=None,suite_root=None,suite=None,fail_fast=False,plan=None,run_id=None)

def integer(s): return int(s,0)
def number(s):
    value=float(s)
    if not math.isfinite(value): raise argparse.ArgumentTypeError('Value must be finite')
    return value

class Parser(argparse.ArgumentParser):
    def error(self,message): raise Failure('invalid_arguments',message)

def parser():
    common=Parser(add_help=False,argument_default=argparse.SUPPRESS)
    for flag in ('json','jsonl','dry-run','target','halt','allow-mmio','handshake','list','require-data','enable','fail-fast'):
        common.add_argument('--'+flag,action='store_true')
    for flag in ('config','output','save','jlink-library','expected-uid','cdc0','cdc1','usb-serial','image','request-file','script',
                 'expected-sha256','backup','streams','sources','name','payload-hex','payload-file','value','values',
                 'manifest','suite-root','plan','run-id'):
        common.add_argument('--'+flag)
    for flag in ('probe','speed','address','length','count','channel','generation','deadline-us','stable-id','qualification-ms','stop-settle-ms'):
        common.add_argument('--'+flag,type=integer)
    common.add_argument('--id',dest='command_id',type=integer)
    for flag in ('timeout','duration','vx','vy','wz'): common.add_argument('--'+flag,type=number)
    common.add_argument('--hz',type=int)
    common.add_argument('--suite',action='append')
    common.add_argument('--device',choices=['STM32H723VG'])
    common.add_argument('--after',choices=['run','halt'])
    common.add_argument('--erase-policy',choices=['reject','preserve','allow'])
    common.add_argument('--type',choices=['u8','u16','u32','u64','i8','i16','i32','i64','f32','f64','bytes'])
    p=Parser(prog='srneos',description='SirinEmbedOS host tools',parents=[common],argument_default=argparse.SUPPRESS)
    p.add_argument('--version',action='version',version=__version__)
    sub=p.add_subparsers(dest='command',required=True,parser_class=Parser)
    def add(parent,name): return parent.add_parser(name,parents=[common],argument_default=argparse.SUPPRESS)
    for name in ('doctor','capabilities','flash','verify','read','log','run','cancel'): add(sub,name)
    for name,actions in [('connect',['jlink','usb']),('telemetry',['list','read','capture']),
                         ('command',['exec','control']),('parameters',['list','read','write','persist','restore','state']),
                         ('rmt',['enable','move','channels'])]:
        node=add(sub,name); children=node.add_subparsers(dest='action',required=True,parser_class=Parser)
        for action in actions: add(children,action)
    test=add(sub,'test').add_subparsers(dest='test_kind',required=True,parser_class=Parser)
    host=add(test,'host').add_subparsers(dest='action',required=True,parser_class=Parser)
    for name in ('list','run'): add(host,name)
    for name in ('report','decode'):
        node=add(sub,name); node.add_argument('path')
    return p

CONFIG_MAP={
 'tool':{'request_timeout_seconds':'timeout','output_root':None},
 'jlink':{'library':'jlink_library','probe_serial':'probe','device':'device','speed_khz':'speed','expected_uid':'expected_uid'},
 'usb':{'serial':'usb_serial','cdc0':'cdc0','cdc1':'cdc1'},
 'firmware':{'image':'image','after':'after','erase_policy':'erase_policy'},
 'rmt':{'hz':'hz','qualification_ms':'qualification_ms','stop_settle_ms':'stop_settle_ms','max_duration_seconds':None,
        'max_abs_vx_m_s':None,'max_abs_vy_m_s':None,'max_abs_wz_rad_s':None},
 'host_tests':{'suite_root':'suite_root'}}

def configure(args):
    supplied=vars(args).copy(); config={}; resolved={}
    if supplied.get('config'):
        path=Path(args.config).resolve(); config=tomllib.loads(path.read_text())
        if config.get('schema_version')!=1: raise Failure('config_version','Configuration requires schema_version=1')
        if set(config)-set(CONFIG_MAP)-{'schema_version','commands'}: raise Failure('unknown_config','Unknown top-level config section')
        for section,mapping in CONFIG_MAP.items():
            fields=config.get(section,{})
            if not isinstance(fields,dict) or set(fields)-set(mapping): raise Failure('unknown_config',f'Unknown fields in [{section}]')
            for key,value in fields.items():
                dest=mapping[key]
                if dest:
                    if dest in ('image','suite_root','jlink_library'):
                        value=str((path.parent/value).resolve())
                    if dest=='probe': value=int(value)
                    resolved[dest]=value
        if 'output_root' in config.get('tool',{}): config['tool']['output_root']=str((path.parent/config['tool']['output_root']).resolve())
    for k,v in {**DEFAULTS,**resolved,**supplied}.items(): setattr(args,k,v)
    if args.device!='STM32H723VG': raise Failure('unsupported_target','Only STM32H723VG is supported')
    if args.after not in (None,'run','halt') or args.erase_policy not in ('reject','preserve','allow'):
        raise Failure('invalid_flash_policy','Invalid after/erase_policy configuration')
    if args.probe is not None and (not isinstance(args.probe,int) or args.probe<=0): raise Failure('invalid_probe','Probe serial must be a positive integer')
    if args.expected_uid:
        uid=args.expected_uid.replace(' ','')
        if len(uid)!=24 or any(c not in '0123456789abcdefABCDEF' for c in uid): raise Failure('invalid_uid','Expected three 8-digit UID words')
    if not isinstance(args.timeout,(int,float)) or not math.isfinite(args.timeout) or not 0<args.timeout<=86400: raise Failure('invalid_timeout','timeout must be finite and in (0,86400]')
    if not isinstance(args.hz,int) or not 1<=args.hz<=1000: raise Failure('invalid_rate','hz must be integer 1..1000')
    if args.json and args.jsonl: raise Failure('invalid_output','Choose --json or --jsonl')
    if not isinstance(args.duration,(int,float)) or not math.isfinite(args.duration) or not 0<args.duration<=86400: raise Failure('invalid_duration','duration must be finite and in (0,86400]')
    if not isinstance(args.speed,int) or not 5<=args.speed<=12000: raise Failure('invalid_speed','SWD speed must be 5..12000 kHz')
    for file in (args.save,args.script):
        if file and (Path(file).exists() or not Path(file).parent.is_dir()): raise Failure('save_unavailable',f'Output exists or parent missing: {file}',30)
    if args.save and args.script and Path(args.save).resolve()==Path(args.script).resolve(): raise Failure('save_conflict','--save and --script must differ')
    if args.expected_sha256 and (len(args.expected_sha256)!=64 or any(c not in '0123456789abcdefABCDEF' for c in args.expected_sha256)): raise Failure('invalid_hash','Expected 64 hexadecimal SHA256 digits')
    if args.save and args.command not in ('verify','read','log','telemetry','command','parameters'):
        raise Failure('invalid_output','This command uses --output, not --save')
    if args.script and args.command!='read': raise Failure('invalid_output','--script is only valid for read')
    if args.backup and args.command!='flash': raise Failure('invalid_output','--backup is only valid for flash')
    if args.image and args.length is not None: raise Failure('invalid_range','--image and --length are mutually exclusive')
    if args.request_file and args.address is not None: raise Failure('invalid_range','--request-file and --address are mutually exclusive')
    if args.payload_file and args.payload_hex is not None: raise Failure('invalid_payload','Choose --payload-file or --payload-hex')
    return config

def payload(args,config):
    data=Path(args.payload_file).read_bytes() if args.payload_file else bytes.fromhex(args.payload_hex or '')
    if args.action=='exec':
        cid=args.command_id
        if args.name:
            if cid is not None: raise Failure('command_conflict','Choose --id or --name')
            matches=[c for c in config.get('commands',[]) if c.get('name')==args.name]
            if len(matches)!=1: raise Failure('command_not_found','Command name is not uniquely configured')
            spec=matches[0]; cid=spec['id']
            if spec.get('encoding')!='bytes' or len(data)>spec.get('max_request_bytes',254): raise Failure('command_payload','Unsupported encoding or payload length')
        if cid is None or not 0<=cid<=65535 or len(data)>254: raise Failure('command_payload','EXEC requires u16 ID and <=254 argument bytes')
        return 1,1,struct.pack('<H',cid)+data,0
    if args.channel is None or not 0<=args.channel<=65535 or not 0<=args.generation<=0xffffffff or not 0<=args.deadline_us<=0xffffffff:
        raise Failure('command_payload','CONTROL requires u16 channel and u32 generation/deadline')
    body=struct.pack('<HII',args.channel,args.generation,args.deadline_us)+data
    if len(body)>480: raise Failure('command_payload','CONTROL payload exceeds 480 bytes')
    return 3,1,body,2

def log(args,result):
    import serial
    from .usb import select_port
    if args.dry_run: result.step('log_plan',duration=args.duration); return
    path=select_port(args,result,0); data=bytearray(); total=0
    raw=None
    if result.output: raw=open(result.output/'cdc0.log.partial','xb')
    result.data['states']['log_protocol']='not_applicable'; result.data['evidence_level']='hardware'
    try:
        with Lock('serial:'+os.path.realpath(path)):
            with serial.Serial(path,115200,timeout=0.1,**({'exclusive':True} if os.name!='nt' else {})) as port:
                result.data['states']['cdc0']='open'; port.dtr=True
                deadline=time.monotonic()+args.duration
                while time.monotonic()<deadline:
                    b=port.read(max(1,min(port.in_waiting,4096))); total+=len(b)
                    if raw: raw.write(b)
                    if len(data)<65536: data.extend(b[:65536-len(data)])
                    if b and args.jsonl: print(dumps(dict(schema_version=1,type='sample',run_id=result.data['run_id'],text=b.decode('utf-8',errors='replace'))),flush=True)
    except serial.SerialException as exc:
        text=str(exc).lower()
        state='permission_denied' if 'permission' in text else 'busy' if 'busy' in text or 'exclusive' in text else 'disconnected'
        result.data['states']['cdc0']=state
        result.data['states']['log_state']='disconnected'
        raise Failure('cdc_'+state,str(exc),14 if state!='disconnected' else 24) from exc
    finally:
        result.data['final_state']['usb_released']=True
        if raw: raw.flush(); os.fsync(raw.fileno()); raw.close()
    result.data['states']['log_state']='receiving' if total else 'quiet'
    result.step('log',bytes=total,preview=data.decode('utf-8',errors='replace'),truncated=total>len(data))
    if result.output:
        source=result.output/'cdc0.log.partial'; target=result.output/'cdc0.log'
        os.link(source,target); source.unlink()
        from .core import file_artifact
        result.data['artifacts'].append(file_artifact(target))
        if args.save: result.export_file(args.save,target)
    if args.require_data and not total: raise Failure('log_quiet','No log bytes within observation window',40)

def dispatch(args,result,config):
    command=args.command
    if command=='capabilities':
        result.step('capabilities',commands=['doctor','connect','flash','verify','read','log','telemetry','command','parameters','rmt','test host','run','cancel','report','decode'],
            protocol='SirinLink V2 / SirinRMT v1',target='STM32H723VG',source_protocol='V2',
            limitations=['No automatic actuator execution acknowledgement','No arbitrary J-Link scripts','No automatic unlock/reconnect',
                         'Windows and Linux hardware behavior not bench validated','No motion feedback assertions in run plans'])
    elif command=='doctor':
        checks={name:importlib.util.find_spec(name) is not None for name in ('serial','pylink','elftools')}
        result.step('dependencies',**checks,python=sys.version,platform=sys.platform,
            jlink_library=args.jlink_library,hardware_access=False)
        if not all(checks.values()): raise Failure('tool_missing','Host dependency missing',10)
    elif command in ('flash','verify','read') or command=='connect' and args.action=='jlink':
        from .probe import run
        run(args,result)
    elif command=='test':
        from .host_tests import run
        run(args,result)
    elif command=='report':
        path=Path(args.path)
        value=json.loads((path/'result.json' if path.is_dir() else path).read_text())
        result.step('report',saved_result=value)
    elif command=='decode':
        from ._legacy.imu_telemetry import Decoder
        decoder=Decoder(); frames=0; preview=[]
        with open(args.path,'rb') as f:
            for chunk in iter(lambda:f.read(4096),b''):
                for kind,service,message,sequence,body in decoder.feed(chunk):
                    frames+=1
                    if len(preview)<32: preview.append(dict(kind=kind,service=service,message=message,sequence=sequence,payload_hex=body.hex()))
        result.step('decode',frames=frames,errors=dict(decoder.errors),preview=preview,truncated=frames>32)
        if decoder.errors or decoder.frame: raise Failure('protocol_decode','Malformed or incomplete capture',22)
    elif command=='cancel':
        from .runs import cancel
        if not args.run_id: raise Failure('run_id_required','cancel requires --run-id')
        if not args.dry_run: cancel(args.run_id)
        result.step('cancel_requested',run_id=args.run_id,stop='not_yet_confirmed')
    elif command=='log': log(args,result)
    else:
        from .usb import Session, select_port, inventory
        from .runs import Control
        prepared=None; motion_values=None
        if command=='command': prepared=payload(args,config)
        elif command=='parameters' and args.action in ('read','write'):
            if args.stable_id is None or not 0<=args.stable_id<=0xffffffff: raise Failure('stable_id_required','Parameter operation requires u32 --stable-id')
            if args.action=='write' and args.value is None: raise Failure('value_required','Parameter write requires --value')
        elif command=='rmt':
            from .rmt import validate
            motion_values=validate(args,config.get('rmt',{}))
        elif command=='run':
            from .runs import load_plan
            if not args.plan: raise Failure('plan_required','run requires --plan')
            prepared=load_plan(args,config)
        elif command=='telemetry' and args.action!='list':
            from .telemetry import STREAMS
            if not args.streams and not args.sources: args.streams='system'
            if any(s not in STREAMS for s in args.streams.split(',') if s): raise Failure('unknown_stream','Unknown stream')
            if args.sources and (args.hz%5 or args.hz<5): raise Failure('invalid_rate','SOURCE rate requires multiples of 5 Hz')
        if args.dry_run:
            result.step('usb_plan',operation=command,action=getattr(args,'action',None),
                        payload_hex=prepared[2].hex() if command=='command' else None,motion=motion_values)
            return
        if command=='connect' and args.list:
            result.step('usb_inventory',**inventory()); return
        path=select_port(args,result,1)
        if command=='connect' and not args.handshake:
            result.step('usb_selected',cdc1=path,handshake='not_requested'); return
        with Control(result.data['run_id']) as control:
            print('srneos run_id='+result.data['run_id'],file=sys.stderr)
            with Session(path,result,args.timeout) as client:
                client.cancel_check=control.check
                if command=='connect': return
                if command=='telemetry':
                    from .telemetry import capture,STREAMS
                    from .protocols import catalog
                    if args.action=='list':
                        h,sources=catalog(client)
                        listing=dict(catalog_hash=h,sources=sources,known_streams={k:dict(id=v,support='unknown') for k,v in STREAMS.items()})
                        result.step('telemetry_catalog',**listing)
                        result.export(args.save,dumps(listing).encode())
                    else: capture(client,args,result)
                elif command=='command':
                    s,m,body,kind=prepared
                    if kind:
                        client.send(s,m,body,kind=kind); result.step('command',execution='sent',validation='not_evaluated')
                        result.export(args.save,body)
                    else:
                        response=client.request(s,m,body); result.step('command',execution='completed',payload_hex=response.hex())
                        result.export(args.save,response)
                elif command=='parameters':
                    from .protocols import parameters
                    value=parameters(client,args); result.step('parameters',**value); result.export(args.save,dumps(value).encode())
                elif command=='rmt':
                    from .rmt import run
                    run(client,args,result,config.get('rmt',{}))
                elif command=='run':
                    from .runs import execute
                    execute(client,args,result,config,*prepared)
        if command=='telemetry' and args.action!='list' and args.save and result.output:
            source=result.output/'samples.jsonl'
            if source.exists(): result.export_file(args.save,source)

def main(argv=None):
    args=None; result=Result('parse'); config={}
    previous_signal=None
    try:
        args=parser().parse_args(argv); result.data['command']=args.command
        config=configure(args)
        side_effect=args.command in ('flash','rmt','run') or args.command=='command' or args.command=='parameters' and args.action in ('write','persist','restore')
        need_output=side_effect or args.command=='telemetry' and args.action=='capture' or args.command=='test' and args.action=='run' or args.save and args.command in ('telemetry','log')
        output=args.output
        if need_output and not output and not args.dry_run:
            output=str(Path(config.get('tool',{}).get('output_root','build/srneos'))/result.data['run_id'])
        if output: result.directory(output)
        if result.output:
            result.save('invocation.json',dumps(dict(arguments=vars(args),configuration=config,
                python=sys.version,platform=sys.platform,config_file=str(Path(args.config).resolve()) if args.config else None)).encode())
        if args.dry_run: result.data.update(status='planned',complete=False,evidence_level='none')
        previous_signal=signal.getsignal(signal.SIGTERM)
        signal.signal(signal.SIGTERM,lambda *a:(_ for _ in ()).throw(KeyboardInterrupt()))
        dispatch(args,result,config)
    except KeyboardInterrupt:
        result.data.update(status='interrupted',exit_code=130,complete=False)
    except Failure as exc: result.fail(exc)
    except (ValueError,KeyError,TypeError,struct.error) as exc: result.fail(Failure('invalid_data',str(exc),2))
    except OSError as exc: result.fail(Failure('host_io_error',str(exc),30))
    except Exception as exc: result.fail(Failure('internal_error',str(exc),50))
    finally:
        if previous_signal is not None: signal.signal(signal.SIGTERM,previous_signal)
    try: result.finish()
    except Failure as exc: result.fail(exc)
    if args and getattr(args,'jsonl',False): print(dumps(dict(schema_version=1,type='result',run_id=result.data['run_id'],result=result.data)))
    elif args and getattr(args,'json',False) or not args: print(dumps(result.data))
    else:
        print(f"{result.data['status'].upper()}: {result.data['command']} (run {result.data['run_id']})")
        for step in result.data['steps']: print(f"{step['name']}: {dumps(step['details'])}")
        if result.data['error']: print(dumps(result.data['error']),file=sys.stderr)
        if result.output: print('Evidence: '+str(result.output))
    return result.data['exit_code']
