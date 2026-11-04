import base64
import json
from pathlib import Path
import struct
import sys
from .core import Failure, Lock, process, dumps, sha
from .images import image, manifest, bounds, TYPES, FLASH, SECTOR

def run(args,result):
    op=args.command if args.command!='connect' else 'connect'
    request=dict(operation=op,probe=args.probe,device=args.device,speed=args.speed,
                 library=args.jlink_library,expected_uid=args.expected_uid,target=args.target if op=='connect' else True,
                 halt=args.halt,reads=[])
    segments=None
    if op in ('flash','verify') and args.image:
        segments=image(args.image,args.address)
        result.step('image',segments=manifest(segments))
        request['segments']=[dict(address=a,data=base64.b64encode(b).decode()) for a,b in segments]
        request['reads']=[dict(address=a,length=len(b),type='bytes') for a,b in segments]
    elif op=='verify':
        if args.address is None or args.length is None: raise Failure('range_required','verify needs --image or --address/--length')
        bounds(args.address,args.length,flash=True)
        request['reads']=[dict(address=args.address,length=args.length,type='bytes')]
    elif op=='read':
        specs=[]
        if args.request_file:
            import tomllib
            doc=tomllib.loads(Path(args.request_file).read_text())
            if doc.get('schema_version')!=1: raise Failure('config_version','Read list requires schema_version=1')
            specs=doc.get('reads',[])
            if not isinstance(specs,list) or not 1<=len(specs)<=64: raise Failure('invalid_reads','Read list requires 1..64 items')
        else:
            if args.address is None: raise Failure('address_required','read needs --address or --request-file')
            specs=[dict(address=args.address,type=args.type,count=args.count)]
        for s in specs:
            if not isinstance(s,dict) or set(s)-{'name','address','type','count'}: raise Failure('invalid_reads','Unknown read item field')
            typ=s.get('type','u32'); address=s['address']; address=int(address,0) if isinstance(address,str) else address
            if typ not in TYPES: raise Failure('invalid_type',str(typ))
            size=struct.calcsize('<'+TYPES[typ]); count=s.get('count',1)
            if not isinstance(count,int) or count<=0: raise Failure('invalid_count','count must be positive')
            bounds(address,size*count,allow_mmio=args.allow_mmio)
            if address%size: raise Failure('unaligned_address','Read address must align to type width')
            request['reads'].append(dict(name=s.get('name',hex(address)),address=address,length=size*count,type=typ))
        if sum(item['length'] for item in request['reads'])>16*1024*1024: raise Failure('invalid_reads','Total read list exceeds 16 MiB')
        result.step('read_plan',reads=request['reads'])
        if args.script:
            lines=[]
            for s in request['reads']:
                width=min(4,struct.calcsize(TYPES[s['type']]))
                lines.append(f'mem{width*8} 0x{s["address"]:08X}, {s["length"]//width}')
            result.export(args.script,('exitonerror 1\nexec SuppressGUI = 1\nexec DisableAutoUpdateFW\nexec InhibitConnectRetries = 1\nconnect\n'+ '\n'.join(lines)+'\nexit\n').encode())
    if op=='flash':
        if not segments: raise Failure('image_required','flash requires --image')
        if not args.after: raise Failure('after_required','flash requires explicit --after run|halt')
        if args.erase_policy=='reject':
            coverage={}
            for address,data in segments:
                for sector in range(FLASH+((address-FLASH)//SECTOR)*SECTOR,address+len(data),SECTOR):
                    lo=max(sector,address); hi=min(sector+SECTOR,address+len(data))
                    coverage[sector]=coverage.get(sector,0)+hi-lo
            if any(n!=SECTOR for n in coverage.values()):
                raise Failure('partial_sector','Image leaves erase-sector bytes undefined; choose --erase-policy preserve or allow',13)
        request.update(after=args.after,erase_policy=args.erase_policy,backup=None)
        if args.backup:
            backup=Path(args.backup).resolve()
            if backup.exists(): raise Failure('save_exists','Backup directory already exists',30)
            if not args.dry_run: backup.mkdir(parents=True)
            request['backup']=str(backup)
    if args.save and len(request['reads'])!=1: raise Failure('save_multisegment','--save requires exactly one contiguous read; use --output')
    if args.dry_run:
        result.step('jlink_plan',request={k:v for k,v in request.items() if k!='segments'})
        return
    result.data['evidence_level']='hardware'
    with Lock('jlink:'+str(args.probe or 'auto')):
        try:
            code,out,err=process([sys.executable,'-m','srneos.probe_worker'],args.timeout,input_data=dumps(request).encode())
        except Failure:
            result.data['final_state']['target_run_state']='unknown'
            raise
    result.save('jlink.log',err)
    if code: raise Failure('jlink_worker_failed',err.decode(errors='replace'),24)
    try: reply=json.loads(out)
    except ValueError as exc: raise Failure('jlink_worker_invalid','Invalid worker result',24) from exc
    result.data['states'].update(reply['states']); result.data['steps'].extend(reply['steps'])
    result.data['final_state'].update(reply['final_state'])
    result.save('jlink-result.json',out)
    if reply.get('error'):
        e=reply['error']; raise Failure(e['code'],e['message'],reply['exit_code'],**e['details'])
    for i,item in enumerate(reply['reads']):
        data=base64.b64decode(item.pop('data'))
        result.save(f'read-{i:03d}-{item["address"]:08x}.bin',data)
        if op=='read':
            fmt='<'+TYPES[item['type']]; size=struct.calcsize(fmt)
            values=[struct.unpack_from(fmt,data,i)[0] for i in range(0,len(data),size)]
            result.step('values',address=hex(item['address']),type=item['type'],values=values[:256],truncated=len(values)>256)
            exported=dumps(dict(**item,values=values,raw_hex=data.hex())).encode()
            if args.save and Path(args.save).suffix=='.bin': exported=data
        else: exported=data
        result.export(args.save,exported)
        expected=segments[i][1] if segments else None
        if expected is not None and data!=expected:
            raise Failure('verify_mismatch','Flash bytes differ from image',31,address=hex(item['address']),expected=sha(expected),actual=sha(data))
        if args.expected_sha256 and sha(data)!=args.expected_sha256.lower():
            raise Failure('verify_mismatch','Flash SHA256 differs',31,actual=sha(data))
    if op=='verify': result.step('comparison',comparison='matched' if segments or args.expected_sha256 else 'not_requested')
