# Delta Radar: Anti-Cheat Bypass & Device Ban Knowledge

## Overview

Delta Force Mobile (com.proxima.dfm) uses **Tencent ACE** (Anti-Cheat Expert) with two native libraries:
- `libanort.so` — Primary anti-cheat (code integrity checks, kill-on-detect)
- `libanogs.so` — Secondary anti-cheat (GOT-based identity reading, device fingerprinting)

A **device ban** ("Irregular connection / abnormal game client behavior") is:
- 3-day duration
- Tied to DEVICE, not account — persists through account changes, app reinstalls
- Server-side — can only be bypassed by presenting a completely different device fingerprint

## Architecture

### Injection Chain
```
Magisk (Zygisk) → loader.cpp (Zygisk module)
    → preAppSpecialize: match package name
    → postAppSpecialize: spawn thread → dlopen libue4patch.so → JNI_OnLoad
        → _spoof_build_fields(): JNI reflection to override Build.* fields
        → _mask_memory(): rename library regions in /proc/self/maps
        → _apply_cfg():
            1. Neutralize libanort.so (NOP kill sites, hook GOT kill/exit/syscall/fork)
            2. Neutralize libanogs.so (hook GOT kill/exit/syscall/fork/abort)
            3. Install identity-spoofing GOT hooks (popen, __system_property_get, fopen)
        → Start scanner thread (entity detection, ESP overlay)
```

### Two-Component System
1. **Internal** (`internal/`): Zygisk module + injectable .so (runs inside game process)
2. **External** (`app/`): Android overlay app "Delta Radar" (receives entity data, draws ESP)

## Device Fingerprint Spoofing

### What ACE reads (and what we hook)

| Identifier | Read Method | Hook Location | Status |
|---|---|---|---|
| Serial (ro.serialno) | `__system_property_get` | libanogs.so GOT | HOOKED |
| Boot serial | `__system_property_get` | libanogs.so GOT | HOOKED |
| GSM serial | `__system_property_get` | libanogs.so GOT | HOOKED |
| IMEI (ril.IMEI, etc.) | `__system_property_get` | libanogs.so GOT | HOOKED |
| Build fingerprint | `__system_property_get` | libanogs.so GOT | HOOKED |
| Build display ID | `__system_property_get` | libanogs.so GOT | HOOKED |
| Build ID | `__system_property_get` | libanogs.so GOT | HOOKED |
| Build.FINGERPRINT | Java field | JNI reflection | HOOKED |
| Build.DISPLAY | Java field | JNI reflection | HOOKED |
| Build.ID | Java field | JNI reflection | HOOKED |
| Build.SERIAL | Java field | JNI reflection | HOOKED |
| Serial via popen | `popen("getprop serial")` | libanogs.so GOT | HOOKED |
| IMEI via service call | `popen("service call iphonesubinfo")` | libanogs.so GOT | HOOKED |
| Telephony via dumpsys | `popen("dumpsys phone/telephony")` | libanogs.so GOT | HOOKED |
| /sys serial_number | `fopen` | libanogs.so GOT | HOOKED |
| /sys soc0/serial | `fopen` | libanogs.so GOT | HOOKED |
| /sys cid | `fopen` | libanogs.so GOT | HOOKED |
| net/address (MAC) | `fopen` | libanogs.so GOT | HOOKED |
| Android ID (SSAID) | ContentResolver (Java) | Manual DB edit | MANUAL |
| GSF ID | GServices ContentProvider | NOT HOOKED | TODO |
| DRM Device ID (Widevine) | MediaDrm API | NOT HOOKED | TODO |
| Google Advertising ID | Play Services API | NOT HOOKED | TODO |

### What's NOT hooked (potential ban contributors)
- **GSF ID** (Google Services Framework ID) — very persistent, survives reinstalls
- **DRM/Widevine device ID** — hardware-backed, extremely hard to spoof
- **Google Advertising ID** — resettable via Settings but not hooked
- **Other Build.* fields** (MODEL, BRAND, DEVICE, BOARD, HARDWARE, MANUFACTURER)
- **CPU/hardware identifiers** via /proc/cpuinfo or Java APIs
- **SIM identifiers** (ICCID, IMSI) via TelephonyManager Java API

### Persistent Fake IDs
Fake identifiers are stored in `/data/data/com.proxima.dfm/cache/.fids` (XOR-encoded path, key=0x55):
```
line 1: fake IMEI (15 digits, valid Luhn)
line 2: fake serial (16 hex chars)
line 3: fake android_id (16 hex chars)
```
Generated once from /dev/urandom, reused across app restarts. **Delete this file to generate new IDs.**

### Android ID (SSAID) Modification
Per-app SSAID lives in `/data/system/users/0/settings_ssaid.xml`. Must be modified with root before app launch:
```bash
# Find the entry: name=<UID> package=com.proxima.dfm
# Current UID: 10552 (changes on reinstall!)
adb shell "su -c 'grep proxima /data/system/users/0/settings_ssaid.xml'"
# Modify:
adb shell "su -c 'sed -i \"s/name=\\\"10552\\\" value=\\\"OLD\\\"/name=\\\"10552\\\" value=\\\"NEW\\\"/\" /data/system/users/0/settings_ssaid.xml'"
```
Note: SSAID is per-app, tied to device+signing_key. Reinstalling from Play Store preserves it. Only factory reset or manual edit changes it.

## Zygisk Module Details

### Critical Build Settings (build.sh)
```bash
# DO NOT add -fvisibility=hidden — it hides zygisk_module_entry, causing silent rejection
# DO add -static-libstdc++ — libc++_shared.so is not available on device
$CC -std=c++17 -shared -fPIC -O2 -static-libstdc++ ...
```

### API Version
`ZYGISK_API_VERSION` must be `2` for Magisk 28.1. Higher versions (3-5) are silently rejected by `registerModule()`.

### AppSpecializeArgs Struct Layout (Magisk 28.1)
The header's struct layout does NOT match the actual runtime layout:
- **nice_name** is at pointer offset **6**, not offset 7 as in the header
- **instruction_set** is at offset 7 (returns "arm64")
- Must read via raw pointer cast: `reinterpret_cast<uintptr_t *>(args)[6]`

### Shamiko Interaction
If Shamiko is installed in **blacklist mode** and the game is on the Magisk denylist, ALL Zygisk modules are hidden from the game. Fix:
```bash
magisk --denylist rm com.proxima.dfm
```

## SELinux & Deployment

### Library Location
Must be in app's own data directory, NOT /data/local/tmp/:
```bash
# /data/local/tmp/ has shell_data_file context — app can't dlopen from there
# Must use app_data_file context
adb shell "su -c 'cp /data/local/tmp/libue4patch.so /data/data/com.proxima.dfm/libue4patch.so'"
adb shell "su -c 'chown u0_a552:u0_a552 /data/data/com.proxima.dfm/libue4patch.so'"
adb shell "su -c 'chmod 755 /data/data/com.proxima.dfm/libue4patch.so'"
adb shell "su -c 'chcon u:object_r:app_data_file:s0:c40,c258,c512,c768 /data/data/com.proxima.dfm/libue4patch.so'"
```
**UID changes on reinstall!** Check with: `dumpsys package com.proxima.dfm | grep userId=`
SELinux categories are derived from UID. Check actual context: `ls -laZ /data/data/com.proxima.dfm/`

## Common Issues & Fixes

### "Activity class does not exist" after `pm clear`
Dex cache gets corrupted. DO NOT use `pm install -r base.apk` — this loses split APKs.
Fix: reinstall from Play Store.

### Game crashes during resource download
Likely our scanner/overlay causing issues. The scanner runs on a separate thread and shouldn't affect downloads, but check diag for multiple initialization blocks (indicates crashes/restarts).

### Hooks install too late
The loader thread delay determines when hooks activate. Currently 200ms (was 3000ms).
If libanogs.so isn't loaded when library starts, a poller thread checks every 50ms for up to 30 seconds.

### Loader thread not attached to JVM
The Zygisk loader creates a native pthread. JNI_OnLoad must call `AttachCurrentThread` before using JNI APIs:
```c
JNIEnv *env = NULL;
int attached = 0;
if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
    if ((*vm)->AttachCurrentThread(vm, &env, NULL) == JNI_OK)
        attached = 1;
}
// ... use env ...
if (attached) (*vm)->DetachCurrentThread(vm);
```

## XOR String Encoding

All game-specific strings use XOR encoding to prevent `strings` detection:
- **delta_radar_lib.c**: key = `0x4A` (identity strings, paths, property names/values)
- **delta_radar_lib.c**: key = `0x55` (file paths like diag, cache, stop sentinel)
- **loader.cpp**: key = `0x33` (package name, library path)

Rules:
- Key must be declared `volatile` to prevent compiler constant-folding
- Decode function must be `__attribute__((noinline))`
- Always run `strings <binary> | grep -iE 'serial|imei|proxima|delta|...'` after build

## GOT Hook Offsets (libanogs.so)

These are hardcoded for the current game version and WILL change on game update:
```c
#define G_GOT_POPEN     0x52d2e0
#define G_GOT_PROPGET   0x52d488
#define G_GOT_FOPEN     0x52d1f8
#define G_GDAT_FOPEN    0x52c898
#define G_GOT_KILL      0x52d188
#define G_GOT_SYSCALL   0x52d320
#define G_GOT_FORK      0x52d3a8
#define G_GOT_EXIT      0x52d3c0
#define G_GOT_ABORT     0x52d4e0
// ... etc
```

## Full Deployment Checklist

1. Build library: `cd internal/native && bash build_lib.sh`
2. Build Zygisk module: `cd internal/zygisk && bash build.sh`
3. Verify no string leaks: `strings libue4patch.so | grep -iE 'serial|imei|proxima|delta|fingerprint'`
4. Push library: `adb push libue4patch.so /data/local/tmp/`
5. Copy with correct permissions (check UID first!)
6. Install Zygisk module: `adb push out/delta_zygisk.zip /data/local/tmp/ && adb shell 'su -c magisk --install-module /data/local/tmp/delta_zygisk.zip'`
7. Verify game NOT on Magisk denylist: `magisk --denylist ls | grep proxima`
8. Reboot: `adb reboot`
9. After boot, verify library exists: `ls -laZ /data/data/com.proxima.dfm/libue4patch.so`
10. Launch game, check diag: `su -c 'cat /data/data/com.proxima.dfm/cache/d'`

## Recommended Ban Bypass: LSPosed + AndroidFaker

GOT-level hooks on libanogs.so are **insufficient** for device ban bypass. ACE reads identity
through many paths (Java APIs, ContentResolver, TelephonyManager, other native libraries).
The native library's GOT hooks only intercept calls going through libanogs.so's own GOT.

**Use AndroidFaker (Xposed module) via LSPosed** — already installed on this device:
1. Open LSPosed Manager
2. Find AndroidFaker → add com.proxima.dfm to its scope
3. Open AndroidFaker → configure fake device values for the game
4. Force-stop game, relaunch

AndroidFaker hooks at the Java framework level, intercepting ALL identity reads:
- Settings.Secure.ANDROID_ID (ContentResolver)
- Build.FINGERPRINT, Build.MODEL, Build.SERIAL, etc.
- TelephonyManager.getDeviceId(), getImei(), getSimSerialNumber()
- WifiInfo.getMacAddress()
- MediaDrm (Widevine) device ID
- BluetoothAdapter.getAddress()

### Installed Xposed Modules (LSPosed)
| Module | Status | Scoped To |
|---|---|---|
| AndroidFaker (com.android1500.androidfaker) | Enabled | Self only — **NEEDS GAME SCOPE** |
| delta.spoof (com.delta.spoof) | Enabled | No scope entries |
| HideMyAppList | Enabled | system, com.openai.sora |
| CorePatch (com.coderstory.toolkit) | Enabled | Self only |
| PairIP Fix | Disabled | com.openai.sora |
| SSL Unpinning | Disabled | — |

### LSPosed Module Database
Located at `/data/adb/lspd/config/modules_config.db` (SQLite).
Tables: `modules` (mid, module_pkg_name, apk_path, enabled), `scope` (mid, app_pkg_name, user_id).

### What the native library should still handle
- Anti-cheat neutralization (kill/exit/syscall/fork GOT hooks on libanort.so + libanogs.so)
- Scanner + ESP overlay
- Memory masking (PR_SET_VMA_ANON_NAME)
- The identity GOT hooks can remain as a backup layer

## For New Ban Bypass Attempt

1. Scope AndroidFaker to com.proxima.dfm in LSPosed, configure fake device values
2. Delete persistent fake IDs: `su -c 'rm /data/data/com.proxima.dfm/cache/.fids'`
3. Modify SSAID (check UID first): edit settings_ssaid.xml
4. Clear GSF ID: `su -c 'pm clear com.google.android.gsf'` (resets Google registration!)
5. Deploy updated library (check UID changed!)
6. Reboot (for LSPosed scope changes to take effect)
7. Launch game as guest (not logged-in account)

## Device Details
- OnePlus 5 (OnePlus5/msm8998), Android 10
- Magisk 28.1, Zygisk enabled, Shamiko installed
- Device PIN: 3515
- App UID: 10552 (u0_a552) — changes on reinstall
- SELinux context: u:object_r:app_data_file:s0:c40,c258,c512,c768
