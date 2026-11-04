#!/usr/bin/env python3
"""Linux owned-memory regression suite. Builds locally; never accesses a phone."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
TESTS = (
    'decoder_schedule_test.cpp', 'discovery_diagnostics_test.c',
    'feed_timestamp_test.c', 'health_lifecycle_test.c', 'legacy_reader_regression.c', 'loot_value_test.c',
    'position_sources_test.c', 'roster_cache_test.c',
    'transform_ring_test.cpp', 'position_codec_regression.c',
    'position_decoder_test.c', 'camera_stream_test.c',
    'skeleton_math_regression.c', 'skeleton_reader_regression.c',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'scanner/build/native-tests')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    for name in TESTS:
        cpp = name.endswith('.cpp')
        compiler = os.environ.get('CXX' if cpp else 'CC', 'c++' if cpp else 'cc')
        binary = output / Path(name).stem
        command = [compiler, '-std=c++17' if cpp else '-std=gnu11',
                   '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                   '-fsanitize=address,undefined', str(ROOT / 'scanner/tests' / name),
                   '-pthread', '-lm', '-o', str(binary)]
        if name == 'legacy_reader_regression.c':
            command.insert(1, '-D_LARGEFILE64_SOURCE')
        print(f'Checking {name}', flush=True)
        subprocess.run(command, cwd=ROOT, check=True, timeout=120)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=30)
    print(f'Passed {len(TESTS)} native regression programs with ASan/UBSan.', flush=True)


if __name__ == '__main__':
    main()
