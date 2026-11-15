# Delta Radar — Handoff (2026-11-15)

## Current weapon overrides — verified in firing range (2026-11-15)

The user confirmed **“Both are now steady”** for camera movement and bullet
marks with the final build. This follows repeated machine-gun wall tests;
intermediate “override active” counters were insufficient. Do not describe this
as a quantitative guarantee for every weapon, distance or future game build.

Final installed SHA256:
`ed554a5fcae807b2851d0c7876c5a23c2ad7ffbf74cd631e7cabc62d79b47be7`.
Local/device hashes match. Device `20f9aa6c`, app UID `u0_a556`; diagnostics are
in the app-private `cache/d` file. Final logs and investigation notes are under
`docs/weapon-2026-11-15/`.

`internal/native/engine_weapon.h` now:

- Selects WeaponManager.RuntimeContext.CurrentActiveWeapon (+0x218), verifies
  pawn/weapon ownership and rejects empty-hand or arbitrary holstered fallback.
- Resolves function (+0xB68) and data (+0xAC8) weak component lists with serial
  validation. Live FUObjectItem serials are at +0x14, inferred from multiple
  independent owned weak references; ambiguous layouts fail closed.
- Resolves recoil through GPWeaponDataComponentFiring.FireRecoilMethod (+0x578)
  when the FireShake DFM cache is null after a weapon switch. Supports DFM and
  PUBGM scalar layouts; live tests used DFM implementations.
- Handles additive DFM recoil factors with a finite negative adjustment and
  the native zero clamp, plus separate FireShake, GunKick and GunSway scalar
  paths. Disables only the local camera's WeaponRecoil, WeaponGunSway and Fire
  modifiers through their SDK bEnable fields. Other camera modifier types stay
  under host control.
- Handles spread authoring values, cached bounds/current angles, separate
  distribution/random-radius settings and the component's native enable gate
  (+0x1E0). Final maxima use a positive 1e-6 cap: native code treats zero as
  unlimited. Expected active counts: recoil/shake/spread `50/73/40`, `kick=2`,
  `sway=4`, `camera=3`, `gate=1` (gate is the tracked override count).
- Preserves opaque blocks, curves, maps, arrays and sampling/timing fields.
  Kernel-checked copies reject invalid write destinations; identity-checked
  groups restore values on disable, weapon change and scanner cleanup.

The scanner clears stale local-pawn state each update and always runs weapon
cleanup, including when possession disappears. Read-only completed-shot logs
are capped at 256 records per weapon. Their angle variation did not independently
establish authoritative impact dispersion; final firing-range confirmation came
from the user observing steady camera and grouping.

Validation: current NDK build, weapon regression and five host fixtures
(safe reads, projection, bounds visibility, entity types, FNames) passed;
`git diff --check` passed. Weapon regression includes weak identity/ownership,
active selection, both recoil layouts, camera flags, shake/kick/sway fields,
spread bounds/radii/gate, restoration, recycled objects and failed writes.
Two older engine visibility/aim fixtures still reference removed GameThread
bridge markers and fail during source extraction.

At task start another session had already replaced the GameThread bridge:
visibility calls ProcessEvent on the scanner thread and aim writes controller
rotation directly. Those changes were preserved. Older sections below describe
historical builds. Two unexpected exits occurred during earlier diagnostics;
no fresh crash tombstone established a cause. External process-memory probes
were discontinued. The final user-confirmed session remains running.

## Historical notes

## Internal aim assistance (2026-11-15)

The internal overlay's previously unused aim settings now drive native
GameThread aim control. This is separate from the external touch servo.

- Enable Aim in the Aim tab. A centered outlined FOV circle uses the same
  angular radius as acquisition (5–45 degrees; default 15). It turns green
  while locked. It also renders when entity ESP is disabled.
- Acquire the nearest allowed target point inside the circle. Targets require
  validated position, known positive health, no death state, and a fresh engine
  visibility result. The default mask includes enemy players and bots/NPCs.
- Retain the acquired identity while eligible, including when another actor
  becomes closer to the center. The radius gates acquisition, not retention.
- `engine_aim.h` resolves Controller.SetControlRotation (12-byte parameters)
  and SetIgnoreLookInput (1 byte) through the live class hierarchy and checks
  their native flags and parameter extents. It calls them only from the
  previously validated UE GameThread callback.
- While locked, set the controller's rotation directly and own one increment
  of SetIgnoreLookInput. Camera drags cannot yield control to manual look.
  Movement input is unaffected. Disabling Aim releases this one increment;
  the host's other input locks are preserved.
- Release for loss of eligibility/cover, death, stale data (>100ms), possession
  changes, object recycling, scanner stop or disabled Aim. Missing data does
  not leave a permanent input block.
- Aim points are capsule-relative Head/Chest/Body samples. The preferred point
  gets its own collision trace. If it is covered while another body sample is
  clear, use an exposed sample instead. These are not name-resolved bone joints.
- Removed the unused strength and auto-shoot controls from this internal menu;
  this path implements hard locking and does not fire automatically.

Live validation in the firing range: acquisition centered a dummy, and a
380px horizontal / 150px vertical camera drag retained the same actor with
corrections continuing. Disabling Aim cleared the target/corrections and the
same drag visibly moved the camera to the ceiling. Re-enabling reacquired;
a short joystick movement left aim tracking active. Screenshots and logs are
in `docs/aim-internal-2026-11-15/`. Physical-device user confirmation is pending.
All seven native fixtures and the Android bitmap/menu regression passed.

Files: `internal/native/aim_math.h`, `internal/native/engine_aim.h`,
`internal/native/engine_visibility.h`, `internal/native/esp_overlay.h`,
`internal/native/overlay/O.java`. Deployed library SHA256:
`b3a9092223e62d5d9e7a6b83ded184dedaad394586b04a1bf45d49140cd94961`.
Long-duration input ownership and moving live-player scenarios are not yet
validated; the runtime checks above do not establish zero-error aiming.

## Visibility fix verified (2026-11-15)

The internal overlay now uses actual `GPPhysics.LineTraceSingle` collision queries
on the UE GameThread. This supersedes the earlier bounds-based visibility notes.
The unverified `mesh+0x858` classifier and its override were removed.

- Implementation: `internal/native/engine_visibility.h`, called after scanner
  sampling and before rendering. Worker requests contain copied coordinates and
  validated object identities; engine queries never run on the scanner thread.
- Dispatch: validated Actor::ProcessEvent native signature at RVA `0x107e2070`,
  vtable slot `0x45`; the query receiver uses UObject::ProcessEvent `0x107e4460`.
  GPPhysics class/function/CDO are resolved and checked against runtime metadata.
  Runtime parameter extent is `0xC1` (aligned C storage `0xC8`), return at `0xC0`.
- Visibility channel: explicit `ECC_Visibility=3`, complex tracing, local pawn and
  target ignored. Five body samples: any clear = green, all blocked = red.
- Unavailable, stale (>100ms), recycled, destroyed, different-world/possession,
  or significantly moved observations remain unknown (amber). No render flag or
  coarse bound can override an internal engine result.
- Debug LOS rows and body markers now report engine query results. Camera/actor
  view rays and bound wireframes are still labelled bounds estimates.
- Live firing-range validation: 17 entities, valid camera; a captured log showed
  572 query batches / 44,110 rays and both full-cover and partial-cover results.
  The user confirmed: "Colors now match visibility" while moving across cover.
- All six host regressions passed, including the new synthetic engine bridge
  fixture. The existing visibility fixture was repaired to include current
  collector diagnostics; query-only/channel semantics were restored.

Collision LOS is not exact rendered pixel coverage. Five samples can miss tiny
exposed regions, and collision can differ from transparent/masked materials,
foliage or smoke. Renderer depth/coverage validation would be separate work.
Long-session and complete session-transition stability remain unverified.

## Current status

Game is running stably in Firing Range with full ESP overlay. Memfd-based
anonymous library loading is confirmed working — library appears as
`/memfd:jit-cache (deleted)` in `/proc/self/maps`, blending with Android ART
JIT cache entries. Anti-cheat (`libanogs.so`) no longer detects the library
in maps. Scanner reads 17 entities with a valid camera. See the visibility fix above for
the current classifier; earlier visible counts used an unverified classifier.

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
- Per-entity LOS: 5 engine-query samples (centre/upper/lower/side/side)
- Hit bound wireframes
- Named hit components

**Current limitation:** Internal visibility uses engine collision traces on five
body samples. View-ray hit bounds remain diagnostic estimates. Collision and
sample coverage can differ from rendered pixels; see the verified fix above.

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
| `external/scanner/memscan-legacy.c` | Camera/entity reading, external bounds estimates |
| `internal/native/engine_visibility.h` | Game-thread collision visibility bridge |
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

1. **Visibility follow-up**: engine collision traces are integrated and confirmed
   in the firing range. Tiny exposed regions, material/depth differences and
   broader scene/session coverage still need dedicated validation.
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
