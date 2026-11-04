#!/usr/bin/env python3
"""Capture the read-only pickup research probe; never deploy or send game input.

Output is private ignored evidence. --lobby explicitly omits match binding.
The diagnostic cannot establish an actionable open-container/UI contract.
"""
import argparse
import json
import re
import shlex
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FRAME_PATH = 'files/latest-scanner-frame.json'
PACKAGE = 'com.deltaradar.app'


def validated_probe(manifest):
    sha = manifest.get('sha256', '')
    if not re.fullmatch(r'[0-9a-f]{64}', sha):
        raise ValueError('invalid diagnostic hash')
    probe = manifest.get('path')
    if probe != '/data/local/tmp/delta-pickup-probe-' + sha[:16]:
        raise ValueError('invalid content-addressed diagnostic path')
    return probe


def capture(adb, manifest, pins, lobby=False, run=subprocess.run, clock=time.time, mode='pickup-context'):
    """The injected command runner is also used by owned-fixture tests."""
    if mode not in ('pickup-context', 'pickup-ui'):
        raise ValueError('invalid read-only diagnostic mode')
    ui = mode == 'pickup-ui'
    probe = validated_probe(manifest)
    sha = manifest['sha256']
    sample = dict(schemaVersion=1, mode=mode, hostStarted=clock(), lobbyCheck=lobby,
                  probe=manifest, complete=False, liveFrameVerified=False,
                  actionReady=False, steps=[])

    def command(name, args, timeout=10):
        result = run(adb + args, capture_output=True, timeout=timeout)
        # Text shell commands use shell -T: exec-out did not preserve remote
        # nonzero exit status on the test phone. No shell text comes from GIDs.
        stdout = result.stdout.decode(errors='replace')
        sample['steps'].append(dict(name=name, exitCode=result.returncode,
                                    stdout=stdout, stderr=result.stderr.decode(errors='replace')))
        if result.returncode:
            raise RuntimeError(f'{name} failed with exit {result.returncode}')
        return stdout

    def frame(name):
        data = json.loads(command(name, ['shell', '-T', 'run-as', PACKAGE, 'cat', FRAME_PATH]))
        accepted = data.get('accepted')
        if (data.get('valid') is not True or not isinstance(accepted, dict) or
                data.get('verifiedScannerSha256') != pins['sha256']):
            raise RuntimeError('no accepted pinned live frame')
        if not -2000 <= clock() * 1000 - data['captureWallTimeMs'] <= 3000:
            raise RuntimeError('stale live frame or clock mismatch')
        raw = json.loads(data['rawLine'])
        if not isinstance(raw.get('self'), str) or not raw['self'] or not raw.get('entities'):
            raise RuntimeError('no active match camera')
        if accepted.get('self') != raw['self'] or not isinstance(accepted.get('entities'), list) or not any(
                isinstance(e, dict) and e.get('id') == raw['self'] for e in accepted['entities']):
            raise RuntimeError('accepted snapshot does not match the live camera')
        return data, raw['self']

    try:
        digest = command('diagnostic-hash', ['shell', '-T', 'sha256sum', probe]).split()
        if not digest or digest[0] != sha:
            raise RuntimeError('diagnostic hash mismatch')
        if not lobby:
            before, before_self = frame('frame-before')
            sample['frameBefore'] = before
        stdout = command(mode, ['shell', '-T', 'su -c ' + shlex.quote(
            shlex.join([probe, mode]))])
        records = [json.loads(line) for line in stdout.splitlines() if line.startswith('{')]
        sample['records'] = records
        prefix = 'pickupUi' if ui else 'pickupContext'
        begins = [r for r in records if r.get('record') == prefix + 'Begin']
        summaries = [r for r in records if r.get('record') == prefix + 'Summary']
        ends = summaries if ui else [r for r in records if r.get('record') == prefix + 'End']
        if len(begins) != 1 or len(summaries) != 1 or len(ends) != 1:
            raise RuntimeError('missing/duplicate diagnostic completion records')
        begin, summary, end = begins[0], summaries[0], ends[0]
        framing = 2 if ui else 3
        if (records[0] != begin or records[-1] != end or records[-(1 if ui else 2)] != summary or
                begin.get('schemaVersion') != 1 or summary.get('actionReady') is not False or
                len(records) > 4096 + framing or summary.get('dataRecords') != len(records) - framing or
                not isinstance(begin.get('gamePid'), int) or begin['gamePid'] <= 0 or
                end.get('gamePid') != begin['gamePid'] or
                not 0 <= end['capturedElapsedMs'] - begin['capturedElapsedMs'] <= 8000):
            raise RuntimeError('invalid diagnostic context/completion')
        if not lobby:
            after, after_self = frame('frame-after')
            sample['frameAfter'] = after
            if after_self != before_self or after['sequence'] < before['sequence']:
                raise RuntimeError('match context changed during capture')
            current_pids = command('game-pid', ['shell', '-T', 'pidof', 'com.proxima.dfm']).split()
            if current_pids != [str(begin['gamePid'])]:
                raise RuntimeError('game process changed during capture')
            sample['liveFrameVerified'] = True
        # complete is transport/context verification. The inner native complete
        # flag separately describes table coverage; neither allows actions.
        sample['nativeComplete'] = summary.get('complete') is True
        sample['complete'] = True
    except Exception as error:
        sample['error'] = f'{type(error).__name__}: {error}'
    sample['hostFinished'] = clock()
    return sample


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--manifest', type=Path, default=ROOT / 'scanner/build/pickup-probe-build.json')
    parser.add_argument('--label', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lobby', action='store_true')
    parser.add_argument('--mode', choices=('pickup-context', 'pickup-ui'), default='pickup-context')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', args.label):
        parser.error('invalid label')
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'evidence/local'):
        parser.error('output must be inside ignored evidence/local')
    manifest = json.loads(args.manifest.read_text())
    try:
        validated_probe(manifest)
    except ValueError as error:
        parser.error(str(error))
    pins = dict(line.split('=', 1) for line in (ROOT / 'scanner/deployment.properties').read_text().splitlines()
                if '=' in line and not line.startswith('#'))
    output.mkdir(parents=True, exist_ok=True)
    dest = output / (args.label + '.json')
    try:
        # Reserve once, before any device reads. A repeat label never overwrites.
        with dest.open('x') as handle:
            sample = capture([args.adb, '-s', args.serial], manifest, pins, args.lobby, mode=args.mode)
            handle.write(json.dumps(sample, indent=2) + '\n')
    except FileExistsError:
        parser.error('label already exists')
    print(json.dumps(dict(path=str(dest), complete=sample['complete'],
                          nativeComplete=sample.get('nativeComplete'), actionReady=False,
                          liveFrameVerified=sample['liveFrameVerified'], error=sample.get('error'))))
    if not sample['complete']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
