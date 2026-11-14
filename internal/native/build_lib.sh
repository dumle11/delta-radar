#!/bin/bash
#
# Build Delta Radar as an injectable shared library (.so).
#
# Requirements:
#   - Android NDK installed, NDK_HOME or ANDROID_NDK_HOME set
#   - Or NDK available at ~/Android/Sdk/ndk/*/
#
# Output: libue4patch.so (arm64-v8a)
#
# Usage: ./build_lib.sh
#

set -e

# Find NDK
NDK="${NDK_HOME:-${ANDROID_NDK_HOME:-}}"
if [ -z "$NDK" ]; then
    # Try common locations
    for d in ~/Android/Sdk/ndk/*/; do
        if [ -d "$d" ]; then NDK="$d"; break; fi
    done
fi
if [ -z "$NDK" ]; then
    echo "Error: Android NDK not found. Set NDK_HOME or ANDROID_NDK_HOME."
    exit 1
fi

TOOLCHAIN="$NDK/toolchains/llvm/prebuilt"
# Find host platform
HOST=$(ls "$TOOLCHAIN" 2>/dev/null | head -1)
if [ -z "$HOST" ]; then
    echo "Error: No toolchain found in $TOOLCHAIN"
    exit 1
fi

CC="$TOOLCHAIN/$HOST/bin/aarch64-linux-android26-clang"
if [ ! -x "$CC" ]; then
    echo "Error: Compiler not found at $CC"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$SCRIPT_DIR/delta_radar_lib.c"
OUT="$SCRIPT_DIR/libue4patch.so"

echo "[*] Building libue4patch.so (arm64-v8a)"
echo "    NDK: $NDK"
echo "    CC:  $CC"

$CC \
    -shared -fPIC \
    -O2 \
    -I"$SCRIPT_DIR" \
    -fvisibility=hidden \
    -s \
    -o "$OUT" \
    "$SRC" \
    -lm -landroid

echo "[+] Built: $OUT"
echo "    Size: $(du -h "$OUT" | cut -f1)"
echo ""
echo "To deploy (Zygisk):"
echo "  adb push $OUT /data/local/tmp/"
echo "  adb shell \"su -c 'cp /data/local/tmp/libue4patch.so /data/data/com.proxima.dfm/libue4patch.so && chown u0_a142:u0_a142 /data/data/com.proxima.dfm/libue4patch.so && chmod 755 /data/data/com.proxima.dfm/libue4patch.so && chcon u:object_r:app_data_file:s0:c142,c256,c512,c768 /data/data/com.proxima.dfm/libue4patch.so'\""
