#!/usr/bin/env python3
"""Build a pinned ARM64/API-31 scanner; never deploy or run it automatically."""
import argparse, hashlib, json, os, shlex, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def git(*args): return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--ndk', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'scanner/build/api31')
    parser.add_argument('--pin-app', action='store_true', help='Update the reviewed app deployment properties')
    args = parser.parse_args()
    ndk = args.ndk.resolve()
    compiler = ndk/'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang'
    if not compiler.is_file(): parser.error('API-31 Linux NDK compiler not found')
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    source = ROOT/'scanner/memscan.c'
    revision, source_hash = git('rev-parse', 'HEAD'), sha(source)
    flags = ['-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror', '-fPIE', '-pie',
             '-fstack-protector-strong', '-D_FORTIFY_SOURCE=2', '-Wl,--build-id=sha1',
             '-Wl,-z,relro,-z,now', '-ffile-prefix-map='+str(ROOT)+'=.',
             '-DDELTA_SOURCE_REVISION="'+revision+'"',
             '-DDELTA_SOURCE_SHA256="'+source_hash+'"']
    binary = output/'delta-radar-scanner-api31'
    command = [str(compiler), *flags, 'scanner/memscan.c', '-lm', '-o', str(binary)]
    subprocess.run(command, cwd=ROOT, check=True, env={**os.environ, 'SOURCE_DATE_EPOCH':'0'})
    binary_hash = sha(binary)
    deployment = '/data/local/tmp/delta-radar/scanner-api31-'+binary_hash[:16]
    manifest = {'baseRevision':revision, 'sourceSha256':source_hash,
                'scannerSourceModified': bool(git('diff', '--', 'scanner/memscan.c')),
                'scannerDiffSha256':hashlib.sha256(subprocess.check_output(['git','-C',str(ROOT),'diff','--','scanner/memscan.c'])).hexdigest(),
                'buildScriptSha256':sha(Path(__file__)), 'target':'aarch64-linux-android31', 'targetApi':31,
                'compilerVersion':subprocess.check_output([str(compiler),'--version'],text=True).splitlines()[0],
                'ndkProperties':(ndk/'source.properties').read_text(), 'command':command,
                'binary':str(binary), 'binarySha256':binary_hash, 'deploymentPath':deployment}
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    (output/'scanner-source.diff').write_text(git('diff','--','scanner/memscan.c')+'\n')
    if args.pin_app:
        (ROOT/'scanner/deployment.properties').write_text(
            '# Reviewed scanner identity; generated binaries remain in ignored scanner/build.\n'
            f'path={deployment}\nsha256={binary_hash}\napi=31\nrevision={revision}\nsourceSha256={source_hash}\n')
    print(json.dumps({'binary':str(binary),'sha256':binary_hash,'deploymentPath':deployment,'manifest':str(output/'manifest.json')},indent=2))

if __name__ == '__main__': main()
