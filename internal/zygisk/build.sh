#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NDK="${NDK_HOME:-${ANDROID_NDK_HOME:-/opt/homebrew/share/android-ndk}}"
HOST=$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)
CC="$NDK/toolchains/llvm/prebuilt/$HOST/bin/aarch64-linux-android26-clang++"
STRIP="$NDK/toolchains/llvm/prebuilt/$HOST/bin/llvm-strip"

OUT_DIR="$SCRIPT_DIR/out"
MOD_DIR="$OUT_DIR/delta_zygisk"
LIB_DIR="$MOD_DIR/zygisk"

rm -rf "$MOD_DIR"
mkdir -p "$LIB_DIR" "$MOD_DIR/payload"
NDK_HOME="$NDK" bash "$SCRIPT_DIR/../native/build_lib.sh"
cp "$SCRIPT_DIR/../native/libue4patch.so" "$MOD_DIR/payload/arm64-v8a.so"

echo "[*] Compiling Zygisk module (arm64-v8a)"
$CC -std=c++17 -shared -fPIC -O2 \
    -Wall -Wextra \
    -DANDROID \
    -static-libstdc++ \
    -I"$SCRIPT_DIR" \
    "$SCRIPT_DIR/loader.cpp" \
    -o "$LIB_DIR/arm64-v8a.so" \
    -llog -ldl

$STRIP --strip-all "$LIB_DIR/arm64-v8a.so"

echo "[*] Size: $(du -h "$LIB_DIR/arm64-v8a.so" | cut -f1)"

# Module descriptor
cat > "$MOD_DIR/module.prop" << 'EOF'
id=delta_zygisk
name=Delta Zygisk Loader
version=v2.0
versionCode=2
author=dme
description=ARM64 internal loader and bundled payload for Android 10-14; Zygisk API 2 (Magisk 24+)
EOF

touch "$MOD_DIR/skip_mount"
(cd "$MOD_DIR" && shasum -a 256 zygisk/arm64-v8a.so payload/arm64-v8a.so > SHA256SUMS)
rm -f "$OUT_DIR/delta_zygisk.zip"

# Package as zip
cd "$OUT_DIR"
(cd delta_zygisk && zip -r ../delta_zygisk.zip .)

echo "[+] Module: $OUT_DIR/delta_zygisk.zip"
echo ""
echo "Install: adb push $OUT_DIR/delta_zygisk.zip /data/local/tmp/"
echo "         adb shell 'su -c magisk --install-module /data/local/tmp/delta_zygisk.zip'"
echo "         adb reboot"
