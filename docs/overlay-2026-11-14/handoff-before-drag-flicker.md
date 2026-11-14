# Delta Radar — Session Handoff (2026-11-14)

## Current verified state — latest continuation

- `.noscan` has been removed, restoring the scanner and overlay. The stale
  `.s7xk` stop sentinel remains archived. The tabbed overlay was seen running on
  the device, and the latest library has been deployed and launched.
- A successful run entered the firing range with 17 entities and a valid,
  plaintext camera. Logs recorded camera movement and rotation while scanning
  continued past frame 4400. Screenshots before/after a camera turn show boxes
  following and enclosing the visible mannequins instead of remaining cut off
  at the bottom of the screen. Captures are in
  `docs/overlay-2026-11-14/range-tracking-before-turn.png` and
  `range-tracking-after-turn.png`.
- The live skeleton compatibility gate was still checking the older getter at
  RVA `0x1acda0fc`, where this build has different instructions. A read-only
  executable-segment search found one exact expected getter at `0x1b040308`.
  Loaded memory confirmed `f9438c08 b4000068 b941d100 d65f03c0`, preserving the
  mesh+0x718 / asset+0x1d0 layout check. The reader now accepts either RVA only
  when the exact signature matches. The regression exercises both profiles.
- Native build, host projection/safe-read tests, skeleton math, the full Android
  synthetic skeleton-reader test, and Android overlay/menu tests pass. The old
  character-identity regression remains a pre-existing failure.
- The last deployed library SHA-256 is
  `27e5bb3620fb62947a13f92dbfe0811beffd11a8810e382061749888839a119a`;
  the local and device copies matched. This includes the updated skeleton gate.
- **Still unverified:** live skeleton lines on training mannequins after the
  profile update, menu preview-model alignment, and longer crash stability.
  Intermittent game exits/crashes were observed during earlier attempts. One
  successful range run does not establish that every crash has been fixed.

The sections below retain investigation history and prior claims; use this
section for the latest verified results.

## Review and fixes in progress (2026-11-14)

This section supersedes conflicting claims below. The earlier “working” list
records prior-session claims, not an end-to-end verification of the current build.

**Confirmed cause of frozen overlay updates:** `/data/local/tmp/.s7xk` was a
root-owned stop sentinel dated November 7. The scanner checks this every 300
iterations, then exits. The game UID cannot unlink the root-owned file from
`/data/local/tmp`, so it remained across launches. Instrumented device logs ended
at frame 299, matching this branch. The sentinel was archived as
`/data/local/tmp/.s7xk.previous-overlay-stop`; stop/failed-unlink/cleanup events now
write diagnostics. Do not restore that archive as `.s7xk` unless requesting a stop.

**Source corrections:**
- The GL renderer was still installed at startup despite being described as unused.
  Its include and installation call were removed; Java is now the only renderer.
- The camera was retained when a frame lacked self. Validity is now reset each
  frame; self matching uses `self_entity_id`, including replacement-pawn IDs.
- Pawn-origin camera fallbacks are unsuitable for accurate screen projection.
  The overlay now requires the reader's valid camera sample and finite inputs.
- Invalid, empty, and disabled frames now clear prior pixels. The Java consumer
  also expires frames after 500ms (periodic invalidation every 250ms), independently
  of whether the producer keeps running.
- Buffer size follows the measured overlay View, including orientation changes;
  startup no longer forces landscape dimensions. Bitmap drawing/swapping/resizing
  is synchronized between the UI and producer.
- `box_h * 0.45` was used as half-width, producing a total width of 0.9 times
  height. Half-width is now `box_h * 0.45 * 0.5`.
- Projection now culls the finished box rather than head/feet separately; the
  arbitrary 800px height clamp and distance-based fallback box were removed.
- Safe-read jump points now restore the saved signal mask. This is a robustness
  change, not the demonstrated cause of the frame-300 stop.

**Additional live finding:** after removing the stop sentinel, the reader passed
frame 1500, then a native stack capture showed it waiting in jemalloc during
`CanvasJNI::initRaster`; the UI waited in `O.onDraw` on the frame lock. Canvas
objects are now cached with their bitmaps rather than allocated every frame.
The UI uses a nonblocking lock attempt, so a stalled producer cannot hold up
`onDraw`. Android regression also exercises 2,000 reusable frames and a
contended producer lock. This does not establish the cause of the process-wide
allocator contention; longer live verification is still necessary.

**Validation so far:** native arm64 build passes; host projection regression
passes; 300 recoverable memory faults pass; Android synthetic bitmap tests pass
for publication, disabling, empty frames, resize and expiry. The existing legacy
reader regression fails `test_character_identity` on both HEAD and the modified
source when run on Android; this failure predates these changes. The full reader
test does not compile natively on macOS because it requires Linux interfaces.
Live scanner updates passed frame 1500 after sentinel removal. The final Canvas
reuse build has been deployed and passed frame 3500 with advancing diagnostic
updates. Menu screenshots still show oversized/off-model boxes and preview actors;
alignment is NOT fixed or verified. The range transition still needs testing;
the menu path has been requested from the user.

**Other claims requiring correction:** the fork hook explanation for reconnecting
is a hypothesis, not a demonstrated root cause. No device-ban-bypass or complete
anti-cheat-neutralization claim has been verified in this review. The stop
sentinel is separate from `.noscan` and must be considered during diagnosis.

## Latest continuation: restored overlay and visual controls

- The missing overlay was expected while `.noscan` was present for the isolation
  test. That marker is now removed. The root-owned `.s7xk` remains archived,
  not active. The latest source and DEX have been built and deployed.
- The user confirmed one scanner-disabled range attempt crashed, then a later
  launch successfully entered the range. A screenshot confirmed that successful
  range run. This does not prove all range crashes are scanner-related.
- Scanner startup redundantly called `_install_segv_handler()` after JNI_OnLoad
  had already installed it. That overwrote saved previous handlers with our own
  handler, breaking the chain on host threads. The duplicate scanner-thread call
  has been removed. Live stability still needs verification.
- Camera-cache selection now resets when the camera manager changes and retries
  after 45 invalid frames, including transitions within the same UWorld.
- The menu now has Visuals, Colors and Status tabs, saved switches, separate boxes
  and skeleton options, player/bot/teammate/loot HSV colors and line weight.
  Native Android widgets keep this in the existing overlay renderer. The backup
  ImGui example's application menu was a small diagnostics window, not the full
  external settings UI.
- Skeleton rendering uses the existing external reader and its selected 17-joint
  body topology. Parent-index checks match the external renderer. Offscreen box
  bounds no longer suppress potentially visible skeleton segments. No fallback
  joints are synthesized when validated pose data is missing.
- The full synthetic skeleton reader regression passes after correcting its
  outdated long wire-field names to the existing short `bv`/`bc`/`bp` protocol.
  Bone-buffer selection, transforms, parent topology and invalid-pose rejection
  passed. Projection, safe-read and Android UI regressions also pass. The Android
  test verifies tab selection, the skeleton toggle, colors, 2,000 reused frames,
  expiry and nonblocking UI behavior. The separate legacy character-identity
  regression remains a pre-existing failure.
- Menu previews from actual Android rendering are saved in
  `docs/overlay-2026-11-14/menu-visuals.png` and `menu-colors.png`.
- Firing-range path supplied by user: main menu Prepare/Loadout → weapon → Modify
  → Firing Range. Earlier live screenshots still had oversized/off-model menu
  boxes. Do not claim live alignment or complete stability is fixed until tested.

## Architecture

Two-component system:
1. **Internal** (`internal/`): Zygisk module (`loader.cpp`) + injectable .so (`delta_radar_lib.c`) — runs inside the game process. This is the primary component.
2. **External** (`app/`): Android overlay app "Delta Radar" — receives entity data via local socket, draws its own ESP overlay. Currently unused; the internal overlay has replaced it.

### Injection chain
Zygisk `loader.cpp` hooks `postAppSpecialize` → spawns a thread → waits 200ms → `dlopen`s `libue4patch.so` from the game's data dir → `JNI_OnLoad` runs: build field spoofing → signal handler install + watchdog thread → memory masking → anti-cheat neutralization (GOT hooks, SVC NOP, syscall trampoline) → scanner thread (unless `/data/local/tmp/.noscan` sentinel exists).

### Internal ESP overlay (DEX-in-.so)
The ESP overlay runs inside the game process using Android's system overlay windows, not an external app:

1. `O.java` (Java 8 source) is compiled and dexed
2. `d8` produces `classes.dex`
3. A python script converts the DEX to a C hex array → `overlay_dex.h`
4. `delta_radar_lib.c` includes `overlay_dex.h` and loads it at runtime via `InMemoryDexClassLoader`
5. The overlay uses `TYPE_APPLICATION_OVERLAY` windows, which render above everything including UE4's SurfaceView
6. Requires `SYSTEM_ALERT_WINDOW` permission: `adb shell "su -c 'appops set com.proxima.dfm SYSTEM_ALERT_WINDOW allow'"`

**O.java** provides:
- **ESP canvas**: full-screen transparent overlay (`FLAG_NOT_FOCUSABLE | FLAG_NOT_TOUCHABLE`), double-buffered bitmap. API: `b()` begins frame (clears back buffer), `r()/f()/l()/t()` draw rect/fill/line/text, `e()` swaps buffers and invalidates.
- **BubbleView**: draggable green circle ("R" label), tap toggles the menu panel.
- **MenuPanel**: toggle switches for ESP Boxes, Health Bars, Distance, Snap Lines. Diagnostic readout: GWorld address, entity count, FName pool, status, extra line.
- **Double-init guard**: `static volatile boolean initDone` prevents multiple `z()` calls from different threads.

---

## What's working

- **Game launches and stays alive** — no SIGBUS crash, anti-cheat fully neutralized
- **Signal handler protection** — SIGBUS/SIGSEGV/SIGABRT handlers installed; watchdog thread (100ms interval) re-installs them; sigaction/signal GOT entries hooked to block overwrites. CONFIRMED WORKING.
- **Memory masking** — library regions renamed in `/proc/self/maps` to hide our .so
- **Internal ESP overlay** — renders on top via TYPE_APPLICATION_OVERLAY. ESP boxes track entities correctly in firing range lobby with proper aspect ratio. Entity name/type labels (PLR/BOT/LOOT/NPC) displayed above boxes in yellow
- **Floating bubble menu** — draggable, toggles ESP/Health/Distance/Snapline, shows diagnostics
- **Entity detection** — discovers players (via `is_character_class` FName chain 0xDA→0xD8), mannequins/training bots (via capsule half-height fallback for actors without PlayerState), and loot
- **Entity labels** — name and type (PLR/BOT/LOOT/NPC) rendered above each ESP box in yellow
- **W2S projection** — fixed uniform pixel scale for both axes (was using hw for X, hh for Y causing 1.78x horizontal distortion)
- **Position reading** — non-self entities report plaintext positions (enc_handler `FF FF 00` marker present), self entity uses camera fallback
- **World transition freeze prevention** — validates GWorld pointer chain before expensive `scan_for_gworld()` rescan; skips rescan when GWorld is still valid during transitions
- **Stale ESP frame clearing** — when entities disappear (zero_entity_frames == 3), the overlay bitmap is cleared and `_rcam_ok` is reset
- **Build field spoofing** — SERIAL, FINGERPRINT, MODEL, etc. modified via JNI reflection in JNI_OnLoad
- **Identity spoofing GOT hooks** — popen/__system_property_get/fopen in libanogs.so intercepted

---

## What's NOT working / needs investigation

### 3. "Reconnecting" message in game — MEDIUM PRIORITY
**Symptom**: Game shows "reconnecting" constantly.

**Unverified hypothesis**: The `fork()` GOT hook in libanort.so/libanogs.so returns 1 (pretend parent) for every fork call. The game or its network stack may fork subprocesses for network operations, and our stub prevents those from actually executing.

**Fix approach**: Make the fork hook smarter — allow forks that aren't anti-cheat related. Could check the calling library or allow forks from specific code paths.

### 4. Device ban bypass not confirmed — MEDIUM PRIORITY
**delta.spoof** Xposed module was built (`scratchpad/xposed_build/src/com/delta/spoof/DeviceSpoof.java`) but **never tested**. LSPosed is currently DISABLED. The native library's identity spoofing covers libanogs.so's GOT reads but not Java-level identity APIs (TelephonyManager, Settings.Secure, etc.).

To enable:
```bash
adb shell "su -c 'rm /data/adb/modules/zygisk_lsposed/disable'"
# Reboot, then in LSPosed Manager: enable delta.spoof, add com.proxima.dfm scope, reboot again
```

### 5. Root detection telemetry — LOW PRIORITY
Telemetry shows `comm_c_root_info=Supersu`. Shamiko is disabled. Options: re-enable Shamiko (but it blocks Zygisk injection if game is on denylist), or add root-hiding hooks to our native library.

### 6. JNI string obfuscation — LOW PRIORITY
Strings like `InMemoryDexClassLoader`, `ActivityThread`, etc. are visible in the binary via `strings`. Should use volatile XOR key + noinline encoding (per project conventions).

---

## Internal library hooks — detailed (delta_radar_lib.c)

### Anti-cheat neutralization

#### SVC instruction NOP — CONFIRMED WORKING
Scans `libanort.so` text segment for AArch64 instruction sequences:
- `MOV X8, #93` (0xd2800ba8) + `SVC #0` (0xd4000001) — exit
- `MOV X8, #94` (0xd2800bc8) + `SVC #0` — exit_group
- `MOV X8, #129` (0xd2801028) + `SVC #0` — kill
- `MOV X8, #131` (0xd2801068) + `SVC #0` — tgkill

Each SVC instruction is NOPed (replaced with `0xd503201f` = NOP) to prevent the anti-cheat from killing the process via raw syscall.

#### GOT hooks in libanort.so and libanogs.so — CONFIRMED WORKING (with caveats)
Redirects GOT entries for:
- `kill` → stub that returns 0
- `exit` → stub that returns 0
- `syscall` → filter that blocks dangerous syscall numbers
- `fork` → stub that returns 1 (pretend parent) — **suspected contributor; not demonstrated**
- `abort` → stub that returns

The fork stub is too aggressive — it blocks ALL forks including legitimate ones the game needs for networking.

#### Signal handler watchdog — CONFIRMED WORKING
Thread running at 100ms interval that re-installs `SIGBUS/SIGSEGV/SIGABRT` handlers using `sigaction()`. Also hooks `sigaction` (GOT offset 0x52d3e8) and `signal` (GOT offset 0x52d3d0) entries in libanogs.so to block any attempt to overwrite our handlers.

Previous bug: watchdog was created AFTER the noscan sentinel check, so it didn't run when scanner was disabled. Fixed by moving watchdog creation before the check. Previous bug: 5-second interval was too slow, anti-cheat won the race. Fixed by reducing to 100ms.

#### tp_syscall_imp trampoline — STATUS UNCLEAR
Replaces the syscall dispatcher function in libanort.so with a filter trampoline that intercepts `kill` (62), `tgkill` (131), and `exit_group` (94) syscall numbers. Needs verification that the offset is correct for the current game version.

### Identity spoofing

#### Build field spoofing via JNI — WORKING
In `JNI_OnLoad`, uses JNI reflection to modify `android.os.Build` static fields: SERIAL, FINGERPRINT, MODEL, BRAND, DEVICE, BOARD, HARDWARE, MANUFACTURER, PRODUCT, HOST, DISPLAY, ID, TYPE, TAGS. Sets them to a Samsung SM-A525F profile.

#### GOT hooks for identity reads — WORKING
In libanogs.so GOT:
- `popen` → intercepts commands that read device properties
- `__system_property_get` → returns spoofed values for identity-related properties
- `fopen` → intercepts reads of files that expose device identity

#### delta.spoof Xposed module — NOT TESTED
Source: `scratchpad/xposed_build/src/com/delta/spoof/DeviceSpoof.java`
- Persistent ID storage in `/data/data/com.proxima.dfm/.ds_ids`
- Hooks Java-level APIs: TelephonyManager, Settings.Secure.getString, WifiInfo, BluetoothAdapter, MediaDrm, AdvertisingIdClient, NetworkInterface
- Fake profile: Samsung SM-A525F (Galaxy A52), Android 13
- LSPosed is currently DISABLED; module was disabled after game reinstall

### Memory scanner (memscan-legacy.c, included into delta_radar_lib.c)

#### scan_for_gworld()
Scans libUE4.so memory for valid `UWorld` pointer candidates by:
1. Parsing `/proc/self/maps` to find libUE4.so regions
2. Reading memory blocks and checking for pointer patterns that match UWorld structure
3. Validating candidates: checks GameInstance, LocalPlayers, PlayerController, Pawn chain
4. Scoring candidates by `plausible_players` count
5. Calling `keep_gameplay_candidates_only()` to filter

**Known issue**: Heavy `/proc/self/mem` reads can block the scanner thread during world transitions.

#### read_entities_sdk()
Two-phase entity reading:

**Discovery phase** (populates `cached_actor_ptrs`):
1. Roster pawns from GameState→PlayerArray — these are active player pawns
2. Level actor array scan — walks all actors in persistent and streamed levels:
   - Checks `is_character_class()` (FName index 0xDA with parent 0xD8 in UClass hierarchy)
   - Fallback: capsule half-height check (0.4m–3.0m) + PlayerState name + PawnPrivate linkage
   - **New**: actors with valid capsule but NO PlayerState are accepted (catches mannequins/training bots)
   - Loot detection: `loot_kind_from_class()` classifies death boxes, containers, stashes, items

**Read phase** (iterates `cached_actor_ptrs`):
- Validates UClass hasn't changed (actor recycling detection)
- Loot actors: reads position from root component
- Character actors: checks `actor_is_active_participant()` — verifies PlayerState pawn linkage, finished flag, bot flags. Actors WITHOUT PlayerState pass automatically.
- Reads position via `read_actor_position()` with encryption handling
- Filters: skips NaN, out-of-range, origin (0,0,0), too close to origin (<20m), excessive Z delta, duplicate positions
- Reads health via `read_entity_health()` (discovers component offset chain)
- Reads team via native_team from PlayerState
- Self entity: uses camera rotation/position if valid, falls back to root component position

#### is_character_class()
Walks UClass `SuperStruct` chain (up to 24 depth) looking for FName index `0xDA` (ACharacter) whose parent has index `0xD8` (APawn). Returns 1 if found.

**Key finding**: firing range mannequins have class chain starting at `0xD8` (APawn) directly, not through `0xDA` (ACharacter). Chain: `0xD8 → 0x3B → 0x1`. They are APawn subclasses but NOT ACharacter subclasses.

#### actor_is_active_participant()
- No PlayerState → returns 1 (active) — mannequins pass
- Has PlayerState → checks: `PawnPrivate == actor`, `finished == 0`, bot flags bits 1/2/5 clear

#### Camera reading
Uses PlayerCameraManager cache:
1. `discover_camera_caches()` — scans camera manager for cache candidates based on self position
2. `select_advancing_camera_cache()` — picks the cache whose timestamp is advancing
3. Reads POV: location (with `enc_handler_unmanaged()` for decryption), rotation, FOV

**Note**: Camera position sometimes reads as (0,0,0) while rotation/FOV are valid. Fallback: when camera position is zero, `camera_valid` is false, and the self entity is synthesized from PlayerController data (line ~2761 of memscan-legacy.c) with position from root component reads or cached self position.

### ESP overlay (esp_overlay.h)

#### Initialization
- `_ov_init()`: loads DEX from `overlay_dex.h` via InMemoryDexClassLoader, resolves JNI method IDs for all O.java static methods
- `_ov_check_ready()`: polls `O.ok()` to check if the overlay is initialized
- Uses `ActivityThread.currentActivityThread().getApplication().getActivity()` reflection chain to get the Activity for overlay window creation

#### Rendering
- `_ov_draw_esp()`: called each frame when entities exist
  - Checks toggle states via JNI calls to O.java
  - Projects each entity position to screen via `_ov_w2s()` (world-to-screen)
  - Draws: ESP box (red for enemy, green for teammate), entity label (name + type in yellow above box), health bar (left side, green→red gradient), distance text (below box), snap lines (bottom center to entity)
  - Skips self entity and dead entities

#### World-to-screen (_ov_w2s)
Implements UE4 camera projection:
- Computes view matrix from camera yaw/pitch
- Projects world position relative to camera into screen coordinates using FOV-based perspective
- Camera FOV is HORIZONTAL. X scale uses `hw / (dot_f * th)` where `th = tan(halfHFov)`. Y scale uses `hh / (dot_f * tv)` where `tv = th / aspect`. Matches external `ProjectionMath.kt` approach
- Returns false if entity is behind camera

#### Box drawing
Projects both head (`z + halfHeight`) and feet (`z - halfHeight`) separately, draws asymmetric box from head-projection to feet-projection. Width = `boxH * 0.45` for characters. Matches external `OverlayService.kt` / `EnemyLinesView.kt` approach. Label above box, health bar left side, distance below, snap line from screen bottom to entity feet.

### Diagnostic system
- `_diag()`: writes timestamped messages to `/data/data/com.proxima.dfm/cache/d`
- `_ov_update_diag()`: updates overlay menu panel diagnostics (GWorld, entities, FName, status)
- Periodic `fprintf(stderr, ...)` diagnostics go to logcat
- Discovery diagnostics: `[disc]`, `[lvl]`, `[read]` log cached actor counts and filter stage pass rates

---

## Build & deploy

```bash
# DEX build (only needed when O.java changes; run from repo root)
bash internal/native/overlay/build_overlay.sh

# Native build
cd internal/native && bash build_lib.sh

# String leak check (ALWAYS do this after build)
strings libue4patch.so | grep -iE 'serial|imei|proxima|delta|fingerprint|radar'

# Deploy
adb push libue4patch.so /data/local/tmp/
adb shell "su -c 'cp /data/local/tmp/libue4patch.so /data/data/com.proxima.dfm/libue4patch.so \
  && chown u0_a555:u0_a555 /data/data/com.proxima.dfm/libue4patch.so \
  && chmod 755 /data/data/com.proxima.dfm/libue4patch.so \
  && chcon u:object_r:app_data_file:s0:c43,c258,c512,c768 /data/data/com.proxima.dfm/libue4patch.so'"

# Clear diag log and restart
adb shell "su -c 'rm -f /data/data/com.proxima.dfm/cache/d'"
adb shell "am force-stop com.proxima.dfm"
adb shell "am start -n com.proxima.dfm/com.epicgames.ue4.SplashActivity"

# Check diag log
adb shell "su -c 'cat /data/data/com.proxima.dfm/cache/d'"

# Grant overlay permission (one-time after install)
adb shell "su -c 'appops set com.proxima.dfm SYSTEM_ALERT_WINDOW allow'"
```

---

## Device state

- **Device**: OnePlus 5 (ONEPLUS A5000), Android 10, Magisk 28.1
- **PIN**: 3515
- **Game UID**: 10555 (u0_a555)
- **SELinux context**: `u:object_r:app_data_file:s0:c43,c258,c512,c768`
- **Game version**: 1.202.37119.3601
- **Magisk modules ENABLED**: `delta_zygisk` only
- **Magisk modules DISABLED**: lsposed, shamiko, frida, luckypatcher, playintegrityfix, riru, zygisk-assistant, zygisksu, trustusercerts, writable_system, nano-ndk, alterinstaller, xXx
- **Zygisk**: ENABLED
- **LSPosed**: DISABLED

### Key files on device
| Path | Purpose |
|---|---|
| `/data/data/com.proxima.dfm/libue4patch.so` | Our injectable library (deployed, active) |
| `/data/local/tmp/.noscan` | Sentinel: if present, scanner thread doesn't start |
| `/data/data/com.proxima.dfm/cache/d` | Diagnostic log (timestamped messages) |
| `/data/data/com.proxima.dfm/cache/.fids` | Persistent fake identity file |

---

## Key offsets and constants

| Value | Meaning |
|---|---|
| FName 0xDA | ACharacter class |
| FName 0xD8 | APawn class |
| FName chain 0xD8→0x3B→0x1 | Mannequin class hierarchy (APawn, not ACharacter) |
| `is_character_class` | Checks for 0xDA with parent 0xD8 in UClass SuperStruct chain |
| Capsule half-height range | 0.4m–3.0m (pawn detection fallback) |
| `resolved_camera_cache_off` | Dynamic offset into PlayerCameraManager for POV cache |
| GOT: `G_GOT_SIGNAL` 0x52d3d0 | signal() GOT entry in libanogs.so |
| GOT: `G_GOT_SIGACT` 0x52d3e8 | sigaction() GOT entry in libanogs.so |

---

## Wire protocol (C↔Kotlin)

Documented in memory file `project-wire-protocol.md`. Short-key JSON mapping for entity data sent over Unix domain socket from the C scanner to the external Kotlin app. Currently unused since the internal overlay renders directly from the entity array.

---

## Old ImGui implementation reference

Located at `/Volumes/SharedData/Backups/MacBook Pro 16 2019/dev/delta-internal/jni/`:

| Directory | Description |
|---|---|
| `main.cpp` | Current version: hooks `eglSwapBuffersWithDamageKHR` → PostRender vtable hook (index 72) + ProcessEvent vtable hook (index 78). Uses KittyMemory for reads, Dobby for function hooks. Hard-coded offsets (GWorld at base+0x18ebc308). |
| `copy` | Earlier version, same hooks without ImGui |
| `copy-2` | **Most complete**: Vulkan hooks (`vkCreateInstance`, `vkCreateDevice`, `vkGetDeviceQueue`, `vkCreateRenderPass`, `vkCreateDescriptorPool`, `vkAllocateCommandBuffers`, `vkQueuePresentKHR`, `vkEnumeratePhysicalDevices`, `vkQueueSubmit`) + ImGui Vulkan backend rendering in `hooked_PostRender`. Has `early_vulkan_hook()` as `__attribute__((constructor))`. |

These are from an earlier approach that rendered directly into the game's render pipeline via vtable hooks. The current approach uses a system overlay window instead, which is simpler and doesn't require hooking the render pipeline.

### Alternative GL rendering (esp_render.h)
`internal/native/esp_render.h` contains an alternative GL-based ESP renderer that hooks `eglSwapBuffers` and draws directly into the game's GL context. This avoids the `TYPE_APPLICATION_OVERLAY` permission requirement but is more complex and potentially more detectable. Currently unused — the Java overlay approach is active.

---

## Previous session history

### Session 2026-11-12: Anti-cheat crashes
- **Fixed**: SIGBUS crash after ~80-100s — watchdog thread wasn't being created when noscan sentinel was present
- **Fixed**: Watchdog interval too slow (5s → 100ms)
- **Fixed**: Added sigaction/signal GOT hooks in libanogs.so
- **Fixed**: Extended SVC scan to catch kill/tgkill patterns

### Session 2026-11-13–14: ESP overlay
- **Fixed**: Overlay invisible behind UE4's SurfaceView — changed TYPE_APPLICATION_PANEL → TYPE_APPLICATION_OVERLAY
- **Fixed**: Game crash from double overlay init — added initDone guard
- **Fixed**: Stale ESP boxes during world transition — clear bitmap after 3 zero-entity frames
- **Fixed**: Firing range mannequins not detected — accept pawn-shaped actors without PlayerState
- **Fixed**: Camera position (0,0,0) fallback — use root component position when camera_valid is false
- **Added**: Entity name/type labels above ESP boxes
- **Added**: Discovery diagnostics (cached actor counts, filter stage counters)
- **Partially fixed**: Scanner thread hanging during rescan — increased threshold to 300 frames

### Session 2026-11-14: W2S + position decoding + world transitions + lobby freeze
- **Fixed**: W2S FOV interpretation — camera FOV is HORIZONTAL, not vertical. Now uses separate X/Y scales: `th = tan(halfHFov)`, `tv = th / aspect`. Matches external `ProjectionMath.kt`
- **Fixed**: Box sizing — projects head (`z+halfHeight`) and feet (`z-halfHeight`) separately, draws asymmetric box from head to feet projection. Width = `boxH * 0.45`. All decorations (label, snap line, health bar, distance) use `box_top`/`box_bot`/`box_cx`/`box_h` coordinates. Matches external `EnemyLinesView.kt`
- **Fixed**: Lobby freeze regression — `scan_for_gworld()` runs in-process (BUILD_AS_LIBRARY) and takes ~3-17s of heavy memory reads. In lobby (no gameplay world), it looped: scan → 15s sleep → scan. Fix: increased no-world sleep from 15s to 45s; increased rescan trigger threshold from 300 frames (~5s) to 1800 frames (~30s); increased world-transition patience from 900 to 3600 frames
- **Fixed**: Position decoder gate — `position_decoder_discover()` in `position-decoder.h` had `if (!use_vm_readv || !ue4_base) return 0;` which completely blocked the decoder in library builds where `use_vm_readv=0`. Added `#ifndef BUILD_AS_LIBRARY` guard
- **Added**: GEngine chain for instant world retrieval — `discover_engine_from_world()` walks UWorld → GameInstance(+0x190) → OuterPrivate(+0x10) → Engine, validates via Engine+0x1048 == GameInstance. `get_world_slot_via_engine()` reads Engine → GameViewport(+0x9A0) → World(+0x70) for instant world pointer during rescans
- **Added**: World transition freeze prevention — validates GWorld pointer chain before `scan_for_gworld()` rescan. Skips expensive rescan when GWorld is still valid during transitions
- **Investigated**: Position decoder never called (`calls=0`) — non-self entities genuinely have plaintext positions (enc_handler `FF FF 00` marker). Self entity uses camera/PlayerController fallback (line ~2761)
- **Investigated**: Self entity not matched in main loop — `is_self_pawn` requires `actor_ptrs[i] == local_pawn`, but the self pawn isn't in the actor array. Synthesized from PlayerController data via fallback

---

## Priority for next session

1. **Test ESP box sizing in match** — W2S and box projection rewritten to match external version (horizontal FOV, head/feet projection, 0.45 width ratio). Deployed, needs live match testing
2. **Test lobby freeze fix** — scan backoff increased (15s→45s sleep, 300→1800 frame rescan threshold). Initial tests show game loading screen responsive. Needs extended lobby testing
3. **Fix "reconnecting"** — make `fork()` GOT hook smarter (allow non-anti-cheat forks)
4. **Test/enable delta.spoof** for device ban bypass (enable LSPosed first)
5. **Obfuscate JNI strings** in binary (InMemoryDexClassLoader, ActivityThread, etc.)
6. **Position decoder for encrypted entities** — currently unused since all detected entities report plaintext positions. If future game versions encrypt positions, the decoder is ready (gate fix applied) but its hardcoded offsets (accessor at `ue4_base + 0x107bbb8c`, libc build ID, TLS layout) will need updating
