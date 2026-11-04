"""SDK SOURCE V2 and Parameters wire contracts; no application policy."""
import math
import struct
from .core import Failure
FORMATS={1:'f',2:'i',3:'I',4:'B'}
class Reader:
    def __init__(self,data): self.data=data; self.at=0
    def take(self,n):
        if n<0 or self.at+n>len(self.data): raise Failure('protocol_decode','Truncated protocol record',22)
        b=self.data[self.at:self.at+n]; self.at+=n; return b
    def unpack(self,fmt): return struct.unpack('<'+fmt,self.take(struct.calcsize('<'+fmt)))
    def end(self):
        if self.at!=len(self.data): raise Failure('protocol_decode','Unexpected trailing bytes',22)

def scalar(r,typ,size=None):
    if typ not in FORMATS: raise Failure('protocol_decode','Unknown scalar type',22)
    fmt=FORMATS[typ]; expected=struct.calcsize(fmt)
    if size is not None and size!=expected: raise Failure('protocol_decode','Wrong scalar width',22)
    value=r.unpack(fmt)[0]
    if typ==4:
        if value not in (0,1): raise Failure('protocol_decode','Invalid bool',22)
        return bool(value)
    return value

def catalog(client,parameters=False,stable=False):
    cursor=0; seen=set(); items=[]; identity=None; total_expected=None
    service,message=(2,8 if stable else 1) if parameters else (4,0x10)
    while cursor!=65535:
        if cursor in seen or len(seen)>1024: raise Failure('protocol_decode','Cyclic catalog pagination',22)
        seen.add(cursor)
        r=Reader(client.request(service,message,struct.pack('<HB',cursor,4)))
        h,total,next_cursor,count=r.unpack('IHHB')
        if identity is not None and (identity!=h or total_expected!=total): raise Failure('catalog_changed','Directory changed during pagination',22)
        identity=h; total_expected=total
        for _ in range(count):
            if stable:
                index,sid=r.unpack('HI'); item=dict(index=index,stable_id=sid)
            elif parameters:
                index,typ,registered,size,n=r.unpack('HBBBB')
                name=r.take(n).decode('utf-8'); default=scalar(r,typ,size); minimum=scalar(r,typ,size); maximum=scalar(r,typ,size)
                item=dict(index=index,type=typ,size=size,registered=bool(registered),name=name,default=default,minimum=minimum,maximum=maximum)
            else:
                index,typ,size,max_hz,current_hz,version,n,u=r.unpack('HBBHHBBB')
                item=dict(index=index,type=typ,size=size,max_hz=max_hz,current_hz=current_hz,subscription_version=version,
                          name=r.take(n).decode('utf-8'),unit=r.take(u).decode('utf-8'))
            if any(x['index']==index for x in items): raise Failure('protocol_decode','Duplicate directory index',22)
            items.append(item)
        r.end(); cursor=next_cursor
    if len(items)!=total_expected: raise Failure('protocol_decode','Catalog item count mismatch',22)
    return identity,items

def source_sample(payload):
    r=Reader(payload); h,stamp,count,reserved=r.unpack('IQB3s')
    if count>16 or reserved!=b'\0'*3: raise Failure('protocol_decode','SOURCE header',22)
    items=[]
    for _ in range(count):
        index,typ,valid,seq,sample_time=r.unpack('HBBIQ')
        if valid not in (0,1): raise Failure('protocol_decode','SOURCE valid flag',22)
        items.append(dict(index=index,type=typ,valid=bool(valid),sequence=seq,sample_time_us=str(sample_time),value=scalar(r,typ)))
    r.end(); return dict(catalog_hash=h,encode_time_us=str(stamp),sources=items)

def parameters(client,args):
    action=args.action
    if action in ('persist','restore','state'):
        message={'persist':4,'restore':5,'state':6}[action]
        body=client.request(2,message,timeout=args.timeout)
        if action!='state':
            if len(body)!=7 or body[-1]!=0: raise Failure('parameter_result','Device parameter completion status failed',23,payload_hex=body.hex())
        return dict(action=action,payload_hex=body.hex(),execution='completed')
    h,items=catalog(client,True)
    sh,sids=catalog(client,True,True)
    if h!=sh: raise Failure('catalog_changed','Stable IDs and schema hashes differ',22)
    mapping={i['index']:i['stable_id'] for i in sids}
    for i in items: i['stable_id']=mapping.get(i['index'])
    if action=='list': return dict(layout_hash=h,parameters=items)
    chosen=[i for i in items if i['stable_id']==args.stable_id]
    if len(chosen)!=1: raise Failure('parameter_not_found','Stable ID is not in current schema',23)
    item=chosen[0]; index=item['index']
    if action=='read':
        r=Reader(client.request(2,2,struct.pack('<BH',1,index)))
        rh,count=r.unpack('IB'); ri,typ,size=r.unpack('HBB')
        if (rh,count,ri,typ,size)!=(h,1,index,item['type'],item['size']): raise Failure('protocol_decode','Parameter reply identity mismatch',22)
        value=scalar(r,typ,size); r.end(); return dict(**item,value=value)
    typ=item['type']
    if typ==4:
        if str(args.value).lower() not in ('true','false','0','1'): raise Failure('invalid_value','Bool must be true/false/0/1')
        value=str(args.value).lower() in ('true','1')
    else: value=float(args.value) if typ==1 else int(args.value,0)
    if not math.isfinite(value) or not item['minimum']<=value<=item['maximum']:
        raise Failure('out_of_range','Value is outside device parameter range')
    payload=struct.pack('<IBHBB',h,1,index,typ,item['size'])+struct.pack('<'+FORMATS[typ],value)
    body=client.request(2,3,payload)
    if len(body)!=8 or struct.unpack_from('<I',body)[0]!=h or body[4]!=1 or body[5]!=255 or body[6]!=0 or body[7]!=0:
        raise Failure('parameter_result','Parameter WRITE failed',23,payload_hex=body.hex())
    return dict(**item,value=value,execution='completed',persisted=False)
