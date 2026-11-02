# Verified scanner deployment and startup — 2026-11-02

Historical baseline: the current SDK repair changes offsets, decoding and binary
identity. See [the current handoff](LIVE_SDK_HANDOFF.md) and
scanner/deployment.properties for the installed version.

## Identity and reproduction

Base repository revision: `a448200d0e645dab34c920ae3a4f7e8f14ef7d0e` plus
uncommitted changes. Previous baseline fixes and regression tests are preserved.
The pre-milestone uncommitted source checkpoint is in ignored local evidence.

Scanner source SHA-256:
`274f08d538a1d3e389e26b6371e6274f71a4a9e0d958125ef95bf4ecd7ca2dc6`.

Built binary SHA-256:
`dfbe410b53e5ccfbab673314dfe62dc385a3af0c112f30db8fb509df9efb4276`.

Deployment:
`/data/local/tmp/delta-radar/scanner-api31-dfbe410b53e5ccfb`.

- Architecture/target: ARM64, `aarch64-linux-android31` (Android 12/API 31).
- NDK: `27.1.12297006` / r27b; Android clang 18.0.2.
- Dynamically linked PIE, `/system/bin/linker64`; ELF Android note reports API 31.
- Flags include `-std=gnu11 -O2 -Wall -Wextra -Werror -fPIE -pie`, stack
  protection, fortify, RELRO/NOW and stable build ID.
- Source revision and source hash are embedded; `--identity` exits before
  looking up the game or reading its memory.
- Two independent output-directory builds compared byte-for-byte identical.
- `--identity` ran successfully on the actual API-31 phone. This establishes
  loader/identity compatibility; it is not proof of engine-layout compatibility.
- Existing `/data/local/tmp/memscan` remained unchanged at SHA-256
  `00e46f0edb67f01a2dd8b4bdd566465fa80a2d0fe5d33c1caaa7fcb7ccd8a0ec`.
  Neither it nor either checked-in precompiled scanner is used by the new flow.

Reproduce with an installed NDK (do not substitute a newer minimum API):

```sh
python3 scanner/tools/build_scanner.py --ndk "$ANDROID_NDK_HOME" --pin-app
python3 scanner/tools/build_scanner.py --ndk "$ANDROID_NDK_HOME" --output scanner/build/repro-check
cmp scanner/build/api31/delta-radar-scanner-api31 scanner/build/repro-check/delta-radar-scanner-api31
```

The script records exact command/toolchain, source revision/hash, scanner diff
hash, build-script hash and output hash in ignored `scanner/build/api31/manifest.json`.
It saves the scanner diff alongside it. `scanner/deployment.properties` is the
reviewed app input pinning the path/hash/revision/source identity; it contains no
host-specific path. Gradle emits BuildConfig constants from that pin.

Deploy without starting a scan:

```sh
python3 scanner/tools/deploy_scanner.py --serial DEVICE_SERIAL \
  --manifest scanner/build/api31/manifest.json \
  --evidence evidence/local/scanner-milestone
```

Deployment refuses mismatching content at the destination, verifies the device
hash and identity, and checks preservation of the old executable. It never
launches a scan, game, dumper, debugger or injection tool. The content-addressed
binary is root-owned and mode 0555.

## Changes to native source

Only build identity, app-session lifetime checks and isolation of the world-cache
path were added. Actor selection, transform/health/team/name offsets, projection
geometry and existing filters were not changed.

An optional app-owned `session=` lease is checked before startup and periodically
before memory reads. Removing the lease or losing the owning app process cancels reads even if a root launch
result was delayed or uncertain. A missing-lease device test returned exit 125
and `Session cancelled before startup`, without a game PID lookup/read.

The old shell launchers now fail closed. They no longer force-stop the game,
set debugger gates, wait indefinitely, or launch the unidentified `memscan`.
They are not an alternative deployment path for this milestone.

## Start/Stop ownership and failure behavior

- Opening/recreating MainActivity does not start the scanner automatically.
- Explicit Start launches the local services and verifies root, the exact
  executable hash, embedded source identity and listener readiness.
- Start does not launch or restart the game. If the game is absent, the launch
  command fails; the user controls gameplay. No early-attach/protection workaround
  is attempted if a normally authorized read is denied.
- Each start has a unique native-readable lease plus PID/log files under the
  app's private `files/scanner-sessions/`. Root dispatch checks the lease before
  launch. Native code independently checks it before doing work.
- One process per root command, a three-second default deadline, cancellable
  dispatch and bounded output collection replace the permanent root shell.
  Failed/uncertain commands are not automatically repeated.
- Stop immediately revokes that lease and cancels pending dispatch. Cleanup is
  claimed only once. It checks **both** exact executable path and session
  argument before sending TERM to the recorded PID. There is no global killall.
- Activity destruction cancels its pending start. A completed start remains
  owned by the foreground feed service; destruction of that owner cancels it.
  Feed status continues to require fresh validated camera data, independently
  of root checks or a process PID.
- A root command timeout reports an uncertain outcome; it is not a guarantee
  that every possible remote descendant died. The revoked native lease and
  owned-process cleanup are the independent controls for that case.

## Diagnostic evidence and boundaries

Useful `/tmp` baseline logs, inventories and screenshots were copied to durable
`evidence/local/2026-11-02-baseline/`. The pre-milestone source checkpoint is at
`evidence/local/pre-scanner-milestone/`. Current deployment evidence is at
`evidence/local/scanner-milestone/`. These locations are ignored because raw
frames/screenshots may contain player identifiers, addresses or other private
data. No generated binary or memory dump is proposed for a commit.

For a bounded, opt-in debug capture, create the app-private
`files/capture-feed.enabled` marker. The debug app then atomically replaces
`latest-scanner-frame.json` and `latest-overlay-frame.json` at most twice per
second. The first records raw received input, accepted snapshot, timestamp,
sequence, and actual socket peer PID/UID; the second records the camera and
projected/rejected entity decisions. This is local file output, not a network
listener. Remove the marker after a capture. Correlate socket peer PID with the
owned scanner PID; do not confuse a synthetic test client with game data.

The distinct validation layers are:

1. Preserved synthetic service/socket regressions from the completed baseline.
2. New command/session tests with deliberately delayed mock verification.
3. Actual WindowManager overlays driven by namespaced synthetic test entities,
   with Show/Hide/Show/expiry screenshots; settings restored after the test.
4. A verified scanner reading the installed game, producing fresh frames and
   corresponding overlay evidence. Only this fourth layer can support claims
   about visible players, positions, death/despawn or zoom.

## Validation status

- Native API-31 build/identity, reproduction comparison and cancelled-lease test:
  passed on the phone without starting a scan.
- JVM: **25 tests passed**, including all 19 preserved tests and six new
  deadline/cancellation/no-retry tests.
- App/test build and lint succeeded; latest exact results and APK identities
  are in local `startup-validation.json`.
- Aleks approved a safe break for app, actual overlay and verified-scanner tests.
  All **8 Android tests passed**: five preserved socket/service regressions,
  two delayed-start cancellation tests and one real WindowManager overlay test.
  Cancellation tests invoke Stop/the activity's pending-start cancellation path
  with delayed verification; they do not physically destroy an activity.
- Actual Show/Hide/Show/expiry checks use a continuous, clearly named TEST FIXTURE
  and restore prior player flags/draw mode/colour. The final 8-test run passed
  in 8.2 seconds and checks marker pixels in the composed screen, avoiding a
  false pass based solely on View visibility or a draw callback. These are real windows with synthetic
  data, not evidence of a live player or correct game positions.
- The first app Start launched owned scanner PID **18314**, with owner app PID
  **17371**, from the exact pinned path/hash above. Its log confirms ordinary
  `process_vm_readv` access to game PID **3746**, UE4 mapping discovery and a
  candidate FName pool. It reported no active gameplay world while the game was
  in the lobby/download screen. No fresh valid gameplay frame was received;
  the app correctly remained waiting with zero entities.
- App Stop removed that owned process. The game remained PID 3746 and the
  original executable hash stayed unchanged. No game restart was performed.
- **Live baseline failed before the feed.** Two loaded matches were observed,
  with screen captures at timers 29:25 and 29:36. Scanner PID 21959 remained on
  the pinned executable and repeatedly reported no active gameplay world. Its
  complete log contains zero `Candidate #` entries, no running-world selection
  and no emitted frame. Debug capture files were absent, not stale test data.
- Four bounded 16-byte `process_vm_readv` reads succeeded during the second
  match at the mapped UE4 ELF base and three writable-data addresses (errno 0;
  ELF magic verified at the base). This rules out denied access at those sampled
  addresses at that time; it does not establish that every scanner read or
  engine-layout assumption is valid. The temporary API31 read-only probe's
  source, command and hashes are in local evidence; it recorded counts/errno,
  no raw bytes, and was removed from the device afterward.
- Source tracing locates the present failure before `post_to_radar`: discovery
  scans selected writable UE4/nearby mappings, follows fixed PersistentLevel and
  actor-array layouts, and logs candidates only at score >=70. No candidate
  reached that logged threshold. The exact rejected field, scan coverage issue
  or read failure is **not established** by existing diagnostics. Name decoding
  is a separate research hypothesis, not a confirmed explanation for this failure.
- Real-feed disconnect/recovery, loading/match-exit **clearing of previously
  displayed live markers**, death/despawn and zoom alignment remain unverified.
  Loading, gameplay, death and lobby screens were observed with no scanner
  markers, but an always-empty feed cannot validate transition clearing.
- Production overlay Hide/Show actions also removed/restored the bubble over
  the running game's lobby, without restarting it. Actual marker pixels were
  verified only with the synthetic fixture, as explicitly distinguished above.
- Latest installed app is version 1.0/code 1/target 34, APK SHA-256
  `f7e58eeb6605fba9f25071804ea4e88b3a1485fa77ea609c00273e697872deea`.
- App restart launched scanner **21959**, owner **21867**, session
  `e345c64a-ea23-429b-87df-c933e3a4bef3`, from the same pinned path. This
  session produced no frame across both loaded matches and was stopped by
  destroying the owning feed service; its lease and process disappeared. A prior intermediate session (PID 21349) was
  also found in the private logs; it is no longer running. The documented
  intentional Start/Stop observation is PID 18314.
- An initial candidate `scanner-api31-49ecf5d500959827` was deployed only for
  identity/lease checks, then superseded by the final owner-PID guarded build.
  It was not used to read the game. The app pins only the final dfbe410b build.
- MobileDumper-7 remains source-only research: see [assessment](SCANNER_RESEARCH.md).

## Final lobby Start/Stop verification

After Aleks confirmed returning to the lobby, the actual app button was tested:
Start (scanner PID **23414**, session `270a90de-442d-4d96-8942-b1413371b53d`)
→ Stop → Start (PID **23672**, session `18a8b784-30d7-4498-8c83-b5b81f17c4b9`)
→ Stop. Both used the exact pinned executable with owner app PID 21867.
Each Stop removed its lease and process, and closed `delta_radar_feed`.
Both starts correctly showed waiting/zero entities rather than connected.
No socket collision or launch retry was observed. Screens and process records
are `final-cycle-*` in local evidence. Capture was disabled afterward; app was
returned to the background. Final feed/overlay/scanner are inactive. Game PID
remained **3746**, and the unknown original executable's hash is unchanged.

The deployment/startup repair is verified within these boundaries. The requested
**live end-to-end baseline is not achieved**. Build success and synthetic overlay
success do not change that result.

## Files changed in this milestone

- Build/deploy identity: `scanner/tools/build_scanner.py`,
  `scanner/tools/deploy_scanner.py`, `scanner/deployment.properties`,
  `scanner/memscan.c`, `android/app/build.gradle.kts`; fail-closed old launchers.
- Startup/ownership: `CommandRunner.kt`, `RootUtils.kt`, `ScannerSession.kt`,
  `ScannerRuntime.kt`, `MainActivity.kt`; focused command/session regression tests.
- Local opt-in evidence: `CaptureDiagnostics.kt`, feed/overlay integration and
  read-only draw diagnostics in `EnemyLinesView.kt`; real-window device test.
- Reports/research, README, evidence exclusions and project memory/profile.
  Prior baseline source/tests remain preserved. Category controls, actor/position
  logic and projection geometry were not redesigned. No commits or pushes.

## Remaining source-accuracy questions

The existing hard-coded object/name/heap/camera/roster assumptions, zero-health
filter disagreement, synthetic-self fallback, old-snapshot replay and narrow-zoom
clamp remain. No vertical correction, fabricated enemy, or new altitude filter
was introduced. Once live lifecycle/data are established, compare raw input,
accepted state, projected decisions and screen captures before selecting the
next narrowly scoped repair.

## Next recommended device test

Add narrowly scoped diagnostic counters for discovery scan coverage, read errors
and world/level/actor-array rejection stages, without changing acceptance rules
or importing dumper code. Build and pin that diagnostic scanner reproducibly,
then capture one stationary loaded-match interval at an agreed break. First
identify why no world candidate reaches score 70; only after fresh valid frames
exist should visible entities be compared with raw, accepted and projected data.
No game restart, suspension, injection or bypass is required by this recommendation.
