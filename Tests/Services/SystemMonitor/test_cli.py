from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[3]/"Tools/HostLink"))
import system_monitor as monitor
from imu_telemetry import pack
root=Path(sys.argv[1])
for name,stream,size,stamp_at in (("adc",2,128,16),("tasks",1,112,76),("cycles",3,112,100)):
    data=(root/f"{name}.bin").read_bytes()
    stamp,=struct.unpack_from("<Q",data,stamp_at)
    payload=struct.pack("<HIIHBB",stream,5,stamp&0xffffffff,0,len(data)//size,size)+data
    report=monitor.decode(payload)
    assert report["stream"]==name
    text=monitor.human(report)
    if name=="cycles":
        first=report["records"][0]
        assert first["jitter_min_us"]==-17 and first["jitter_max_us"]==22 and first["window_us"]==1000017
        assert "jitter=-17..22" in text and first["age_us"]==50
    else:
        assert "未测量" in text if name=="tasks" else "无效" in text
    assert all(r.get("stack_free_words") is None for r in report["records"])
    for invalid in (payload[:-1],payload[:14]+b"\xff"+payload[15:],payload[:10]+b"\x01\x00"+payload[12:]):
        try:monitor.decode(invalid)
        except ValueError:pass
        else:raise AssertionError("invalid packet accepted")
    class Port:
        def __init__(self):self.data=pack(4,2,99,payload,kind=2)
        @property
        def in_waiting(self):return len(self.data)
        def read(self,n):v=self.data[:n];self.data=self.data[n:];return v
    results=[];client=monitor.Client(Port(),results.append);client.receive()
    assert len(results)==1 and client.counts[name]==1 and not client.decoder.errors
print("PASS: C/Python wire agreement, validation, display and framed CDC receive")

client=monitor.Client(None,lambda _:None)
requests=[]
def old_request(service,message,payload):
    stream,period,enabled=struct.unpack("<HIB",payload)
    requests.append((stream,enabled))
    if stream==3:raise monitor.DeviceStatus(0x0109)
client.request=old_request
client.subscribe(True)
assert client.active_streams=={1,2} and client.supported_streams=={1,2}
client.subscribe(False)
assert not client.active_streams and requests[-2:]==[(1,0),(2,0)]
print("PASS: cycle signed fields/window/age and optional stream compatibility")

event=dict(report["records"][0],flags=5)
display=monitor.human(dict(stream="cycles",records=[event]))
assert "late_max=不适用" in display and "miss=不适用" in display and "skip=不适用" in display
