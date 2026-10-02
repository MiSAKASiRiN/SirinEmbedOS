#!/usr/bin/env python3
"""Explicit board test: read-only telemetry subscriptions, CDC read stall and DTR reconnect."""
import argparse
import json
from pathlib import Path
import struct
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/'Tools/HostLink'))
from system_monitor import Client,open_port
from imu_telemetry import PERIODS
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--port',required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
phase='normal';records=[];meta={'complete':False,'port':a.port,'phases':[]}
port=None;client=None
with (a.output/'records.jsonl').open('x') as log:
 def report(r):
  r['phase']=phase;records.append(r);log.write(json.dumps(r)+'\n');log.flush()
 def read_for(seconds):
  until=time.monotonic()+seconds
  while time.monotonic()<until:client.receive()
 def mark(name):
  global phase
  phase=name;meta['phases'].append({'phase':name,'monotonic_ns':time.monotonic_ns()});print(name,flush=True)
 try:
  port=open_port(a.port);client=Client(port,report);client.request(0,1);client.subscribe(True)
  # Increase outbound load only; this never commands CAN or actuators.
  for stream,period in PERIODS.items():client.request(4,1,struct.pack('<HIB',stream,period,True))
  mark('normal');read_for(3)
  mark('read_stalled');time.sleep(12)
  mark('draining');read_for(5)
  meta['first_decode_errors']=dict(client.decoder.errors)
  mark('dtr_down');port.dtr=False;time.sleep(5);port.close();port=None
  mark('reconnected');port=open_port(a.port);client=Client(port,report);client.request(0,1);client.subscribe(True);read_for(5)
  meta['reconnected_decode_errors']=dict(client.decoder.errors)
  meta['reconnected_reports']=dict(client.counts)
  meta['complete']=all(client.counts[k] for k in ('tasks','adc','cycles')) and not client.decoder.errors and not meta['first_decode_errors']
 finally:
  if client and port:
   try:
    client.subscribe(False)
    for stream,period in PERIODS.items():client.request(4,1,struct.pack('<HIB',stream,period,False))
   except Exception as e:meta['cleanup_error']=str(e)
  if port:port.close()
  (a.output/'metadata.json').write_text(json.dumps(meta,indent=2)+'\n')
sys.exit(0 if meta['complete'] else 1)
