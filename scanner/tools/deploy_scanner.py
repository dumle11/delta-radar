#!/usr/bin/env python3
"""Deploy only a verified build at its content-addressed path. Never start a scan."""
import argparse, hashlib, json, re, shlex, subprocess, uuid
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--serial', required=True)
    p.add_argument('--manifest', type=Path, required=True)
    p.add_argument('--evidence', type=Path, required=True)
    args=p.parse_args()
    m=json.loads(args.manifest.read_text());binary=Path(m['binary'])
    expected=m['binarySha256'];target=m['deploymentPath']
    assert m['targetApi']==31 and re.fullmatch(r'[a-f0-9]{64}',expected)
    assert target=='/data/local/tmp/delta-radar/scanner-api31-'+expected[:16]
    assert hashlib.sha256(binary.read_bytes()).hexdigest()==expected
    def adb(*cmd):
        r=subprocess.run(['adb','-s',args.serial,*cmd],capture_output=True,text=True,timeout=20)
        if r.returncode: raise RuntimeError(r.stderr or r.stdout)
        return r.stdout.strip()
    def root(script): return adb('shell','su -c '+shlex.quote(script))
    api=int(adb('shell','getprop','ro.build.version.sdk'))
    if api<31: raise RuntimeError('Device is older than this API-31 build')
    old=root('sha256sum /data/local/tmp/memscan').split()[0]
    stage='/data/local/tmp/delta-radar-stage-'+uuid.uuid4().hex
    adb('push',str(binary),stage)
    q=shlex.quote
    try:
        script=f'''set -e
mkdir -p /data/local/tmp/delta-radar
chmod 0755 /data/local/tmp/delta-radar
if [ ! -e {q(target)} ]; then
  cp {q(stage)} {q(stage)}.verified
  chmod 0555 {q(stage)}.verified
  chown 0:0 {q(stage)}.verified
  mv -n {q(stage)}.verified {q(target)}
fi
sha256sum {q(target)}
'''
        observed=root(script).split()[0]
        assert observed==expected,(observed,expected)
        identity=json.loads(root(q(target)+' --identity'))
        assert identity['project']=='delta-radar' and identity['api']==31
        assert identity['revision']==m['baseRevision'] and identity['sourceSha256']==m['sourceSha256']
        assert root('sha256sum /data/local/tmp/memscan').split()[0]==old
        proof={'targetApi':31,'deviceApi':api,'deploymentPath':target,'binarySha256':observed,
               'identity':identity,'preexistingScannerSha256':old,'preexistingScannerPreserved':True,
               'scanStarted':False}
        args.evidence.mkdir(parents=True,exist_ok=True)
        (args.evidence/'deployment.json').write_text(json.dumps(proof,indent=2)+'\n')
        print(json.dumps(proof,indent=2))
    finally: root('rm -f '+q(stage)+' '+q(stage+'.verified'))

if __name__=='__main__':main()
