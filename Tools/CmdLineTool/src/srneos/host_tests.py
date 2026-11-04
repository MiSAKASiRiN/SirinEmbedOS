"""Explicit SDK/private host-suite runner. No recursive script execution."""
import os
from pathlib import Path
import sys
import tomllib
from .core import Failure, process

def load(args):
    if args.manifest:
        path=Path(args.manifest).resolve(); doc=tomllib.loads(path.read_text())
        if doc.get('schema_version')!=1: raise Failure('config_version','Test manifest requires schema_version=1')
        specs=doc.get('tests',[]); base=path.parent
    else:
        if not args.suite_root: raise Failure('suite_unavailable','Provide --suite-root or --manifest; private SDK tests are not packaged',41)
        base=Path(args.suite_root).resolve()
        if not base.is_dir(): raise Failure('suite_unavailable',f'Suite root missing: {base}',41)
        names=args.suite or []
        if args.action=='list':
            return [dict(id=str(p.parent.relative_to(base)),argv=['bash',str(p)],cwd=str(base.parent),timeout_seconds=args.timeout) for p in sorted(base.rglob('run_host_tests.sh'))]
        if not names: raise Failure('no_tests','Explicit --suite names or --manifest required',41)
        specs=[]
        for name in names:
            path=(base/name/'run_host_tests.sh').resolve()
            if not path.is_relative_to(base) or not path.is_file(): raise Failure('suite_unavailable',f'Suite missing: {name}',41)
            specs.append(dict(id=name,argv=['bash',str(path)],cwd=str(base.parent),timeout_seconds=args.timeout))
    if not specs: raise Failure('no_tests','No test cases selected',41)
    seen=set(); output=[]
    for s in specs:
        if not isinstance(s,dict) or set(s)-{'id','argv','cwd','timeout_seconds','env'}: raise Failure('invalid_manifest','Unknown test fields')
        name=s.get('id'); argv=s.get('argv')
        if not isinstance(name,str) or not name or name in seen: raise Failure('invalid_manifest','Test IDs must be unique strings')
        seen.add(name)
        if not isinstance(argv,list) or not argv or any(not isinstance(v,str) for v in argv): raise Failure('invalid_manifest','argv must be nonempty string array')
        cwd=(base/s.get('cwd','.')).resolve(); timeout=s.get('timeout_seconds',args.timeout)
        if not cwd.is_dir() or not isinstance(timeout,(int,float)) or not 0<timeout<=86400: raise Failure('invalid_manifest','Invalid cwd or timeout')
        env=s.get('env',{})
        if not isinstance(env,dict) or any(not isinstance(k,str) or not isinstance(v,str) for k,v in env.items()): raise Failure('invalid_manifest','env must be string mapping')
        output.append(dict(id=name,argv=argv,cwd=str(cwd),timeout_seconds=timeout,env=env))
    return output

def run(args,result):
    specs=load(args)
    if args.action=='list' or args.dry_run:
        result.step('host_test_plan',tests=specs); return
    failed=[]; result.data['evidence_level']='host'
    for i,s in enumerate(specs):
        try:
            code,out,err=process(s['argv'],s['timeout_seconds'],cwd=s['cwd'],env={**os.environ,**s.get('env',{})})
            result.save(f'test-{i:03d}.stdout.log',out); result.save(f'test-{i:03d}.stderr.log',err)
            result.step(s['id'],'ok' if code==0 else 'failed',exit_code=code,argv=s['argv'])
            if code: failed.append(s['id'])
        except Failure as exc:
            result.step(s['id'],'failed',error=exc.record()); failed.append(s['id'])
        if failed and args.fail_fast:
            for remaining in specs[i+1:]: result.step(remaining['id'],'not_run')
            break
    if failed: raise Failure('host_tests_failed','One or more requested host tests failed',40,failed=failed)
