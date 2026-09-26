#!/usr/bin/env python3
"""One-time source snapshot import; prototype is strictly read-only."""
from pathlib import Path
import re, shutil, hashlib, json, subprocess, configparser
SDK=Path(__file__).resolve().parents[2]
PROTO=Path('/Users/sirin/CLionProjects/h723vg-v2-prototype')
MAP={
'SirinOS':'Core','bsp_bmi088':'Devices/IMU/BMI088','bsp_buzzer':'Devices/Buzzer',
'bsp_dr16':'Services/RemoteControl/DR16','bsp_dwt':'Diagnostics/Time','bsp_eth_lan8720a':None,
'bsp_fdcan':'IO/CAN/Native','bsp_i2c':'IO/I2C','bsp_spi':'IO/SPI',
'bsp_srn_log':'Services/Logging','bsp_vt03':'Services/RemoteControl/VT03','bsp_ws2812':'Devices/WS2812',
'lib_24c02':'Devices/EEPROM','lib_adp_dji_c610':'Devices/Motors/C610','lib_adp_dji_c620':'Devices/Motors/C620',
'lib_adp_dji_gm6020':'Devices/Motors/GM6020','lib_adp_dm_4310':'Devices/Motors/DM4310',
'lib_adp_dm_4340':'Devices/Motors/DM4340','lib_adp_lk_mg8016e':'Devices/Motors/LK8016E',
'lib_adp_navision':'Devices/NaviVision','lib_adp_supcap':'Devices/Supercapacitor',
'lib_algos':'Algorithms/Math','lib_controls':'Algorithms/Control','lib_debug_watch':'Diagnostics/Watch',
'lib_generic_ui':'Services/Referee/UI','lib_imu':'Devices/IMU','lib_mcp2518fd':'Devices/MCP2518FD',
'lib_modbus_rtu':'IO/RS485/Modbus','lib_mpu_dcache':'Core/Memory','lib_params':'Services/Parameters',
'lib_power_control':'Algorithms/Power','lib_referee':'Services/Referee','lib_remote_control':'Services/RemoteControl',
'lib_rs485_lc_tl42r':'Devices/TL42R','lib_serial_servo_h12h':'Devices/H12H','lib_serial_servo_hd':None,
'lib_srn_cobs':'Services/HostLink/Codec','lib_swerve':None,'lib_uart_router':None,
'tskptt_exfdcan_router':'IO/CAN/External','tskptt_host_comm':'Services/HostLink',
'tskptt_hskp':'Diagnostics/Runtime','tskptt_imu':'Devices/IMU/Worker','tskptt_log':'Services/Logging/Worker',
'tskptt_ntfdcan_router':'IO/CAN/Router','tskptt_param_console':'Services/Parameters/HostLink',
'tskptt_referee':'Services/Referee/Receiver','tskptt_referee_tx':'Services/Referee/Transmitter',
'tskptt_rs485_router':'IO/RS485'}
EXCLUDE={'ui_Main.c','ui_Main.h'}

def git(path,*args):
 r=subprocess.run(['git','-C',str(path),*args],capture_output=True,text=True)
 return r.stdout.strip() if r.returncode==0 else None

def transform(text,headers):
 text=re.sub(r'\bOS_', 'SRNOS_', text)
 text=re.sub(r'\bOS::', 'SRNOS::', text)
 text=re.sub(r'\bnamespace\s+OS\b', 'namespace SRNOS',text)
 def inc(m):
  path=m[2]; key=path if path.startswith('SirinOS/') else path.split('/')[-1]
  return m[1]+headers.get(key,path)+m[3]
 return re.sub(r'(#\s*include\s*[<"])([^">]+)([">])',inc,text)

if __name__=='__main__':
 if (SDK/'Docs/Migration/source-manifest.json').exists(): raise SystemExit('Snapshot already imported; refusing to overwrite edits')
 gm=configparser.ConfigParser();gm.read(PROTO/'.gitmodules')
 urls={gm[s]['path']:gm[s]['url'] for s in gm.sections()}
 moves=[];headers={};records=[]
 for name,cap in MAP.items():
  module=PROTO/'Frameworks'/name
  rec={'module':name,'destination':cap,'action':'import' if cap else 'exclude','commit':git(module,'rev-parse','HEAD'), 'source_url':urls.get('Frameworks/'+name), 'status':git(module,'status','--porcelain'), 'files':[]}
  if name=='lib_uart_router':rec['action']='replace-with-Serial'
  for src in sorted(module.rglob('*')):
   if not src.is_file() or '.git' in src.relative_to(module).parts:continue
   rel=src.relative_to(module); dest=None
   if cap and src.name not in EXCLUDE:
    if rel.parts[0]=='Inc':
     tail=Path(*rel.parts[1:]);tail=Path(*tail.parts[1:]) if name=='SirinOS' else tail
     private=src.stem.lower().endswith('internal')
     dest=Path('Source')/cap/tail if private else Path('Include/SirinEmbedOS')/cap/tail
     key='SirinOS/'+tail.as_posix() if name=='SirinOS' else src.name
     include=src.name if private else str(dest.relative_to('Include'))
     if key in headers and headers[key]!=include:raise RuntimeError(('duplicate header',key))
     headers[key]=include
    elif rel.parts[0]=='Src':dest=Path('Source')/cap/Path(*rel.parts[1:])
    elif rel.parts[0]=='Tests':dest=Path('Tests')/cap/Path(*rel.parts[1:])
    elif rel.parts[0]=='Tools':dest=Path('Tools')/cap/Path(*rel.parts[1:])
    elif name=='SirinOS' and rel.parts[0]=='Ports':
     if src.suffix in ('.c','.h','.ld'):
      dest=Path('Board/Glue')/src.name if src.suffix!='.ld' else Path('Board/Linker')/src.name
      if src.suffix=='.h': headers['SirinOS/'+src.name]=src.name
    elif name=='lib_mpu_dcache' and rel.parts[0]=='Lnk':dest=Path('Board/Linker')/src.name
    elif src.name.lower().startswith(('license','copying')):dest=Path('Docs/Migration/Licenses')/name/src.name
   sha=hashlib.sha256(src.read_bytes()).hexdigest()
   rec['files'].append({'source':str(src.relative_to(PROTO)),'sha256':sha,'destination':str(dest) if dest else None})
   if dest:moves.append((src,SDK/dest))
  records.append(rec)
 for src,dst in moves:
  if dst.exists():raise RuntimeError(('destination exists',dst))
  dst.parent.mkdir(parents=True,exist_ok=True)
  try: data=transform(src.read_text(),headers);dst.write_text(data)
  except UnicodeDecodeError:shutil.copy2(src,dst)
  if src.stat().st_mode & 0o111:dst.chmod(dst.stat().st_mode|0o111)
 for p in [SDK/'Include',SDK/'Source',SDK/'Config',SDK/'Examples',SDK/'Tests',SDK/'Board/Glue']:
  for f in p.rglob('*'):
   if f.is_file() and f.suffix in ('.c','.cpp','.h','.hpp','.cmake','.py','.s','.S'):
    f.write_text(transform(f.read_text(),headers))
 # USER CODE call only; preserve generated file line endings.
 p=SDK/'Board/Core/Src/main.c';p.write_bytes(p.read_bytes().replace(b'OS_BoardStartKernel',b'SRNOS_BoardStartKernel'))
 out=SDK/'Docs/Migration';out.mkdir(parents=True,exist_ok=True)
 (out/'source-manifest.json').write_text(json.dumps({'prototype':str(PROTO),'prototype_commit':git(PROTO,'rev-parse','HEAD'),'prototype_status':git(PROTO,'status','--porcelain'),'modules':records},ensure_ascii=False,indent=2)+'\n')
 (out/'header-map.json').write_text(json.dumps(headers,indent=2)+'\n')
 print(f'Imported {len(moves)} files across {len(records)} module records')
