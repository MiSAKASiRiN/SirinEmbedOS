"""H723 Flash image geometry; no device access."""
from pathlib import Path
from .core import Failure, sha
FLASH=0x08000000
FLASH_END=0x08100000
SECTOR=128*1024
TYPES={'u8':'B','i8':'b','u16':'H','i16':'h','u32':'I','i32':'i','u64':'Q','i64':'q','f32':'f','f64':'d','bytes':'B'}

def bounds(address,length,flash=False,allow_mmio=False):
    if not isinstance(address,int) or not isinstance(length,int) or length<=0 or length>16*1024*1024 or address<0 or address+length>2**32:
        raise Failure('invalid_range','Address/length invalid or exceeds 16 MiB read limit')
    ranges=[(FLASH,FLASH_END)]
    if not flash:
        ranges += [(0x00000000,0x00010000),(0x20000000,0x20020000),(0x24000000,0x24020000),
                   (0x30000000,0x30008000),(0x38000000,0x38004000),(0x1FF1E800,0x1FF1E80C)]
    if not any(lo<=address and address+length<=hi for lo,hi in ranges) and not (allow_mmio and not flash):
        raise Failure('range_not_allowed','Range outside supported Flash/RAM/UID; MMIO requires --allow-mmio')

def image(path,address=None):
    path=Path(path)
    if not path.is_file(): raise Failure('image_missing',f'Image not found: {path}')
    raw=path.read_bytes(); segments=[]
    if path.suffix.lower() in ('.elf','.axf'):
        from elftools.elf.elffile import ELFFile
        with path.open('rb') as f:
            elf=ELFFile(f)
            if elf['e_machine']!='EM_ARM': raise Failure('image_architecture','Expected ARM ELF')
            for seg in elf.iter_segments():
                if seg['p_type']=='PT_LOAD' and seg['p_filesz']:
                    segments.append((int(seg['p_paddr']),seg.data()))
    elif path.suffix.lower()=='.hex':
        memory={}; base=0; ended=False
        for line in raw.decode('ascii').splitlines():
            if not line.strip(): continue
            if ended or not line.startswith(':'): raise Failure('invalid_hex','Invalid HEX framing')
            rec=bytes.fromhex(line[1:]); n=rec[0]
            if len(rec)!=n+5 or sum(rec)&255: raise Failure('invalid_hex','HEX checksum/length')
            off=int.from_bytes(rec[1:3],'big'); kind=rec[3]; payload=rec[4:-1]
            if kind==0:
                for i,b in enumerate(payload):
                    a=base+off+i
                    if a in memory: raise Failure('overlapping_image','HEX overlapping bytes')
                    memory[a]=b
            elif kind==1 and n==0: ended=True
            elif kind in (2,4) and n==2: base=int.from_bytes(payload,'big')<<(4 if kind==2 else 16)
            elif kind in (3,5) and n==4: pass
            else: raise Failure('invalid_hex','Unsupported HEX record')
        if not ended: raise Failure('invalid_hex','HEX missing EOF')
        for a in sorted(memory):
            if not segments or a!=segments[-1][0]+len(segments[-1][1]): segments.append((a,bytearray()))
            segments[-1][1].append(memory[a])
    else:
        if address is None: raise Failure('address_required','BIN requires --address')
        segments=[(address,raw)]
    if not segments: raise Failure('empty_image','No loadable bytes')
    segments=sorted((a,bytes(b)) for a,b in segments)
    last=0
    for a,b in segments:
        bounds(a,len(b),flash=True)
        if a<last: raise Failure('overlapping_image','Overlapping load segments')
        last=a+len(b)
    return segments

def manifest(segments):
    return [dict(address=hex(a),length=len(b),sha256=sha(b)) for a,b in segments]
