#!/usr/bin/env python3
"""Record an explicitly requested live test until the match clears or a time cap.

Owns only its UUID-named screenrecord process/files. Never operates the game.
Run only when the user has requested recording; output contains player identities.
"""
import argparse
import hashlib
import json
import shlex
import subprocess
import time
import uuid
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--serial', required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--duration', type=int, default=1800)
    parser.add_argument('--ignore-empty-feed', action='store_true',
                        help='Record a bounded diagnostic window with the radar stopped')
    args = parser.parse_args()
    if not 5 <= args.duration <= 3600:
        parser.error('duration must be 5..3600 seconds')
    args.evidence.mkdir(parents=True, exist_ok=True)
    adb = ['adb', '-s', args.serial]

    def root(script, timeout=20, **kwargs):
        return subprocess.run(adb + ['exec-out', 'su -c ' + shlex.quote(script)],
                              check=True, timeout=timeout, **kwargs)

    token = uuid.uuid4().hex
    deadline = time.monotonic() + args.duration
    parts = []
    cleared = False
    with (args.evidence / 'feed.jsonl').open('wb') as feed:
        try:
            while time.monotonic() < deadline and not cleared:
                index = len(parts)
                remote = f'/data/local/tmp/delta-recording-{token}-{index:03}.mp4'
                pidfile = remote + '.pid'
                duration = max(1, min(180, int(deadline - time.monotonic())))
                script = (f'echo $$ > {shlex.quote(pidfile)}; exec /system/bin/screenrecord '
                          f'--time-limit {duration} --bit-rate 6000000 {shlex.quote(remote)}')
                started = time.time()
                empty = 0
                with (args.evidence / f'part-{index:03}.log').open('wb') as log:
                    recorder = subprocess.Popen(adb + ['shell', 'su -c ' + shlex.quote(script)],
                                                stdout=log, stderr=subprocess.STDOUT)
                    try:
                        while recorder.poll() is None and time.monotonic() < deadline:
                            # Debug-app sandbox access avoids repeated Magisk grant toasts.
                            frame = subprocess.run(adb + ['exec-out', 'run-as', 'com.deltaradar.app',
                                                          'cat', 'files/latest-scanner-frame.json'],
                                                   check=True, timeout=20, capture_output=True).stdout
                            try:
                                obj = json.loads(frame)
                                snapshot = json.loads(obj['rawLine'])
                                empty = empty + 1 if not snapshot.get('self') else 0
                                feed.write(json.dumps({'recordedWallTime': time.time(), 'capture': obj},
                                                      separators=(',', ':')).encode() + b'\n')
                                feed.flush()
                            except (ValueError, KeyError):
                                empty = 0
                            if empty >= 6 and not args.ignore_empty_feed:
                                cleared = True
                                break
                            time.sleep(.5)
                    finally:
                        # Check both this recorder's PID and its unique output argument.
                        stop = (f'p=$(cat {shlex.quote(pidfile)} 2>/dev/null); '
                                'case "$p" in ""|*[!0-9]*) exit 0;; esac; '
                                'c=$(tr "\\000" " " < /proc/$p/cmdline 2>/dev/null); '
                                f'case "$c" in *screenrecord*{remote}*) kill -INT "$p";; esac')
                        root(stop, capture_output=True)
                        try:
                            recorder.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            recorder.terminate()  # Local adb child only; never the game.
                            recorder.wait(timeout=5)
                local = args.evidence / f'part-{index:03}.mp4'
                expected = int(root('stat -c %s ' + shlex.quote(remote), capture_output=True).stdout)
                with local.open('wb') as output:
                    root('cat ' + shlex.quote(remote), stdout=output, timeout=60)
                if local.stat().st_size != expected:
                    raise RuntimeError('Recording copy incomplete; remote file preserved: ' + remote)
                parts.append({'path': local.name, 'startWallTime': started, 'endWallTime': time.time(),
                              'bytes': expected, 'sha256': hashlib.file_digest(local.open('rb'), 'sha256').hexdigest()})
                root('rm -f ' + shlex.quote(remote) + ' ' + shlex.quote(pidfile), capture_output=True)
                (args.evidence / 'index.json').write_text(json.dumps(parts, indent=2) + '\n')
                print('Saved', local, 'bytes', expected, flush=True)
        finally:
            (args.evidence / 'index.json').write_text(json.dumps(parts, indent=2) + '\n')
    print('Recording ended:', 'match cleared' if cleared else 'time limit', flush=True)


if __name__ == '__main__':
    main()
