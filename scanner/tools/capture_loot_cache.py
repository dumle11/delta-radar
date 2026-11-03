#!/usr/bin/env python3
"""Read bounded loot-UI caches without invoking Lua/game functions or changing apps.

Live mode requires a fresh pinned radar frame. --lobby is a preparation check,
explicitly not evidence of match contents. All output stays in ignored local/.
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
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lobby', action='store_true')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', args.label):
        parser.error('invalid label')
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'evidence/local'):
        parser.error('output must be inside ignored evidence/local')
    output.mkdir(parents=True, exist_ok=True)
    dest = output / (args.label + '.json')
    if dest.exists():
        parser.error('label already exists')
    manifest = json.loads((ROOT / 'scanner/build/loot-probe-deployment.json').read_text())
    probe = manifest['path']
    if not re.fullmatch(r'/data/local/tmp/delta-loot-probe-[0-9a-f]{16}', probe):
        parser.error('invalid diagnostic path')
    adb = [args.adb, '-s', args.serial]
    sample = dict(label=args.label, hostStarted=time.time(), probe=manifest,
                  liveFrameVerified=False, lobbyCheck=args.lobby, steps=[], caches={}, complete=False)
    started = time.monotonic()
    observed = {}

    def run(label, *command):
        if len(sample['steps']) >= 64 or time.monotonic() - started > 20:
            raise RuntimeError('bounded cache capture budget exhausted')
        result = subprocess.run(adb + ['shell', '-T', 'su -c ' + shlex.quote(
            shlex.join([probe, *command]))], capture_output=True, timeout=10)
        text = result.stdout.decode(errors='replace')
        records = [json.loads(line) for line in text.splitlines() if line.startswith('{')]
        step = dict(label=label, exitCode=result.returncode, stdout=text,
                    stderr=result.stderr.decode(errors='replace'), records=records)
        sample['steps'].append(step)
        return step

    def get(table, key, label):
        result = run(label, 'lua-get', table, key)
        return next((r for r in result['records'] if r.get('key') == key), None)

    def table_field(table, key, label):
        value = get(table, key, label)
        return value['value'] if value and value.get('tag') == 69 else None

    def snapshot(table, label, child_limit):
        if table in observed:
            return dict(reference=observed[table])
        observed[table] = label
        result = run(label, 'lua-table', table)
        if result['exitCode']:
            return dict(readable=False, step=label)
        records = result['records']
        children = [r for r in records if r.get('tag') == 69 and
                    r.get('key') not in ('_meta_', '__index', '_parentclass', 'Class', 'Super')]
        entry = dict(readable=True, step=label, sampledEntries=len(records),
                     entryLimitReached=len(records) >= 1024, children={},
                     childLimitReached=len(children) > child_limit)
        for index, child in enumerate(children[:child_limit]):
            entry['children'][child['key']] = snapshot(child['value'], f'{label}/child{index}', 0)
        return entry

    try:
        remote_sha = subprocess.check_output(adb + ['shell', 'sha256sum', probe],
                                             text=True, timeout=8).split()[0]
        if remote_sha != manifest['sha256']:
            raise RuntimeError('diagnostic hash mismatch')
        if not args.lobby:
            frame = json.loads(subprocess.check_output(adb + [
                'exec-out', 'run-as', 'com.deltaradar.app', 'cat',
                'files/latest-scanner-frame.json'], timeout=8))
            pins = dict(line.split('=', 1) for line in
                        (ROOT / 'scanner/deployment.properties').read_text().splitlines()
                        if '=' in line and not line.startswith('#'))
            if not frame.get('valid') or frame.get('verifiedScannerSha256') != pins['sha256']:
                raise RuntimeError('no valid pinned live frame')
            if not -2000 <= time.time() * 1000 - frame['captureWallTimeMs'] <= 3000:
                raise RuntimeError('stale live frame or clock mismatch')
            sample['frame'] = frame
            sample['liveFrameVerified'] = True
        roots = run('fresh-roots', 'lua-price-metadata')
        globals_found = re.findall(r'lua-registry offset=\w+ globals=([0-9a-f]+)', roots['stdout'])
        if roots['exitCode'] or len(globals_found) != 1:
            raise RuntimeError('fresh Lua globals were not verified')
        globals_table = globals_found[0]
        server = table_field(globals_table, 'Server', 'Server')
        if not server:
            raise RuntimeError('Server root unavailable')
        loot = table_field(server, 'LootingServer', 'LootingServer')
        inventory = table_field(server, 'InventoryServer', 'InventoryServer')
        groups = table_field(globals_table, 'ESlotGroup', 'ESlotGroup')
        if not loot or not inventory or not groups:
            raise RuntimeError('loot/inventory roots unavailable')
        nearby = get(groups, 'Nearby', 'Nearby-enum')
        if not nearby or nearby.get('tag') != 19 or not 0 <= int(nearby['value'], 16) <= 128:
            raise RuntimeError('Nearby group enum was not verified')
        group_key = '#' + str(int(nearby['value'], 16))
        sample['nearbyGroup'] = int(nearby['value'], 16)
        for key in ('_cachedData', '_slotItemMap', '_nearbyPickupMapping', '_fetchScope'):
            table = table_field(loot, key, 'LootingServer/' + key)
            sample['caches']['LootingServer.' + key] = (
                snapshot(table, 'LootingServer.' + key, 4) if table else dict(available=False))
        for key in ('_mapGid2Items', '_allItemSlots'):
            table = table_field(inventory, key, 'InventoryServer/' + key)
            group = table_field(table, group_key, 'InventoryServer/' + key + '/Nearby') if table else None
            sample['caches']['InventoryServer.' + key + '.Nearby'] = (
                snapshot(group, 'InventoryServer.' + key + '.Nearby', 12) if group else dict(available=False))
        sample['complete'] = True
    except Exception as error:
        sample['error'] = f'{type(error).__name__}: {error}'
    finally:
        sample['hostFinished'] = time.time()
        sample['elapsedSeconds'] = round(time.monotonic() - started, 3)
        dest.write_text(json.dumps(sample, indent=2) + '\n')
        print(json.dumps(dict(path=str(dest), complete=sample['complete'],
                              liveFrameVerified=sample['liveFrameVerified'],
                              elapsedSeconds=sample['elapsedSeconds'], steps=len(sample['steps']),
                              cacheStates={k: {f: v[f] for f in ('readable', 'available', 'sampledEntries',
                                           'childLimitReached') if f in v}
                                           for k, v in sample['caches'].items()},
                              error=sample.get('error'))))
    if not sample['complete']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
