# Selective poc-wip integration — 2026-11-04

Source reviewed: `origin/poc-wip` at `ea410c5`. Integrated into the existing
`codex/audit-cleanup` working tree, preserving its uncommitted startup work and
cancelled-feature cleanup. No branch switch, merge, device access, installation,
game launch or live test was performed.

## Startup/recovery retained

The app still verifies the deployed executable hash, project/API identity,
source hash and revision before launch. The foreground service owns startup;
Activity recreation cannot create an independent reader. Each launch has a
unique lease and PID file. Cancellation revokes the lease immediately, including
when a root command has an uncertain outcome. Cleanup checks both the executable
and exact session argument before signalling a process. Root commands and their
output drains have deadlines. Recovery requires a definite exit or changed PID
identity and allows three recoveries per minute; uncertain launches are never
repeated automatically. Explicit Stop clears the durable requested state.

The independent camera sampler also checks the lease. Its lease-check cache is
thread-local, so it neither races the entity reader nor waits for that reader's
loading/rescan delays before noticing cancellation.

## Integrated changes

1. Health reads validate actor ownership of both component and attribute set on
   every read. A component's design maximum is a shape check, not the runtime
   maximum. Failed discovery can retry after 250 ms; sibling classes can reuse a
   currently validated layout. Unknown health remains unknown.
2. Remote PlayerState-backed pawns must match their active pawn backlink and be
   unfinished, active and nonspectating. State-less AI and the local camera anchor
   retain their existing handling. Active GameState roster pawns are merged on
   every update. Validated level arrays are read beyond the former 2,000-slot
   cutoff, within the existing 50,000-slot sanity bound and scratch buffer.
3. Verified component-space animated poses replace the guessed bone map and
   schematic fallback. The latest POC source has **17** body joints; older POC
   documents saying 19 describe an earlier revision. Mesh ownership, transform
   shape, active pose buffer, hierarchy, code profile and repeated reads are
   checked. Unknown rigs omit poses. Projection handles roll and offscreen joints;
   boxes, health bars and labels use measured pose bounds when available.
4. A separate bounded abstract socket transports coherent camera samples. The
   app requires the camera connection's peer PID to match the entity connection
   and checks game PID/world identity. Frozen samples expire at 100 ms. Native
   CLOCK_BOOTTIME and Android elapsedRealtime carry sample ages; wall-clock
   changes do not rejuvenate data. Entity frames remain complete, bounded,
   atomically validated snapshots; missing actors disappear immediately.
5. Projection runs on Choreographer callbacks and publishes on the main thread
   before traversal, preserving Hide/Show/destroy generation checks and one
   conflated pending request. Independent camera advances can reproject the same
   entity frame. Validated world JSON is read without serializing all bones on
   each projection; public snapshots remain defensive copies. Short world-space
   motion compensation is capped at 75 ms; combat samples over 150 ms old are
   omitted. Original emulator results retain their actual capture timestamp.
6. The POC's direct software bounds decoder is preferred. The existing emulator
   remains a fallback, and the separate loot transform/value path is preserved.
   New wire fields coexist with the previous protocol. The app accepts legacy
   16-joint arrays but only draws poses carrying a validated parent hierarchy.

Aim/auto-shoot, auto-pickup, POC launcher/replacement scripts, bundled opaque
binaries, generated build caches and the POC manifest were not imported. Valuable
loot filtering, container value fields, explicit deployment and runtime controls
remain in place.

## Android 12 decoder profile

The POC's Android 10 profile is retained with its exact ELF build ID checks.
The saved OnePlus 7T libc at ignored `evidence/local/session/libc.so` has build ID
`bbbdeb7c87c74f1491f92c6e605095b0`, Android API 31. Its symbol table and offline
ARM64 disassembly establish these additional exact-build offsets:

| Field | Offset / evidence |
| --- | --- |
| Readable build ID | `+0x340` |
| `pthread_getspecific` | `+0xdf684` |
| `g_thread_list` | `+0xf25c8` |
| `key_map` | `+0xf4618`, generation/value entries 16 bytes apart |
| thread ID | `pthread_internal + 0x10` |
| bionic TLS storage | `pthread_internal + 0x2f8` |

`pthread_create + 0x180` stores the bionic TLS pointer at `thread+0x2f8`;
`pthread_getspecific` reads the same storage through `TP-8`, checks the key's odd
generation against the per-thread entry and reads its value at entry+8. Runtime
code checks the readable build ID and decoder callback address; it does not read
execute-only libc code pages. Unknown libc/provider/hardware paths fail closed
for the direct decoder. Arithmetic fixtures plus a sparse synthetic address
space exercise both profiles, complete root/mesh lookups, mismatched generations,
foreign ownership, changing records and unknown-build rejection.

This establishes an offline profile implementation, **not live OnePlus 7T
compatibility, alignment, improved visible latency or full-match stability**.
The existing live-testing halt remains in force. The deployment pin remains at
the previously reviewed scanner until explicit candidate activation.

## Validation and artifacts

Reproduce host checks with Java 17:

```sh
./android/gradlew -p android testDebugUnitTest assembleDebug assembleDebugAndroidTest lintDebug --console=plain --max-workers=2
python3 scanner/tests/run.py --output scanner/build/poc-integration/native-tests
python3 scanner/tools/build_scanner.py --ndk /path/to/linux/ndk --output scanner/build/poc-integration/api31
```

Validation: **63 JVM tests passed**, **14 native ASan/UBSan programs passed**,
Android app/instrumentation APKs built, and lint reported **zero errors, 145
warnings**. The final expanded health/roster fixture was also rerun individually.

An optional `-PscannerDeploymentFile=/absolute/path/to/candidate.properties`
build setting produces a matching candidate APK without changing the ordinary
pin. The archived `poc-integration-candidate-debug.apk` targets the archived
scanner and its hash/source identity constants were checked in the APK dex.
The normal build was then restored and rechecked against the unchanged pin.
Neither APK nor scanner was installed. Candidate identities are not a claim of
live acceptance.

Exact counts, hashes and tool reports are retained in ignored
`evidence/local/session/poc-integration-20261104/validation.json`. Native build
identity and all input hashes are in `scanner/build/poc-integration/api31/manifest.json`.
The pre-integration dirty-tree patch is retained alongside the validation record.


## Installation and container-decoder review

After the offline checks above, the user authorized installation for manual
verification on 2026-11-04. Installed APK SHA-256:
`e8775b8dc560c6e3b1d070ac5fb86e8b7ec0f62a4b628864534d13698ab4192c`.
Native scanner SHA-256:
`778232f8016817b5d59548562556d60d7258ca661dcb482e810b70b4a4cb1aba`.
The installed files and native `--identity` match the candidate manifest. The
normal deployment properties now pin this pair. Visual preferences were
preserved; the previous APK is backed up. Normal Stop cleared the old enabled
request before replacement. No app/scanner was started by the agent and game
PID stayed unchanged. Exact proof is in ignored
`evidence/local/session/poc-integration-20261104/install-20261104/installation.json`.
This supersedes the earlier not-installed/pin-unchanged status; live performance
and decoder compatibility still await the user's test.

The branch's `position_decode_component` looks up type-45 records for a
component's bounds origin at `+0x148`, returning three coordinates.
`position_decode_actor` additionally requires matching character root/mesh
bounds. It does not decode inventory arrays, catalog IDs, counts or loot tables.
The packaged POC reader's loot branch uses `read_plain_world_position` and does
not invoke this decoder. A lower-level bounds decoder might eventually help
position a container whose own bounds use the same verified scheme, but that
would require container-specific validation and does not expose its contents.

Our `loot_value.h` already reads item arrays and follows validated linked
pickups/open-box/cache-spawn actors. The earlier matched safe experiment found
zero readable safe items in the checked native and complete Nearby item-map
sources before search, then three matching catalog IDs afterward. That is not
proof that all client sources are empty, or of when the server spawns loot;
it supplies no evidence of an encrypted contents list this decoder can unlock.
No new game-memory capture or container action was performed for this review.
