"""Results, durable output, resource ownership and bounded processes."""
import contextlib
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import shutil
import subprocess
import tempfile
import time
import uuid
from . import __version__

class Failure(Exception):
    def __init__(self, code, message, exit_code=2, **details):
        super().__init__(message)
        self.code, self.exit_code, self.details = code, exit_code, details
    def record(self):
        return dict(code=self.code, message=str(self), retryable=False, details=self.details)

def clean(value):
    if isinstance(value, float) and not math.isfinite(value):
        return {"value": None, "class": str(value)}
    if isinstance(value, int) and not isinstance(value, bool) and abs(value) > 2**53-1:
        return str(value)
    if isinstance(value, dict):
        return {str(k): clean(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [clean(v) for v in value]
    if isinstance(value, Path):
        return str(value)
    return value

def dumps(value):
    return json.dumps(clean(value), ensure_ascii=False, allow_nan=False)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def file_artifact(path,kind=None):
    path=Path(path); digest=hashlib.sha256(); size=0
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):
            digest.update(block); size+=len(block)
    return dict(path=str(path.resolve()),kind=kind or path.suffix.lstrip('.') or 'data',
                complete=True,bytes=size,sha256=digest.hexdigest())

def copy_new(source,path):
    path=Path(path); temp=None
    try:
        fd,temp=tempfile.mkstemp(prefix='.'+path.name+'.',suffix='.partial',dir=path.parent)
        with os.fdopen(fd,'wb') as dst, open(source,'rb') as src:
            shutil.copyfileobj(src,dst,1024*1024); dst.flush(); os.fsync(dst.fileno())
        os.link(temp,path); os.unlink(temp)
        return file_artifact(path)
    except OSError as exc:
        raise Failure('save_failed',str(exc),30,partial=temp) from exc

def save_new(path, data):
    """Publish with link(2) no-clobber; retain partial payload on failure."""
    path = Path(path)
    if path.exists():
        raise Failure('save_exists', f'Output already exists: {path}', 30)
    if not path.parent.is_dir():
        raise Failure('save_parent_missing', f'Output parent missing: {path.parent}', 30)
    temp = None
    try:
        fd, temp = tempfile.mkstemp(prefix='.'+path.name+'.', suffix='.partial', dir=path.parent)
        with os.fdopen(fd, 'wb') as file:
            file.write(data); file.flush(); os.fsync(file.fileno())
        os.link(temp, path)  # atomic and refuses existing destination
        os.unlink(temp)
    except OSError as exc:
        raise Failure('save_failed', str(exc), 30, partial=temp) from exc
    return dict(path=str(path.resolve()), kind=path.suffix.lstrip('.') or 'data',
                complete=True, bytes=len(data), sha256=sha(data))

class Result:
    def __init__(self, command):
        self.data = dict(schema_version=1, tool_version=__version__,
            run_id=time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())+'-'+uuid.uuid4().hex[:8],
            command=command, status='ok', exit_code=0, complete=True,
            evidence_level='none', states={}, steps=[], artifacts=[], error=None,
            final_state=dict(target_run_state='not_checked', probe_released=None, usb_released=None))
        self.output = None
    def step(self, name, status='ok', **details):
        self.data['steps'].append(dict(name=name,status=status,details=clean(details)))
    def fail(self, error):
        if self.data['error'] is None:
            self.data.update(status='failed',exit_code=error.exit_code,complete=False,error=error.record())
        else:
            self.data.setdefault('errors',[]).append(error.record())
        self.step(error.code, 'failed', **error.details)
    def directory(self, path):
        self.output = Path(path).resolve()
        try:
            self.output.mkdir(parents=True, exist_ok=False)
        except OSError as exc:
            raise Failure('save_failed',str(exc),30) from exc
        self.save('manifest.json', dumps(dict(schema_version=1,run_id=self.data['run_id'],
                  state='in_progress',tool_version=__version__,started_utc=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()))).encode(), track=False)
    def save(self, name, data, track=True):
        if self.output:
            item=save_new(self.output/name, data)
            if track: self.data['artifacts'].append(item)
    def export(self, path, data):
        if path:
            self.data['artifacts'].append(save_new(path,data))
            self.step('save', save='succeeded', path=str(path))
    def export_file(self,path,source):
        if path:
            self.data['artifacts'].append(copy_new(source,path))
            self.step('save',save='succeeded',path=str(path))
    def finish(self):
        if self.output:
            # Initial manifest remains immutable. Terminal manifest is a separate file.
            self.save('manifest-final.json',dumps(dict(schema_version=1,run_id=self.data['run_id'],
                state=self.data['status'],complete=self.data['complete'],artifacts=self.data['artifacts'])).encode(),track=False)
            self.save('result.json',dumps(self.data).encode(),track=False)

class Lock:
    def __init__(self, key):
        folder=Path(tempfile.gettempdir())/('srneos-locks-'+str(getattr(os,'getuid',lambda:0)()))
        folder.mkdir(mode=0o700,exist_ok=True)
        self.path=folder/(sha(key.encode())+'.lock')
        self.file=None
    def __enter__(self):
        self.file=self.path.open('a+b')
        try:
            if os.name=='nt':
                import msvcrt
                self.file.seek(0); self.file.write(b'0'); self.file.flush(); self.file.seek(0)
                msvcrt.locking(self.file.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(self.file,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except OSError as exc:
            self.file.close()
            raise Failure('resource_busy','Resource is owned by another srneos process',14) from exc
        return self
    def __exit__(self,*args):
        if self.file: self.file.close()  # kernel releases lock even after crashes; never unlink locked inode

def process(argv, timeout, cwd=None, input_data=None, env=None):
    """No shell. Capture output and stop the whole child group on timeout/cancel."""
    try:
        p=subprocess.Popen(argv,cwd=cwd,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,start_new_session=os.name!='nt',
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name=='nt' else 0)
    except OSError as exc:
        raise Failure('tool_missing',str(exc),10) from exc
    try:
        out,err=p.communicate(input_data,timeout=timeout)
    except (subprocess.TimeoutExpired,KeyboardInterrupt):
        if os.name=='nt':
            subprocess.run(['taskkill','/PID',str(p.pid),'/T','/F'],capture_output=True)
        else:
            with contextlib.suppress(ProcessLookupError): os.killpg(p.pid,signal.SIGKILL)
        out,err=p.communicate()
        if isinstance(__import__('sys').exception(),KeyboardInterrupt): raise
        raise Failure('operation_timeout','Child operation timed out; target outcome may be unknown',24,
                      stdout=out.decode(errors='replace'),stderr=err.decode(errors='replace'))
    return p.returncode,out,err
