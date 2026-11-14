#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NDK="/opt/homebrew/share/android-ndk"
CC="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android26-clang++"
STRIP="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"

OUT_DIR="$SCRIPT_DIR/out"
MOD_DIR="$OUT_DIR/delta_zygisk"
LIB_DIR="$MOD_DIR/zygisk"

rm -rf "$OUT_DIR"
mkdir -p "$LIB_DIR"

echo "[*] Compiling Zygisk module (arm64-v8a)"
$CC -std=c++17 -shared -fPIC -O2 \
    -Wall -Wextra \
    -DANDROID -D__ANDROID_API__=26 \
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
version=v1.0
versionCode=1
author=dme
description=Loads library into target process via Zygisk
EOF

# Package as zip
cd "$OUT_DIR"
(cd delta_zygisk && zip -r ../delta_zygisk.zip .)

echo "[+] Module: $OUT_DIR/delta_zygisk.zip"
echo ""
echo "Install: adb push $OUT_DIR/delta_zygisk.zip /data/local/tmp/"
echo "         adb shell 'su -c magisk --install-module /data/local/tmp/delta_zygisk.zip'"
echo "         adb reboot"
