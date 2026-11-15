#!/bin/bash
set -euo pipefail

overlay_dir="$(cd "$(dirname "$0")" && pwd)"
sdk_dir="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}"
android_jar="${ANDROID_JAR:-$sdk_dir/platforms/android-31/android.jar}"
dex_compiler="${D8:-$sdk_dir/build-tools/35.0.0/d8}"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

javac -source 8 -target 8 -Xlint:-options -bootclasspath "$android_jar" \
    -d "$build_dir" "$overlay_dir/O.java"
"$dex_compiler" --min-api 26 --output "$build_dir" "$build_dir"/*.class
python3 - "$build_dir/classes.dex" "$overlay_dir/../overlay_dex.h" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
Path(sys.argv[2]).write_text(
    'static const unsigned char _overlay_dex[] = {\n' +
    ''.join('    ' + ','.join('0x%02x' % b for b in data[i:i+16]) + ',\n'
            for i in range(0, len(data), 16)) + '};\n' +
    'static const unsigned int _overlay_dex_len = %d;\n' % len(data)
)
print('Generated overlay_dex.h (%d bytes)' % len(data))
PY
