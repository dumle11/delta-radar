# Delta Radar — Handoff (2026-11-15)

## Current status

Game is running stably in Firing Range with full ESP overlay. Memfd-based
anonymous library loading is confirmed working — library appears as
`/memfd:jit-cache (deleted)` in `/proc/self/maps`, blending with Android ART
JIT cache entries. Anti-cheat (`libanogs.so`) no longer detects the library
in maps. Scanner reads 17 entities with a valid camera, 119 occluder bounds
collected, 14/17 entities classified as visible, 3-5ms read time per frame.

### Crash fix summary

Two root causes were identified and fixed:

1. **Library visible in `/proc/self/maps`**: `libue4patch.so` was loaded via
   `dlopen` from a file path, making it visible as a file-backed mapping.
   Anti-cheat scans maps for suspicious libraries. **Fix:** Zygisk loader now
   reads the library into a `memfd_create` fd, then loads via
   `android_dlopen_ext` with `ANDROID_DLEXT_USE_LIBRARY_FD`. The library never
   touches disk as a mapped file.

2. **`_mask_memory()` SIGSEGV**: The function did `mmap(MAP_FIXED|MAP_ANONYMOUS)`
   over its own executing code page, zeroing the page mid-execution. Tombstone:
   `signal 11 (SIGSEGV), code 2 (SEGV_ACCERR)` at `_mask_memory+0xc2f4`.
   **Fix:** `_mask_memory()` simplified to only attempt `PR_SET_VMA_ANON_NAME`
   on memfd mappings (no dangerous self-remap). With memfd loading, mappings
   are anonymous from the start.

### Raycast visibility debug

Debug visualization enabled in Status → Raycast debug. Shows:
- Camera view ray (cyan=clear, red=hit, amber=unknown)
- Per-entity LOS: 5 sample rays (centre/upper/lower/side/side)
- Hit bound wireframes
- Named hit components

**Current limitation:** Visibility uses bounds-based estimation, not engine
collision traces. If ANY of 5 rays is clear, the entity is considered visible
(green). Only when ALL 5 are blocked is it occluded (red). Unknown state
(incomplete occluder data or camera inside a bound) is amber. Some walls may
not be collected as occluders, causing false "visible" results.

## Device and deployment state

- **Loader module:** `delta_zygisk` ENABLED, game removed from DenyList
- **Library loading:** `memfd_create` + `android_dlopen_ext` (maps as `/memfd:jit-cache (deleted)`)
- **Library source:** `/data/data/com.proxima.dfm/libue4patch.so`, mode 755,
  owner `u0_a556:u0_a556`, context `u:object_r:app_data_file:s0:c44,c258,c512,c768`
- **Anti-cheat hooks:** `_spoof_build_fields`, `_mask_memory`, `_apply_cfg`
  (GOT hooks + SVC NOPs in libanort.so), `patch_report_functions`
- **Overlay:** `SYSTEM_ALERT_WINDOW` permission granted
- **Memory reads:** pipe-based checked kernel copies (`self-memory.h`), no signal handlers
- **Boot script:** `spoof_props.sh` disabled, `/data/adb/service.d/` empty
- **`/data/local/tmp/`:** cleaned — hacking tools in `/data/local/.stash/`
- Device: OnePlus 5 / ONEPLUS A5000, Android 10, serial `20f9aa6c`, PIN `3515`
- Package: `com.proxima.dfm`, UID `10556` / `u0_a556`
- Version: `1.202.37119.3601`
- Magisk: 28.1, Zygisk=1, DenyList=1 (game NOT in list)

### Deploy commands
```bash
# Build library
cd internal/native && bash build_lib.sh
# Verify no identifiable strings leaked
strings libue4patch.so | grep -iE 'serial|imei|proxima|delta|fingerprint|radar'
# Deploy to device
adb push libue4patch.so /data/local/tmp/
adb shell "su -c 'cp /data/local/tmp/libue4patch.so /data/data/com.proxima.dfm/libue4patch.so \
  && chown u0_a556:u0_a556 /data/data/com.proxima.dfm/libue4patch.so \
  && chmod 755 /data/data/com.proxima.dfm/libue4patch.so \
  && chcon u:object_r:app_data_file:s0:c44,c258,c512,c768 /data/data/com.proxima.dfm/libue4patch.so'"
# Clean up tmp so anti-cheat doesn't see it
adb shell "su -c 'mv /data/local/tmp/libue4patch.so /data/local/.stash/'"
# Clear diag log, restart game
adb shell "su -c 'rm -f /data/data/com.proxima.dfm/cache/d'"
adb shell "am force-stop com.proxima.dfm"
# Launch from home screen (am start may fail right after boot)
```

### Build Zygisk module
```bash
cd internal/zygisk && bash build.sh
adb push delta_zygisk.zip /data/local/tmp/
adb shell "su -c 'magisk --install-module /data/local/tmp/delta_zygisk.zip'"
adb shell "su -c 'mv /data/local/tmp/delta_zygisk.zip /data/local/.stash/'"
adb reboot
```

## Architecture

Two-component system: injectable `.so` scanner + Android overlay launcher.

### Injection path
1. Zygisk module (`internal/zygisk/loader.cpp`, class `SvcModule`) intercepts
   app specialization at fork time
2. Matches target package `com.proxima.dfm` (XOR-encoded)
3. Spawns detached `_loader_thread` after 200ms delay
4. Thread reads `libue4patch.so` from game's private dir into memory
5. Creates anonymous fd via `memfd_create("jit-cache", MFD_CLOEXEC)`
   (name XOR-encoded with key 0x33)
6. Writes library bytes to memfd, loads via `android_dlopen_ext` with
   `ANDROID_DLEXT_USE_LIBRARY_FD`
7. Calls `JNI_OnLoad` which runs anti-cheat mitigations and starts scanner thread

### Scanner and overlay
- `delta_radar_lib.c` → `JNI_OnLoad`: `_mask_memory()`, `_apply_cfg()`,
  `patch_report_functions()`, then spawns scanner thread
- Scanner (`memscan-legacy.c`): reads entity/camera data from game memory via
  pipe-based checked copies (`self-memory.h`)
- ESP rendering (`esp_overlay.h`): JNI bridge to Java overlay
- Java overlay (`O.java`): `TYPE_APPLICATION_OVERLAY` windows — drawing surface,
  draggable bubble, touchable menu with Visuals/Colors/Status tabs
- Overlay DEX embedded in native library via `overlay_dex.h`

### Anti-cheat mitigations (Tencent ACE)
- GOT/GDAT hooks in `libanogs.so`: `access`, `stat`, `fstatat`, `statfs`,
  `open`, `__open_2`, `__openat_2`, `opendir`, `readlink`, `fopen` →
  return ENOENT for root paths (`/sbin/*`, `/data/adb*`, `magisk`, `supersu`)
- `__system_property_get` → spoof `ro.boot.verifiedbootstate` as `green`
- SVC instruction NOPs in `libanort.so` (syscall-level checks)
- `patch_report_functions` (report/telemetry neutralization)
- Stub page: mmap'd anonymous RWX→RX page with ARM64 assembly stubs for
  kill, exit, syscall, fork, tp_syscall_imp, abort; labeled `scudo:heap`

### String obfuscation
All sensitive strings use volatile XOR key + `__attribute__((noinline))` to
defeat compiler constant-folding. After any code change, verify with:
```bash
strings libue4patch.so | grep -iE 'serial|imei|proxima|delta|fingerprint|radar'
```

## Key files

| Path | Purpose |
| --- | --- |
| `internal/zygisk/loader.cpp` | Zygisk module — memfd injection |
| `internal/zygisk/build.sh` | Builds delta_zygisk.zip |
| `internal/native/delta_radar_lib.c` | Main injectable library, JNI_OnLoad |
| `internal/native/esp_overlay.h` | JNI API, projection, boxes, skeleton, raycast debug |
| `internal/native/overlay/O.java` | Overlay menu, frame presentation, preferences |
| `internal/native/build_lib.sh` | Native arm64 build |
| `internal/native/overlay/build_overlay.sh` | DEX/header generation |
| `external/scanner/memscan-legacy.c` | Camera/entity reading, visibility raycasting |
| `external/scanner/skeleton-reader.h` | Validated animated pose and bone selection |
| `external/scanner/self-memory.h` | Pipe-based checked memory copies |

## Wire protocol (C↔Kotlin short keys)

| Key | Meaning |
| --- | --- |
| `n` | entity count |
| `id` | entity ID |
| `x`,`y`,`z` | position |
| `h` | health |
| `mh` | max health |
| `hk` | health known |
| `d` | dead |
| `nm` | name |
| `k` | kind (p=player, a=bot, l=loot) |
| `tm` | team (1=own) |
| `hh` | half height |
| `yw` | yaw |
| `pt` | pitch |
| `se` | self entity ID |
| `cx`,`cy`,`cz` | camera position |
| `cp`,`cyw` | camera pitch/yaw |
| `cf` | camera FOV |
| `bv` | bone valid |
| `bc` | bone count |
| `b` | bone positions array |
| `bp` | bone parents array |

## Diagnostics

| Path / marker | Meaning |
| --- | --- |
| `/data/data/com.proxima.dfm/cache/d` | Monotonic timestamped diag log |
| `camera=0` / `camera=1` | Camera validity state |
| `occ=N` | Number of occluder bounds collected |
| `visible=N` | Entities classified as visible |
| `.noscan` in `/data/local/tmp/` | Skips scanner+overlay at init |
| `.s7xk` in `/data/local/tmp/` | Stops scanner at next 300-iteration check |

Read diag log: `adb shell "su -c 'cat /data/data/com.proxima.dfm/cache/d'"`

## Remaining work

1. **Raycast visibility accuracy**: bounds-based estimation misses some walls.
   Consider collecting more scene components, or adding engine-internal
   collision trace integration.
2. **Long-term crash stability**: memfd loading resolved the immediate crash
   but extended play testing is needed.
3. **Live skeleton verification**: toggle present, getter signature validated,
   but live skeleton poses on targets not yet confirmed.
4. **Session transitions**: menu→Loadout→weapon→Modify→Firing Range, exit/
   re-enter, background/foreground not fully tested.

## GOT offsets in libanogs.so

```
G_GOT_ACCESS=0x52d358    G_GDAT_ACCESS=0x52c978
G_GOT_STAT=0x52d360      G_GDAT_STAT=0x52c980
G_GOT_OPEN=0x52d330      G_GDAT_OPEN=0x52c950
G_GOT_OPEN2=0x52d2a0     (__open_2)
G_GOT_OPENAT2=0x52d450   (__openat_2)
G_GOT_OPENDIR=0x52d388   G_GDAT_OPENDIR=0x52c9b8
G_GOT_READLINK=0x52d468
G_GOT_STATFS=0x52d470
G_GOT_FSTATAT=0x52d438
```

## Historical context

Extensive crash investigation documented in `docs/overlay-2026-11-14/`. Key findings:
- Anti-cheat scans `/data/local/tmp/` for hacking tool filenames → deliberate
  register-poisoning termination via `sub_47e678` in libanogs.so
- Deliberate termination mechanism: sleep 10s, compute random low address from
  `random() & 0xFFF + 0x1000`, poison all registers, branch to invalid address
- Game exits also observed without payload/loader (device-level issue)
- Tencent ACE analysis reference: `ff-ace-anticheat-analysis` (Free Fire ARMv7,
  different target but relevant research)
