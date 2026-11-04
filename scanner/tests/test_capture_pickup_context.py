"""No ADB/device: validate the capture workflow against owned command fixtures."""
import importlib.util
import json
import subprocess
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('pickup_capture', Path(__file__).parents[1] / 'tools/capture_pickup_context.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
SHA = 'a' * 64
MANIFEST = dict(sha256=SHA, path='/data/local/tmp/delta-pickup-probe-' + SHA[:16])
PINS = dict(sha256='b' * 64)


class Commands:
    def __init__(self):
        self.calls = []
        self.stale = False
        self.exit_code = 0
        self.hash = SHA
        self.camera_after = 'camera-a'
        self.pid = '123'
        self.truncate = False
        self.native_complete = True
        self.action_ready = False
        self.accepted_override = None

    def __call__(self, args, **kwargs):
        assert kwargs['capture_output'] and kwargs['timeout'] <= 10
        self.calls.append(args)
        if 'sha256sum' in args:
            output = self.hash + '  ' + MANIFEST['path']
        elif 'run-as' in args:
            nth = sum('run-as' in x for x in self.calls)
            camera = 'camera-a' if nth == 1 else self.camera_after
            entities = [dict(id=camera)]
            accepted = dict(self=camera, entities=entities) if self.accepted_override is None else self.accepted_override
            output = json.dumps(dict(valid=True, accepted=accepted, sequence=nth,
                captureWallTimeMs=1000 if self.stale else 100_000,
                verifiedScannerSha256=PINS['sha256'],
                rawLine=json.dumps(dict(self=camera, entities=entities))))
        elif 'pidof' in args:
            output = self.pid
        elif args[-1].startswith('su -c '):
            records = [dict(record='pickupContextBegin', schemaVersion=1, gamePid=123, capturedElapsedMs=100),
                dict(record='pickupContextSummary', complete=self.native_complete,
                     actionReady=self.action_ready, dataRecords=0),
                dict(record='pickupContextEnd', gamePid=123, capturedElapsedMs=120)]
            if args[-1].endswith("pickup-ui'") or args[-1].endswith('pickup-ui'):
                records = [dict(record='pickupUiBegin', schemaVersion=1, gamePid=123, capturedElapsedMs=100),
                    dict(record='pickupUiSummary', complete=self.native_complete,
                         actionReady=self.action_ready, dataRecords=0, gamePid=123, capturedElapsedMs=120)]
            if self.truncate:
                records.pop()
            output = '\n'.join(json.dumps(r) for r in records)
            return subprocess.CompletedProcess(args, self.exit_code, output.encode(), b'')
        else:
            raise AssertionError(f'unexpected command: {args}')
        return subprocess.CompletedProcess(args, 0, output.encode(), b'')


class CaptureTest(unittest.TestCase):
    def capture(self, commands, lobby=False, mode='pickup-context'):
        return module.capture(['adb-fixture', '-s', 'fixture'], MANIFEST, PINS,
                              lobby=lobby, run=commands, clock=lambda: 100.0, mode=mode)

    def test_ui_capture_uses_its_own_framing_and_live_guards(self):
        commands = Commands(); result = self.capture(commands, mode='pickup-ui')
        self.assertTrue(result['complete']); self.assertTrue(result['liveFrameVerified'])
        self.assertEqual('pickup-ui', result['mode']); self.assertFalse(result['actionReady'])
        self.assertEqual(5, len(commands.calls))

    def test_ui_truncation_and_action_flag_are_rejected(self):
        for option in ('truncate', 'action_ready'):
            commands = Commands(); setattr(commands, option, True)
            self.assertFalse(self.capture(commands, mode='pickup-ui')['complete'])

    def test_unknown_mode_is_rejected_before_device_reads(self):
        commands = Commands()
        with self.assertRaises(ValueError):
            self.capture(commands, mode='pickup-ui; input tap 1 1')
        self.assertEqual([], commands.calls)

    def test_live_context_is_bound_before_and_after_and_never_actionable(self):
        commands = Commands(); result = self.capture(commands)
        self.assertTrue(result['complete']); self.assertTrue(result['liveFrameVerified'])
        self.assertFalse(result['actionReady'])
        self.assertEqual(5, len(commands.calls))

    def test_native_partial_coverage_remains_explicit(self):
        commands = Commands(); commands.native_complete = False
        result = self.capture(commands)
        self.assertTrue(result['complete']); self.assertFalse(result['nativeComplete'])
        self.assertFalse(result['actionReady'])

    def test_accepted_snapshot_requires_object_with_matching_camera(self):
        for value in (True, {}, dict(self='other-camera', entities=[dict(id='other-camera')]),
                      dict(self='camera-a', entities=[])):
            commands = Commands(); commands.accepted_override = value
            self.assertFalse(self.capture(commands)['complete'])
            self.assertEqual(2, len(commands.calls))

    def test_stale_frame_stops_before_probe(self):
        commands = Commands(); commands.stale = True
        result = self.capture(commands)
        self.assertFalse(result['complete']); self.assertEqual(2, len(commands.calls))

    def test_wrong_hash_stops_before_frame_or_probe(self):
        commands = Commands(); commands.hash = 'c' * 64
        self.assertFalse(self.capture(commands)['complete']); self.assertEqual(1, len(commands.calls))

    def test_remote_failure_and_truncated_output_are_preserved(self):
        for option in ('exit_code', 'truncate'):
            commands = Commands(); setattr(commands, option, 3 if option == 'exit_code' else True)
            result = self.capture(commands)
            self.assertFalse(result['complete']); self.assertIn('stdout', result['steps'][-1])

    def test_context_or_process_change_rejects_live_binding(self):
        for option, value in [('camera_after', 'camera-b'), ('pid', '456')]:
            commands = Commands(); setattr(commands, option, value)
            result = self.capture(commands)
            self.assertFalse(result['complete']); self.assertFalse(result['liveFrameVerified'])

    def test_lobby_capture_is_never_live_proof(self):
        commands = Commands(); result = self.capture(commands, lobby=True)
        self.assertTrue(result['complete']); self.assertFalse(result['liveFrameVerified'])
        self.assertEqual(2, len(commands.calls))

    def test_unexpected_action_flag_is_rejected(self):
        commands = Commands(); commands.action_ready = True
        self.assertFalse(self.capture(commands)['complete'])

    def test_manifest_path_cannot_inject_shell_command(self):
        commands = Commands()
        with self.assertRaises(ValueError):
            module.capture(['fixture'], dict(sha256=SHA, path='/tmp/probe; input tap 1 1'),
                           PINS, run=commands)
        self.assertEqual([], commands.calls)


if __name__ == '__main__':
    unittest.main()
