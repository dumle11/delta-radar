# Delta Radar recovery state — 2026-11-02

## Live match encrypted-position audit — 2026-11-03

A live sample confirmed4 encoded bounds/root transforms among29 character
actors. Later fresh GameState/PlayerState-linked samples showed that clear-tag
LastFrame/LastUpdate caches can still contain duplicate underground values;
do not use them as a fallback. The game stayed PID12758 and automatic input
was disabled for the capture, with the visual feed/overlay running.

Readable runtime ARM64 getter snapshots show mode2 + Index!=0xFFFF decoder
dispatch, independently of bEncrypted. The reader now requires an unmanaged
index sentinel plus clear flag for bounds/transforms/camera certification.
Native fixtures cover active indices with clear flags and camera admission;
25 Android tests and the APK build pass. This does not decode positions.
The patch APK is staged under /tmp/delta-live-encryption-evidence/build;
it has not been installed while the user's recording timing is pending.
Decoder dispatch slots are in a write-only VMA; no protected reads were made.

All20 supplied forum screenshots were reviewed, but several are clipped or
blank at16384px. Page16's pointer-encryption/account-flag discussion does not
supply an Android coordinate decoder; its code example is cut off. Detailed
coverage is /tmp/delta-forum-screenshots-review.md and the investigation doc.

## 2026-11-03: ghost-target fix and encryption investigation

The user still reported empty-space aiming after the earlier corrective build.
A live lobby probe reproduced overbroad character classification: the local
Pawn->Actor->Object hierarchy caused unrelated actors to pass. Fixed native
Character->Pawn ancestry, strengthened the PlayerState fallback, and recheck
cached classes every frame. Automatic input now also requires a live possessed
character and coherent plaintext bounds/world-transform positions. Camera drag
setup is optional and has an explicit reset-to-default action.

Visible firing-range dummies were plaintext and ESP-aligned in the same game
process without reboot. Automatic aiming visibly converged on torso targets;
manual and automatic firing produced range hits after fixing the saved fire
point. An80ms stationary fire contact replaces zero-duration taps. These tests
do not validate encrypted multiplayer targets or full-match stability.

All90 supplied forum pages were attempted in a normal browser; only pages
1,2,6,24,25,90 were readable (112 post containers). The remaining84 hit browser
challenges. Do not claim the entire thread was reviewed or that the user's
account is flagged. No verified Android decoder was obtained or integrated.
See docs/AIM_POSITION_INVESTIGATION_2026-11-03.md for evidence and limitations.

## Aim regression correction

The first Aim implementation was reported to rotate toward empty space. It
accepted legacy raw positions, treated missing visibility as eligibility,
and repeatedly injected uncalibrated swipes without observing the result.
That implementation was not validated against a live target before release.

The corrective reader adds `positionPlaintext`, `renderedOnScreen`,
`cameraTimestamp` and `cameraPositionPlaintext`. These are read-only checks of
the matching SDK layouts. Encrypted positions remain undecoded and are excluded
from automatic input. A rendered-mesh flag is only a necessary visibility
filter; it is not an authoritative line-of-sight result. Targeting additionally
requires a current-snapshot entity with known positive health. Feed snapshots
are now copied atomically, and camera angles retain sub-degree precision.

The controller now sends small corrections, waits for an advancing camera
sample and checks that screen error decreases. Wrong or absent response,
large camera movement, zoom changes and a bounded correction count stop it.
These checks have regression coverage, but are not proof of live aim accuracy.

The old discovery loop also missed an active gameplay world. A bounded SDK
pointer-chain check found a world with 427 actors and 12 PlayerStates through
`libUE4 + 0x20AA87A0`. Reading through that slot restored 176 entities. The
reader now prefers that slot after checking the level backlink, camera owner,
replicated player names and actor-array bounds; this no longer depends on a
pawn appearing in the first 256 sampled actor entries. A subsequent world
transition was observed with the game process still alive.

Owned-memory native tests and Android tests pass. Runtime metadata has shown
both excluded encrypted/unrendered actors and eligible rendered, plaintext
actors with positive health. Initial stationary probes sent no gestures while the view was moving or
the player unavailable. A later live test isolated the saved centre/left look
point: a four-pixel drag there changed no camera angle, while the same drag
from the right-hand area increased yaw by roughly 0.44 degrees. Twelve-pixel
right/down probes changed yaw/pitch in the expected directions. Old centre/left
look points now use the measured right-hand area (70%, 55%), and calibration
rejects that inactive area. Pending gesture feedback is checked before another
target is selected; a transient render-flag change cannot silently erase the
in-flight result. The in-app diagnostics have observed a correction reducing
target error. Full live aiming/firing acceptance remains outstanding. Keep this
limitation explicit; encrypted remote positions are still unsupported.

## Later follow-up: missing gameplay world

After installing the unchanged `poc-wip` APK, the user reported no lines,
including bots, despite being spawned. The running scanner executable matched
the bundled legacy reader. Its log repeatedly rejected all discovered world
candidates with `players=0` and waited for an active gameplay world. The
discovery scan samples at most 256 actor entries per candidate and limits
which mappings it searches; neither limitation has been proven to explain
this particular failure.

With explicit approval, an isolated, owned-memory-tested diagnostic checked
the local-player pointer chains of two previously logged candidates using
readable mappings and `process_vm_readv` only. One had no usable game-instance
pointer and the other no usable local-players pointer. Five reads totaling
40 bytes were made. These candidates may have changed since they were logged;
the result does not locate the real gameplay world or establish why it was
missed. The game, app and scanner were running immediately afterward. The
diagnostic executable was removed from the device. No production fix or new
APK resulted, and correct position decryption remains unverified.

The same game and scanner processes later selected a gameplay world with 165
actors and one plausible player, and then reselected it with 213 actors and
two plausible players. Entity counts subsequently rose above 100. The user
reported that all visible players, bots and teammates were aligned at that
time. This explains the return of entities but does not establish why world
discovery previously failed or whether character positions were encrypted.
The installed reader still uses raw bounds and has no verified decoder.

The user approved a separate check of four previously logged actors' bounds
encryption flags. At the time of the check none of those actor root-field
addresses was in a readable mapping, so the diagnostic made zero
`process_vm_readv` calls and obtained no encryption flags. The game, app and
scanner were still running afterward. The diagnostic was removed from the
device. Subsequent logs contained different actor addresses; the unsuccessful
check is not evidence that the game is unencrypted or inaccessible.

## Later follow-up: working friend and scanner identity

The user reports a friend with a OnePlus 5 and the same game/region gets
correct teammate, bot and human positions in every match using `working-v1`.
The friend's Android version and the hashes of the installed APK and running
scanner have not yet been established. This report does not establish whether
the difference is account-related, encryption state, OS, scanner build, or
configuration. The user later reported that the friend is on Android 12.
Only one device was visible through ADB; its reported Android version was 10
(API 29), and its running scanner was the legacy reader. The friend's running
scanner identity has therefore not been independently checked.

On this device, switching the Git checkout to `poc-wip` did not install a new
APK. After the user's restart, the installed APK SHA-256 remained
`31df2e99e91ed671280bcc4d9065edec23684a0b63b43e7ba444a9761acd2080`,
the `working-v1` APK installed during the preceding request. Its scanner
verification expects `/data/local/tmp/delta-radar/scanner-api31-3e77827de53dcacc`
and hash `3e77827de53dcacc6eaac1a9b03174e74fb96a875d9729167d126fb78115c63f`;
that file was absent after restart. The existing legacy scanner file at
`/data/local/tmp/delta_radar_legacy/memscan` instead has SHA-256
`a8323eb1466d5aafc53c60387bad2611e792191723b30fa5f6da9d1f164c3fe4`,
matching the `poc-wip` bundled reader. A process named `memscan` had been
running before the restart, but its executable identity was not verified.
Consequently, testing the `working-v1` APK does not by itself establish that
the `working-v1` decoder scanner ran. This device is Android 10/API 29;
the pinned `working-v1` scanner targets API 31.

With the user's approval, the unchanged `poc-wip` APK was subsequently built,
unit-tested and installed. Its APK SHA-256 is
`96c0af8bf20ccde134a9f9d334e4f1bbb161554c4955a7c4e7465e36256bbffe`;
the installed APK matched, and its bundled scanner hash matched the legacy
reader above. The radar app and scanner were not running during the final
process check; the game was running. No position fix was included.

## Later follow-up: HUD position investigation

The SDK declares a possible read-only position path from
`ADFMCharacter::CachedMarkerManager` to `AGPMarkerManager` and its
`AGPSceneActorReplicator`. The latter contains a character HUD location array
and scene-actor records; teammate records contain UINs and actor pointers.
These declarations do not establish that the fields are populated, current,
correctly matched, or in the coordinate system needed for projection.

An isolated Android diagnostic passed an owned-memory test. With the user's
approval, one bounded `process_vm_readv` attempt was made against readable
mappings only. It stopped before obtaining positions because a required
pointer was unavailable; the initial diagnostic did not identify which
pointer. A second separately approved attempt reached the cached marker
manager and verified its local-character and world links, but its presumed
scene-replicator pointer at `+0x758` was unavailable. The dump may differ
from the live build, or the pointer may not be populated in this context.
Neither attempt obtained HUD locations. The game and existing scanner were
running immediately afterward, which does not establish long-term safety.
The diagnostic remains outside the production reader and APK. No position fix
has been validated, and the previous raw player behavior is unchanged.

A later user-approved readable-memory diagnostic examined all 295 actor
pointers in one persistent-level snapshot, but the scanner's configured
FName-pool interpretation resolved none of 64 observed class names. It
therefore could not establish whether the replicator was present. A separate
approved check matched one of 466 actors by a presumed battlefield-system
pointer whose local controller and pawn agreed with the current world. That
is a structural candidate, not confirmed class identity. Its presumed HUD and
scene-record arrays and the marker manager's teammate array were empty in
both samples, so no teammate coordinates were obtained or validated. The
game and scanner were running immediately after the checks. The limits were
512 actors, 2,500 `process_vm_readv` calls and 128 KiB per diagnostic, with
readable mappings only; the latter check actually used 344 calls and 7,128
bytes. These observations do not establish that the real arrays are empty.

An offline copy of the installed `libUE4.so` had SHA-256
`ae72b80e2993589558fd642e8f0697613f9b8af2b20a421ef079244e5f0b8639`.
Bounded disassembly at the prior decoder accessor and SDK map-icon offsets
showed mostly undecodable instructions in the file; these bytes do not
establish the runtime functions or a usable decoding algorithm. The copied
library was removed after inspection. No decoder or new APK was deployed.

Another user-approved readable-memory probe tested the SDK's distinct
`AGPCharacter::LastFrameWorldPosition` at actor `+0x1d10` alongside the
root bounds origin. One same-team candidate, validated through its
player-state/pawn link and team ID, was found in the first 512 of 700 actors.
Across three one-second samples its bounds handler reported encryption; the
last-frame handler reported unencrypted data but did not change. Relative to
the local plaintext root, the last-frame vector was approximately 9.7 m away
and 9.6 m lower. The match had ended by the time the user's observation was
requested, so actual teammate motion and alignment were not established;
neither plaintext status nor a finite value is proof of a current location.
The game and scanner were running immediately afterward. The probe used 288
reads and 6,472 bytes and remains outside the production APK.

In a new match, the user reported a teammate roughly 30 m away and separately
approved repeating the same diagnostic. One same-team candidate was found in
the first 512 of 556 actors. Its last-frame vector was marked unencrypted and
reported 11.2–11.4 m distance and 10.7 m below the local root; its encrypted
bounds reported 12–20 m distance and 11.3 m below. These estimates do not
agree with the reported distance, although the sampled candidate and user
observation were not independently correlated by name or screenshot. The
game and scanner remained running immediately afterward; the probe used 321
reads and 6,740 bytes.

The user supplied a Windows/x86-64 Tencent China coordinate-decoding post.
It describes finding a transform through a dynamic hash table and executing
modified, copied machine code, using an AVX register-derived key. This is a
testable architectural hypothesis, not evidence that the Android ARM64 build
uses the same table, offsets, key, or slot layout. It also targets
ComponentToWorld rather than the bounds origin currently used by the radar.
The supplied code was not executed, copied into the project, or deployed.
Process-map metadata in the new game process still placed the existing Android
decoder's expected global in an anonymous write-only `.bss` mapping. No
protected memory was read; map metadata does not prove whether another usable
copy of that value exists.

## Later follow-up: stable reader and missing loot

The original executive summary below describes an earlier build. The app now
uses the historical external `memscan-legacy` reader with `process_vm_readv`
by default. The user confirmed that player and bot boxes appeared without the
previous game exit. A full-match stability test has not been completed.

The reader now also recognizes selected loot and death-box classes using native
class indices and parent indices from the SDK, with bounded inheritance walks.
It reads unencrypted component transforms and skips encrypted or invalid ones.
The user observed loot rendering correctly in a match, but the additions and
expanded streamed-level scan have not been comprehensively verified on this
device. The user's existing loot setting is preserved.
Do not copy the newer reader's unverified structural loot heuristics: they
previously misidentified moving pawns, and several offsets disagree with the SDK.

With the user's permission, a short read-only class-table probe failed at the
object-table global because its mapping was write-only. A separate bounded
actor-class sample read 16 actors without read failures, but found no pickup
or death-box class; it did confirm the SDK's `Engine.Actor` index and size.
That single match is insufficient to establish that the dump's loot-class
indices are stable. The game and reader remained alive after these probes.
During a world transition the reader temporarily reported no gameplay world
and later resumed publishing characters.

The user also reported some enemy boxes and lines appearing underground. A
user-approved `process_vm_readv` comparison of three affected remote human
actors showed `RelativeLocation.EncHandler.bEncrypted == 1`; the local
character's flag was zero. Their cached root, mesh and replicated positions
did not establish a correct alternative. These observations do not prove that
every encrypted actor has an invalid cached world position.

The screenshot showed nearby underground player markers, including a teammate
whose game HUD distance disagreed with the overlay. A user-approved bounded
`process_vm_readv` sample found encrypted bounds and encrypted world transforms
for remote characters; a normal quaternion was not sufficient to validate a
transform. The game and reader remained running. An interim fallback omitted
remote characters whose bounds could not be confirmed plaintext, but it hid
player lines. At the user's request, the reader again uses master's raw player
positions; encrypted players may appear underground or elsewhere incorrectly.
The offline
disassembler was blocked by macOS Automation permissions, so position decoding
remains unknown. Loot class indices have not been established as stable for
this game process, and stability of the extra reads is unverified. Avoid
speculative offsets and memory-access methods associated with game exits.

## Android 10 decoder investigation

The user reported that the fail-closed fallback hid all player lines and chose
to investigate the experimental `working-v1` decoder. An isolated API-24 build
started on this Android 10 device in identity-only mode; it was not deployed
as the scanner. The branch expects Android 12 stack/TLS mapping labels, which
are absent here. A prototype using thread syscall metadata to identify the
stack mapping passed three owned-thread TLS tests. With user approval, a
bounded game-thread metadata and `process_vm_readv` diagnostic also found a
matching TLS. Neither result validates decoded positions.

The user then approved one 32-byte read of the decoder-global write-only
mapping through `/proc/<pid>/mem`. `process_vm_readv` could not read that
mapping, while the single protected read succeeded. The game and reader were
running immediately afterward. Later the game process exited at 20:23:28;
Android recorded its death but the available crash buffer did not identify a
cause. The user confirmed the exit was unexpected. The timing does not
establish whether the diagnostic caused the exit. The user requested the
master player-detection behavior instead. Do not restart the game automatically
or repeat protected reads. The decoder remains an isolated prototype and was
not integrated into an APK.

Further isolated work on the decoder found two reproducible emulator defects:

- Refreshing copied instruction pages did not invalidate Unicorn's translated
  code cache. An owned ARM64 fixture continued executing the old instructions
  after a refresh. Explicitly flushing translated blocks fixed that fixture.
- A failed page read was cached as zero-filled data and later returned as a
  successful read. The prototype now rejects that cache entry and retries the
  read on the next request.

The prototype also stops on unsupported emulated syscalls instead of returning
success, and rejects incomplete or timed-out emulated calls. An owned-process
test covers these paths and recovery, and the Android 10 owned-thread TLS tests
still pass. The API-24 prototype builds, but these tests do not demonstrate
correct decryption of a game character. In particular, the prior branch observed
an unsupported KGSL ioctl and had no validated visible-player alignment.
The experimental work is retained in the session workspace, not the APK.
Existing frozen game captures from the branch handoff are not present in this
workspace; do not make further protected game-memory reads without a new,
explicitly approved safety plan.

## Executive summary

The current Delta Radar build is **not a working release**. The Android overlay itself still starts, but its native entity feed is not safe to attach to the live game. Starting the radar launches an external reader; the game then exits. When the reader is absent, the game starts normally.

This document records the complete state so the work can be recovered without repeating destructive or inconclusive experiments.

The two separate problems are:

1. **Entity correctness:** the earlier prototype rendered entities, but mixed player, bot, loot, and dead-body objects and used several stale offsets. This caused moving “loot,” players below the map, stale dead actors, incorrect health, and missing player names.
2. **Runtime stability:** while correcting the scanner, the attachment lifecycle became incompatible with the game. External readers and a manually injected test library caused the game process to terminate cleanly. The current application therefore crashes/exits the game and publishes zero entities.

The second problem must be solved before further drawing/projection work is meaningful.

## Current observed state

- Repository: `/Users/dme/dev/delta-radar`
- Base commit at the start of the latest repair session: `381e876d08aa6328691021e9b98fbcfc747c4834`
- Rooted Android device is reachable through ADB.
- At the time this document was written, the game process was running and no external reader process was running.
- The currently installed radar APK predates the final `vmreadv` source edit and launches `/data/local/tmp/ue_telemetry run` when Start is pressed.
- `/data/local/tmp/ue_telemetry` is the latest native reader binary and supports both `/proc/<pid>/mem` and `process_vm_readv` modes.
- `/data/local/tmp/libdelta_telemetry.so` is an experimental in-process build. It is **not** launched by the Android app. It was injected manually once for diagnosis and must not be considered production code.
- The Android UI reports zero entities because the scanner never reaches a stable verified match feed.

## Files changed after the base commit

Tracked source/runtime files currently modified:

- `android/app/src/main/java/com/deltaradar/app/MainActivity.kt`
- `android/app/src/main/java/com/deltaradar/app/RootUtils.kt`
- `scanner/launch.sh`
- `scanner/launch_early.sh`
- `scanner/memscan.c`
- `scanner/memscan` (rebuilt binary)

Untracked diagnostic build:

- `scanner/libdelta_telemetry.so`

There are also many Gradle build/cache changes in the dirty worktree. They are generated files and were not intentionally edited as source.

Do not blindly reset the whole repository: the worktree already contained user/Claude changes before this repair session.

## What was verified successfully

### Full SDK generation

MobileDumper was repaired and generated a full SDK successfully.

Host artifact:

`/Users/dme/dev/MobileDumper-7/output/com.proxima.dfm_v1.202.37117.5601_20261102-152210.zip`

Device artifact:

`/data/local/tmp/dfm-fixed-pread2/MobileDumper-7/com.proxima.dfm_v1.202.37117.5601_20261102-152210.zip`

Working dump command:

```sh
su -c '/data/local/tmp/MobileDumper-7 -p com.proxima.dfm -o /data/local/tmp/dfm -s -m 2'
```

Important MobileDumper fixes are in:

- `/Users/dme/dev/MobileDumper-7/Dumper/Profile/CustomProfiles/Shared/DeltaForce.h`
- `/Users/dme/dev/MobileDumper-7/Dumper/Memory/MemoryAndroid.cpp`

The existing user change in `Dumper/main_android.cpp` was preserved.

### Verified engine globals and core layouts

These values came from the successful dump and live validation for game version `1.202.37117.5601`:

| Item                                 | Verified value |
| ------------------------------------ | -------------: |
| `GNames` / `FNamePool` module offset |   `0x20744AC0` |
| `GUObjectArray` module offset        |   `0x2076B8F8` |
| `GWorld` module offset               |   `0x20AA87A0` |
| `UObject::Class`                     |          `0x8` |
| `UObject::Outer`                     |         `0x10` |
| `UObject::Flags`                     |         `0x18` |
| `UObject::Name`                      |         `0x1C` |
| `UObject::Index`                     |         `0x24` |
| `ULevel::Actors`                     |         `0x98` |
| `UWorld::PersistentLevel`            |         `0xF8` |
| `UWorld::GameState`                  |        `0x140` |
| `UWorld::Levels`                     |        `0x158` |
| `UWorld::OwningGameInstance`         |        `0x190` |
| `GameInstance::LocalPlayers`         |         `0x38` |
| `GameStateBase::PlayerArray`         |        `0x388` |
| `ProcessEvent` vtable index          |           `69` |
| `ProcessEvent` module offset         |   `0x1089F264` |

`GUObjectArray` is customized/reordered:

- `FUObjectArray::ObjObjects` is at outer `+0x10`.
- Object chunk table is at `GUObjectArray + 0x20`.
- `NumElements` is at `GUObjectArray + 0x14`.
- Elements per chunk: `0x10000`.
- `FUObjectItem` stride: `0x18`.

### FNamePool details

The canonical pool is valid but its container page is mapped write-only. `process_vm_readv` cannot read the block-pointer container, while root `/proc/<pid>/mem` can.

Verified layout:

- Blocks array: pool `+0x38`
- Blocks bit count: `0x12`
- Entry header: `+0x0`, 2 bytes
- Entry string: `+0x2`
- Entry stride: 2
- Length shift: 6
- Wide mask: 1

The entry bytes are encrypted. The first entry was observed as:

```text
2a 01 b1 90 91 9a ...
```

Header `0x012a` gives length 4. Applying the game’s length-derived decryption produces `None`. The matching decryption routine is now in `scanner/memscan.c` as `decrypt_fname_utf8()` and in the Delta Force MobileDumper profile.

This fixed the earlier false “readable alias” pool, which happened to contain plaintext `None` but produced blank/incorrect class names.

### Player/controller/camera offsets

Verified SDK offsets used by the scanner:

| Field                                              |   Offset |
| -------------------------------------------------- | -------: |
| `Controller::PlayerState`                          |  `0x378` |
| `Controller::Pawn`                                 |  `0x3A0` |
| `Controller::Character`                            |  `0x3B0` |
| `PlayerController::AcknowledgedPawn`               |  `0x3F0` |
| `PlayerController::PlayerCameraManager`            |  `0x408` |
| `PlayerController::SpectatorPawn`                  |  `0x698` |
| `PlayerState::PlayerName`                          |  `0x378` |
| `PlayerState::PawnPrivate`                         |  `0x3F8` |
| `PlayerState::bDead`                               |  `0x4AC` |
| `PlayerState::PlayerNamePrivate`                   |  `0x470` |
| `PlayerCameraManager::PCOwner`                     |  `0x370` |
| `PlayerCameraManager::CameraCache`                 |  `0x3E0` |
| `PlayerCameraManager::ViewTarget`                  | `0x1880` |
| `PlayerCameraManager::CameraCachePrivate`          | `0x2D60` |
| `PlayerCameraManager::LastFrameCameraCachePrivate` | `0x37B0` |

Important remaining correction: the scanner previously discovered and used manager `+0x37B0`. The SDK identifies that as the **last-frame** cache. The current-frame cache is `+0x2D60` and should be preferred after runtime stability is restored.

### Health/death offsets

Verified current SDK offsets:

| Field                          |   Offset |
| ------------------------------ | -------: |
| Character health component     | `0x10B8` |
| Health component attribute set |  `0x280` |
| Character last-is-dead flag    |  `0xE35` |
| PlayerState dead flag          |  `0x4AC` |

Live layout discovery previously found the same `+0x10B8 -> +0x280` health chain and read valid `100/100` health. Older fallback offsets were the reason dead bots and `0/0` health objects remained visible.

### Position data

The live character root position at root component `+0x148` tracked actors correctly in the same UE world coordinate system as the camera.

The previously tested `+0x164` value was not the correct live position for this build. It appeared to overlap quaternion/shifted transform data in some objects.

### Authoritative match roster

`UGPBattleFieldSystem` exposed the authoritative character roster:

- Local controller: `+0x88`
- Local pawn: `+0x90`
- `CurrentClientAllCharacters`: `+0x110`

A prior live capture produced an authoritative roster of 13 actors. This is preferable to scanning every actor and guessing by shape.

### Loot/dead-body classification

The scanner was changed to use reflected inheritance/class names before byte heuristics:

- `InventoryPickup_DeadBody` -> dead-body loot
- `SingleItemContainer` / `ChangeableContainer` -> container
- `InventoryPickup_Container` / `OpenBox` -> box/container
- `InventoryPickup` / `PickupBase` / `PlunderPickup` -> pickup

Structural pawn checks were added so pawn-shaped actors cannot fall through to loot heuristics.

Verified dead-body fields:

| Field                   |   Offset |
| ----------------------- | -------: |
| `PlayerName_Buffer`     | `0x2638` |
| `CampId`                | `0x2670` |
| `DeadbodyCurrency`      | `0x2678` |
| Mesh                    | `0x2688` |
| `bIsAI`                 | `0x277A` |
| `LootingCharacterOwner` | `0x2780` |
| `bLooted`               | `0x2790` |

These findings explain the reported moving “loot box”: the earlier fallback classifier accepted character-shaped objects as loot when FName resolution failed.

## Android UI work that built successfully

Before runtime attachment failed, the following Android-side work compiled and tests passed:

- Per-entity settings for player, teammate, bot, and loot visuals
- Loot-specific display mode, color, and feature controls
- Independent line/box/name/distance/health settings by entity category
- Overlay filtering of dead entities and known zero-health entities
- Projection/rendering code and unit tests

Build verification command:

```sh
./android/gradlew -p android testDebugUnitTest assembleDebug
```

This command completed successfully multiple times. It only verifies Android code and projection math; it does not verify a stable game feed.

## Crash/termination investigation

The game exits normally without the radar reader. Starting the reader or injecting the test library causes the game process to disappear. Android recorded `am_proc_died` and “proc died without state saved,” but no native tombstone or crash-buffer record for these exits. That is consistent with a clean/self termination or integrity shutdown, not a segmentation fault in the game’s renderer.

Observed experiments:

| Experiment                                                                                  | Result                                         |
| ------------------------------------------------------------------------------------------- | ---------------------------------------------- |
| Game with no reader                                                                         | Stable in lobby                                |
| External binary named `memscan`, `process_vm_readv`                                         | Game exited                                    |
| External binary named `memscan`, `/proc/<pid>/mem`                                          | Game exited                                    |
| Same reader renamed `ue_telemetry`, `/proc/<pid>/mem`, lobby                                | Stable for the short lobby test                |
| `ue_telemetry` present through match loading                                                | Game reached 93% loading, then exited          |
| `ue_telemetry`, `process_vm_readv`                                                          | Game exited                                    |
| MobileDumper, `/proc/<pid>/mem`, 10-second lobby probe                                      | Game remained alive during the short probe     |
| `libdelta_telemetry.so` injected with KittyInjector using memfd, hidden maps, hidden solist | Injection succeeded; game later exited cleanly |

The manual injection command used for the last diagnostic was:

```sh
/data/local/tmp/KittyInjector \
  -pkg com.proxima.dfm \
  -lib /data/local/tmp/libdelta_telemetry.so \
  -dl_memfd -hide_maps -hide_solist
```

The injector reported success. The game later terminated without a tombstone. This experiment should **not** be repeated as a normal launch path.

The evidence strongly suggests the current game integrity layer detects late attachment/injection or the reader’s ongoing access pattern. It does not prove which individual signal is used.

## Why the current build reports zero entities

Several safety gates were added to prevent lobby preview operators from being treated as match players:

- The exact static `GWorld` is checked instead of broad memory scans.
- A match requires `GameState::PlayerArray` to contain a replicated named `PlayerState`.
- Preview worlds return an empty snapshot.
- Broad compatibility scans are disabled in continuous mode.

Those gates are directionally correct, but the game terminates before a stable match feed can be verified. Therefore the Android listener receives no verified snapshot and displays zero entities.

The earlier prototype displayed something because it accepted looser actor candidates. Those candidates included false players and false loot and are the source of many of the reported rendering bugs. Simply removing the gates would restore incorrect boxes, not a correct implementation.

## Performance/crash fixes already present in source

`scanner/memscan.c` currently includes:

- Exact static globals instead of launch-time global scans
- No broad FNamePool scan in continuous mode
- No broad `GWorld` scan in continuous mode
- Front-end match gate before camera, roster, health, and loot traversal
- 2 Hz polling outside a verified match and 30 Hz in a match
- Cached FName block pointers
- Immediate closing of the exceptional `/proc/<pid>/mem` descriptor in `vmreadv` mode
- Process/world cache invalidation on PID changes
- Reflected class/inheritance checks for players and loot

These reduce load, but they did not resolve the integrity-triggered termination.

## Experimental in-process build

`scanner/memscan.c` can now compile with `-DIN_PROCESS`. It starts the same scanner loop from a constructor thread. The build command used was:

```sh
$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang \
  -O2 -Wall -Wextra -Werror -fPIC -shared -DIN_PROCESS \
  scanner/memscan.c -o scanner/libdelta_telemetry.so -lm -pthread
```

This library is diagnostic only. Late injection was detected or otherwise followed by a clean game shutdown. It is not wired into the Android application and should not be shipped.

REA was also used to inspect `/data/local/tmp/libdme_scanner.so`, an older on-device library. It contains strings such as `dMeScanner library loaded` and exports `find_entity_lists()`, but there is no source for it in this repository and it was not established as the feed used by the current Android overlay. It should not be treated as a known-good replacement without a separate controlled analysis.

## Safest immediate recovery options

### Option A — restore game stability first

Disable automatic native-reader startup in `MainActivity.toggleRadar()` and run only the Android overlay/listener. This will leave entity count at zero but will stop the radar UI from terminating the game. This is the safest user-facing interim build.

The Start path to disable is the `scanner-launch` thread in:

`android/app/src/main/java/com/deltaradar/app/MainActivity.kt`

### Option B — return source files to the start-of-session baseline

This restores the six tracked files to commit `381e876`, but does **not** guarantee correct entities because that baseline already had the reported classification/rendering bugs.

Back up the patch first:

```sh
git diff -- \
  android/app/src/main/java/com/deltaradar/app/MainActivity.kt \
  android/app/src/main/java/com/deltaradar/app/RootUtils.kt \
  scanner/launch.sh scanner/launch_early.sh scanner/memscan.c scanner/memscan \
  > /tmp/delta-radar-repair.patch
```

Then restore only those paths, not the whole dirty worktree:

```sh
git restore --source=381e876d08aa6328691021e9b98fbcfc747c4834 -- \
  android/app/src/main/java/com/deltaradar/app/MainActivity.kt \
  android/app/src/main/java/com/deltaradar/app/RootUtils.kt \
  scanner/launch.sh scanner/launch_early.sh scanner/memscan.c scanner/memscan
```

Remove the untracked experimental library only after preserving it if desired:

```sh
rm scanner/libdelta_telemetry.so
```

No rollback command was executed while writing this document.

### Option C — continue from the verified SDK work

For a legitimate controlled test build, the recommended order is:

1. Establish an authorized, stable telemetry path before touching drawing code.
2. Require exact reflected class/inheritance identity; do not re-enable byte-only loot fallbacks for pawn-shaped actors.
3. Use `UGPBattleFieldSystem::CurrentClientAllCharacters` as the primary character roster.
4. Use exact `PlayerState` and health/death offsets above.
5. Prefer `CameraCachePrivate +0x2D60`, not last-frame cache `+0x37B0`.
6. Publish a generation/match ID with each snapshot so stale actors are cleared on world changes.
7. Only after the feed is stable, validate boxes, feet/head projection, zoom/FOV, health, death, loot, and minimize/resume behavior.

## Suggested acceptance test for the next implementation

A build is not ready until one complete bot match passes all of these checks:

- Game remains alive from lobby through loading, spawn, death/spectate, respawn, minimize/resume, and match end.
- Exactly one local player is identified.
- Players, teammates, bots, and loot have mutually exclusive classifications.
- A moving pawn can never be emitted as loot.
- Dead players/bots are removed immediately or converted only to an exact dead-body loot actor.
- Health is nonnegative, bounded by max health, and reaches zero on death.
- Player names come from current `PlayerState`, not stale actor memory.
- Positions update continuously and do not collapse below the map.
- Boxes contain feet and head on slopes and while zooming.
- Current camera data uses `+0x2D60`; projection remains stable during pitch/yaw/FOV changes.
- Minimize/background clears or hides the overlay.
- No old-match entity remains after world/PID/generation changes.
- Reader and overlay CPU usage are measured in lobby and match.

## Build and diagnostic commands

Native compile:

```sh
NDK_CLANG="$HOME/Library/Android/sdk/ndk/30.0.14904198/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang"
"$NDK_CLANG" -O2 -Wall -Wextra -Werror scanner/memscan.c -o scanner/memscan -lm
```

Android verification:

```sh
./android/gradlew -p android testDebugUnitTest assembleDebug
```

Whitespace verification:

```sh
git diff --check -- scanner android/app/src/main
```

Runtime status without starting anything:

```sh
adb shell 'pidof com.proxima.dfm; pidof ue_telemetry; pidof memscan'
```

## Final handoff note

The SDK/offset/FName/object-classification findings are valuable and reproducible. The runtime delivery path is the unresolved blocker. The current radar application should not be presented as working, and the experimental injector should not be enabled from the UI. Restore a stable launch path first, then reintroduce the verified entity logic behind an end-to-end match acceptance test.
