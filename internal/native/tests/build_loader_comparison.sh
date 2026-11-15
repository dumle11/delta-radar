#!/bin/bash
set -euo pipefail

native_dir="$(cd "$(dirname "$0")/.." && pwd)"
ndk_dir="${NDK_HOME:-${ANDROID_NDK_HOME:-$HOME/Library/Android/sdk/ndk/28.2.13676358}}"
toolchain="$ndk_dir/toolchains/llvm/prebuilt/darwin-x86_64/bin"
compiler="$toolchain/aarch64-linux-android26-clang"
output_dir="${1:-/tmp/delta-loader-comparison}"
mkdir -p "$output_dir"
chmod 700 "$output_dir"

"$compiler" -shared -fPIC -O2 -fvisibility=hidden \
    "$native_dir/tests/loader_probe.c" -llog -o "$output_dir/libloader_probe.so"
"$compiler" -shared -fPIC -O2 -fvisibility=hidden \
    -I"$native_dir" \
    "$native_dir/delta_radar_lib.c" -lm -landroid -o "$output_dir/liboverlay_plain.so"
chmod 600 "$output_dir"/*.so
sdk_dir="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Library/Android/sdk}}"
smoke_dir="$(mktemp -d)"
trap 'rm -rf "$smoke_dir"' EXIT
javac -source 8 -target 8 -Xlint:-options \
    -bootclasspath "$sdk_dir/platforms/android-31/android.jar" \
    -d "$smoke_dir" "$native_dir/tests/LoaderProbeSmoke.java"
"$sdk_dir/build-tools/35.0.0/d8" --min-api 26 --output "$smoke_dir" "$smoke_dir"/*.class
cp "$smoke_dir/classes.dex" "$output_dir/loader-probe-smoke.dex"
chmod 600 "$output_dir/loader-probe-smoke.dex"
shasum -a 256 "$output_dir/libloader_probe.so" "$output_dir/liboverlay_plain.so"
echo "Comparison artifacts: $output_dir"
