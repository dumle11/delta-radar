# Delta Radar recovery state — 2026-11-02

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
