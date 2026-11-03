#!/usr/bin/env python3
"""Capture a bounded loot sample from an already running, verified radar.

Never starts/stops/reinstalls either app. Evidence stays under ignored local/.
"""
import argparse
import json
import re
import shlex
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--limit', type=int, default=16)
    parser.add_argument('--watch', nargs='*', default=[])
    parser.add_argument('--registry-filter', help='Independently inspect registered loot by case-insensitive class fragment (up to 32 nearby and 16 unresolved transforms)')
    parser.add_argument('--radius', type=float, default=15, help='Registry search radius in metres, 0 < radius <= 1000')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[a-zA-Z0-9_-]{1,64}', args.label):
        parser.error('label must use letters, digits, underscore or hyphen')
    if not 1 <= args.limit <= 64 or len(args.watch) > args.limit:
        parser.error('limit must be 1..64 and accommodate all watched addresses')
    if any(not re.fullmatch(r'[0-9a-fA-F]{8,16}', a) for a in args.watch):
        parser.error('watch addresses must be hexadecimal')
    if args.registry_filter is not None and not re.fullmatch(r'[A-Za-z0-9_]{1,64}', args.registry_filter):
        parser.error('registry filter must be a 1..64-character class-name fragment')
    if not 0 < args.radius <= 1000:
        parser.error('radius must be finite and in (0, 1000]')
    if args.registry_filter and args.watch:
        parser.error('registry mode does not accept watched addresses')
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'evidence/local'):
        parser.error('output must be inside ignored evidence/local')
    output.mkdir(parents=True, exist_ok=True)
    dest = output / (args.label + '.json')
    if dest.exists():
        parser.error('sample label already exists; choose a new label')
    adb = [args.adb, '-s', args.serial]
    manifest = json.loads((ROOT / 'scanner/build/loot-probe-deployment.json').read_text())
    probe = manifest['path']
    if not re.fullmatch(r'/data/local/tmp/delta-loot-probe-[0-9a-f]{16}', probe):
        parser.error('invalid diagnostic deployment path')
    frame = json.loads(subprocess.check_output(adb + [
        'exec-out', 'run-as', 'com.deltaradar.app', 'cat',
        'files/latest-scanner-frame.json'], timeout=8))
    pins = dict(line.split('=', 1) for line in
                (ROOT / 'scanner/deployment.properties').read_text().splitlines()
                if '=' in line and not line.startswith('#'))
    if not frame.get('valid') or frame.get('verifiedScannerSha256') != pins['sha256']:
        raise RuntimeError('no valid feed from the pinned scanner')
    age_ms = time.time() * 1000 - frame['captureWallTimeMs']
    if not -2000 <= age_ms <= 3000:
        raise RuntimeError('feed is stale or phone/host clocks differ')
    raw = json.loads(frame['rawLine'])
    me = next(e for e in raw['entities'] if e['id'] == raw['self'])
    loot = sorted((e for e in raw['entities'] if e['kind'] == 'loot'),
                  key=lambda e: sum((e[k] - me[k]) ** 2 for k in ('x', 'y', 'z')))
    current_addresses = {e['id'].removeprefix('ent_') for e in loot}
    if any(address not in current_addresses for address in args.watch):
        raise RuntimeError('watched actor is absent from the current loot feed; rediscover it')
    targets = list(dict.fromkeys(args.watch))
    for entity in loot:
        address = entity['id'].removeprefix('ent_')
        if not re.fullmatch(r'[0-9a-fA-F]{8,16}', address):
            raise RuntimeError('invalid actor address in feed')
        if address not in targets and len(targets) < args.limit:
            targets.append(address)
    if not targets and not args.registry_filter:
        raise RuntimeError('no loot in the current feed')
    started = time.time()
    command = ([probe, 'nearby-loot', *(str(me[k]) for k in ('x', 'y', 'z')),
                str(args.radius), args.registry_filter, me['id'].removeprefix('ent_')] if args.registry_filter else
               [probe, 'actors', *targets])
    if args.registry_filter and not re.fullmatch(r'[0-9a-fA-F]{8,16}', command[-1]):
        raise RuntimeError('invalid current self actor; registry mode requires a fresh match anchor')
    result = subprocess.run(adb + ['shell', '-T', 'su -c ' + shlex.quote(
        shlex.join(command))], capture_output=True, timeout=12)
    lines = result.stdout.decode().splitlines()
    records = [json.loads(line) for line in lines if line.startswith('{')]
    if args.registry_filter:
        targets = [record['actor'] for record in records]
    sample = dict(label=args.label, hostStarted=started, hostFinished=time.time(),
                  probe=manifest, exitCode=result.returncode, targets=targets,
                  mode='registry' if args.registry_filter else 'feed',
                  registryFilter=args.registry_filter, registryRadius=args.radius,
                  records=records, diagnostics=[line for line in lines
                                               if not line.startswith('{')],
                  stderr=result.stderr.decode(errors='replace'), frame=frame)
    dest.write_text(json.dumps(sample, indent=2) + '\n')
    print(json.dumps(dict(path=str(dest), exitCode=result.returncode,
                          lootInFeed=len(loot), inspected=len(records),
                          itemIds=sum(bool(e.get('itemNameId', '').isdigit()) for e in records),
                          containersWithItems=sum(e.get('contentsCount', 0) > 0 for e in records))))
    if args.registry_filter and not any(line.startswith('nearby-complete ') for line in lines):
        raise RuntimeError('registry pass did not complete; do not infer absence from partial output')
    if args.registry_filter and not any(line.startswith('nearby-complete ') and 'worldFiltered=true' in line for line in lines):
        raise RuntimeError('registry world was not verified from the current self actor')
    if args.registry_filter and not any(line.startswith('nearby-complete ') and 'truncated=false' in line for line in lines):
        raise RuntimeError('registry was truncated; do not infer absence from this sample')
    if result.returncode:
        raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
