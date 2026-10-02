import json,struct,sys
from elftools.elf.elffile import ELFFile
elf_path,dump_path,out_path=sys.argv[1:]
data=open(dump_path,'rb').read()
f=open(elf_path,'rb');dw=ELFFile(f).get_dwarf_info()
def attr(d,n,default=None):
 a=d.attributes.get(n);return a.value if a else default
def base(d):
 while d.tag in ('DW_TAG_typedef','DW_TAG_const_type','DW_TAG_volatile_type','DW_TAG_restrict_type'):
  d=d.get_DIE_from_attribute('DW_AT_type')
 return d
def size(d):
 d=base(d)
 if d.tag=='DW_TAG_array_type':return size(d.get_DIE_from_attribute('DW_AT_type'))*count(d)
 return attr(d,'DW_AT_byte_size')
def count(d):
 n=1
 for c in d.iter_children():
  if c.tag=='DW_TAG_subrange_type':n*=attr(c,'DW_AT_count',attr(c,'DW_AT_upper_bound',-1)+1)
 return n
def decode(d,offset):
 d=base(d);n=size(d)
 if d.tag in ('DW_TAG_structure_type','DW_TAG_class_type'):
  return {attr(c,'DW_AT_name').decode():decode(c.get_DIE_from_attribute('DW_AT_type'),offset+attr(c,'DW_AT_data_member_location',0)) for c in d.iter_children() if c.tag=='DW_TAG_member'}
 if d.tag=='DW_TAG_array_type':
  t=d.get_DIE_from_attribute('DW_AT_type');return [decode(t,offset+i*size(t)) for i in range(count(d))]
 b=data[offset:offset+n]
 if d.tag=='DW_TAG_base_type' and attr(d,'DW_AT_encoding')==4:return struct.unpack('<f' if n==4 else '<d',b)[0]
 return int.from_bytes(b,'little',signed=attr(d,'DW_AT_encoding') in (5,6))
for cu in dw.iter_CUs():
 for d in cu.iter_DIEs():
  if d.tag=='DW_TAG_variable' and attr(d,'DW_AT_name')==b'board_test':
   t=d.get_DIE_from_attribute('DW_AT_type');assert len(data)==size(t),(len(data),size(t))
   result=decode(t,0);open(out_path,'w').write(json.dumps(result,indent=2)+'\n');sys.exit(0)
raise SystemExit('board_test not found')
