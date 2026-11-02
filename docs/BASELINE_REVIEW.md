# Baseline repair — 2026-11-02

Reviewed baseline: `master` at `a448200d0e645dab34c920ae3a4f7e8f14ef7d0e`.
Completed first-repair scope: Android snapshot lifetime, truthful feed status, and cancellation of queued
rendering across Hide/Stop. No scanner, entity-category controls, projection
geometry, or menu redesign was included in that first repair. The subsequent
verified-scanner/startup milestone is documented in
[SCANNER_DEPLOYMENT.md](SCANNER_DEPLOYMENT.md), with exact identities, changed
startup behavior, durable evidence locations and current validation status.
No aimbot, automated aiming, hitbox changes, or anti-cheat bypass work is included.

## What was actually available

- Connected OnePlus 7T HD1903, Android 12 / API 31.
- Delta Force installed: `1.202.37117.5601`, version code 2217, target SDK 35.
- Delta Radar was **not installed** at intake. Repository app configuration is
  version `1.0` / code 1, target SDK 34, minimum SDK 26.
- The game was running; a screen capture confirmed active gameplay. No input,
  game restart, injection, or live scanner launch was performed during intake.
- Neither a `memscan` process nor `delta_radar_feed` socket was present.
- Existing `/data/local/tmp/memscan` is a 2,220,800-byte, static ARM64 Android
  executable, built for API 35 with NDK r27. It was copied read-only for identity
  inspection and remains unchanged on the phone.

| Binary | SHA-256 |
| --- | --- |
| Phone's existing `memscan` | `00e46f0edb67f01a2dd8b4bdd566465fa80a2d0fe5d33c1caaa7fcb7ccd8a0ec` |
| Repository `scanner/memscan` | `7dcdee87864df5d948d010782c8b27e695e70ad99cb3c0c59ec9b41954e76d12` |
| Repository `scanner/memscan.fixed` | `4aa080625cf934742536bd24a315e97c2f1086ea1cd4b324e98aea8e5c8ab360` |

The phone's original executable remains unidentified and preserved. The subsequent
startup repair removes that hard-coded launch path and selects only the verified
API-31 deployment recorded in [SCANNER_DEPLOYMENT.md](SCANNER_DEPLOYMENT.md).

## Historical screenshots versus checkout

All nine supplied images were reviewed. Seven are visual/feature references;
the two agent-message screenshots contain historical claims, not raw captures
of a verified feed.

| Historical claim | Exists in this checkout? | What remains unproven |
| --- | --- | --- |
| BattleFieldSystem character roster discovery | Yes: `is_battlefield_system`, world/subsystem and heap searches, and `read_authoritative_characters` | The structural offsets identify the intended roster on this phone/build; that roster includes every visible player |
| Roster used to select actors | Yes: allow-list when available, append roster members missing from level arrays, fallback heuristics otherwise | Elimination of stale proxies and correct ownership/presentation transforms |
| Underground transforms compared | Diagnostics print alternative root transforms | Historical 9–12 m deltas, their cause, and any successful transform repair |
| Separate loot display controls | Yes: Settings, feature panel, colour/mode and renderer paths | Per-category runtime correctness |

The scanner still contains fallback self selection, cached actor discovery,
fixed offsets/address-range assumptions, an existing broad 500 m altitude guard,
and replay of old entity values during some failed reads. None was added or
changed by this repair. A recently received frame does **not** prove that the
scanner's source memory values are current or correct.

## Confirmed Android failure mechanisms

1. **Hide can be undone by pending work.** ACTION_HIDE clears/removes views but
   previously left the 33 ms projection loop running. Publication checked only
   whether the menu was open. Old computation could recreate/submit a visible
   overlay, including after service destruction.
2. **Connection status was inferred from processes.** A listening feed service
   plus `pidof memscan` was labelled connected, even without one valid frame.
   Feed UI updates also waited behind synchronous root-command reads.
3. **Stored entities had inconsistent lifetime.** Missing actors survived twelve
   incoming frames, including an explicit empty loading frame. A silent open
   socket retained entity state/count indefinitely even though the renderer
   separately timed out. Concurrent maps did not make a camera plus roster
   snapshot atomic.
4. **Drawing renewed the display's age.** A computed frame was submitted with
   publication time instead of its data receipt time.

These are code-traced causes for lifecycle/status failures. They do not
establish the cause of missing visible actors or underground source positions.
Device acceptance tests distinguish reproduced failures from source analysis.

## Repair behavior

- `EntitySnapshotStore` validates each complete frame before replacing state.
  Missing coordinates, nonfinite positions/camera fields, duplicate IDs, invalid
  FOV, and nonempty frames without their camera do not update freshness or
  partially change the roster. Explicit empty frames clear the old world.
- Snapshots are copied atomically. Despawned actors are removed on the next
  complete frame, without the old twelve-frame retention. This may reveal
  scanner-side roster gaps as flicker; masking those gaps would undermine this
  baseline.
- Freshness uses elapsed realtime and expires at 750 ms, even with no new data.
  Stale reads expose zero entities. A silent socket times out after 2 seconds,
  is closed, and the listener can accept another client. Stop explicitly shuts
  down both the listening and accepted sockets before closing them, then waits
  up to 250 ms for its worker to exit. Socket publication is locked against
  destruction so a late worker cannot publish a live socket after Stop.
- The scanner's existing death/health interpretation is preserved downstream;
  disagreement about zero-health trust remains an explicitly deferred issue.
- Overlay lifecycle generations invalidate old work across Hide, Show and
  destruction. Publication also requires the same feed-service instance and
  current fresh snapshot. Data receipt time is passed to the view.
- Feed UI polls independently of root checks. Only fresh validated camera frames
  produce “connected”; empty frames show idle, expiry shows stale, and a
  listener without frames shows waiting. UI polling resolution is 250 ms.
- The declared `dataSync` service now has `FOREGROUND_SERVICE_DATA_SYNC`.
  Android 14 requires the permission for target SDK 34 apps; Android 12 does not
  exercise that new enforcement. See [Android's service-type requirements](https://developer.android.com/about/versions/14/changes/fgs-types-required).

## Validation and artifacts

Builds use Java 17 and the existing SDK in a temporary source tree, excluding
tracked imported caches/build output. Machine-specific `local.properties` is
changed only in that temporary tree. Generated files are not part of this patch.

Commands in the review tree:

```sh
./gradlew --no-daemon testDebugUnitTest assembleDebug assembleDebugAndroidTest lintDebug
```

- Seven existing projection tests and twelve new snapshot/lifecycle JVM tests.
- Five Android instrumented acceptance checks: stalled connection expiry,
  immediate loading-frame clear, disconnect/reconnect, and Stop while a socket
  read or accept is blocked followed by restart. They exercise the actual service/socket
  path with clearly named synthetic test fixtures; they do not start an activity,
  game, scanner, or overlay.
- The two `FeedExpiryDeviceTest` checks also compile against the unchanged
  baseline, allowing a before/after device comparison.
- Final repaired build: `testDebugUnitTest assembleDebug assembleDebugAndroidTest lintDebug` **passed**. All 19 JVM tests passed (7 existing + 12 new), with no failures/errors/skips. Lint: zero errors, 170 warnings (UI/i18n, drawing allocations, dependencies, SDK age, etc.); these are not silently treated as a clean lint report.
- Unchanged original app plus the common Android acceptance-test APK also built successfully.
- Both Android test APKs were inspected to confirm that the common before/after test class is present.
- Aleks explicitly confirmed a safe break. Original and repaired app/test APKs
  were installed in turn, using `adb install -r` for updates with no data clear.
- **Original:** the silent-feed check failed: two entities remained after one
  second. The isolated empty-frame check also failed: two previous entities
  remained after a loading frame. The original combined run additionally had a
  setup failure on its second test; the isolated run establishes the intended
  empty-frame failure without relying on that setup failure.
- **Initial repair:** two of four device tests passed. Rapid service restart
  exposed `java.io.IOException: Address already in use` in logcat. Plain socket
  close did not reliably release a listener blocked in native accept on this
  handset. This was an actual additional failure, not a passing validation run.
- **Final repair:** explicit socket shutdown and bounded worker termination were
  added, with a fifth regression for Stop during blocked accept. The complete
  device suite then passed **5/5 in 2.628 seconds**, including expiry, empty
  frame, reconnect, blocked-read Stop/restart, and blocked-accept Stop/restart.
  These are real Android service/socket tests using synthetic input, not live
  game-feed or overlay-window verification.
- Final installed app: version `1.0` / code 1 / target 34. Installed APK SHA-256
  `ebaa0aad186a8b067afb76aadfb93050342a9d569ff7d9ffd99704c97529504b`
  matches the tested build. App and companion test package remain installed;
  no feed service, feed socket, radar process, or scanner process remains active.
- Game PID remained **3746** before and after. The phone's pre-existing scanner
  hash is unchanged. No game or scanner was launched/stopped by this testing.
- Hide generation checks passed on the JVM; actual overlay-window Hide/Show,
  MainActivity Start/Stop/scanner startup, and game-source accuracy remain
  **unverified on-device**.
- Exact APK hashes and JVM results are recorded in `/tmp/delta-radar-baseline-evidence/validation.json`.

Temporary captures, installed-package inventory, original scanner identity copy,
and raw test transcripts: `/tmp/delta-radar-baseline-evidence/`.
Temporary build trees: `/tmp/delta-radar-original-baseline/` and
`/tmp/delta-radar-review-zid_v55a/`. These paths are local working artifacts,
not committed deliverables or persistent backups.

## Limits at completion of the first repair (historical)

- Missing visible entities, static/underground markers, live death/despawn,
  match transitions, and camera/zoom alignment have not yet been reproduced
  from this checkout on this phone: no matching scanner/feed was running.
- Narrow zoom still passes through the existing 30° projection clamp. No
  arbitrary vertical correction or fabricated enemy was introduced.
- RootUtils still uses blocking reads without a deadline and retries commands
  after exceptions; status caches can outlive a failed root call. The activity's
  startup worker and delayed callbacks are not fully cancellable by Stop or
  activity destruction. Feed status no longer blocks on those root reads.
- An older projection is rejected if a newer snapshot arrives before publication.
  On-device frame-rate/starvation behavior at large rosters remains to be measured.
- No emulator/AVD was available. Android 14 enforcement and actual overlay
  window behavior still require a suitable device or test environment.
- Before running MainActivity Start, establish a separately preserved deployment
  path and identity for the correct scanner. Coordinate any interruption with
  Aleks; do not overwrite the pre-existing executable or restart an active match.
- Data-only before/after service tests are complete. Next establish a verified
  live scanner deployment, then test fresh feed → Hide → Show →
  disconnect/stall → loading/exit → Stop, capturing feed and screen evidence at
  each stage. Only after that baseline should roster/position fixes be selected.

## Device test transcripts

Local raw evidence under `/tmp/delta-radar-baseline-evidence/`:

- `original-device-tests.txt` and `original-empty-frame-test.txt`
- `repaired-device-tests.txt` (initial repair; includes restart failures)
- `first-repair-restart-failure-logcat.txt`
- `repaired-device-tests-after-shutdown-fix.txt` (final 5/5 pass)
- `device-before.json`, `device-after.json`, and `validation.json`

The shutdown fix uses Android's public `Os.shutdown` API before closing socket
file descriptors; compare [Android's LocalSocket implementation](https://android.googlesource.com/platform/frameworks/base/+/HEAD/core/java/android/net/LocalSocketImpl.java).
The on-device failure and passing rerun, rather than documentation alone, are
the evidence that this repair addressed the observed restart failure.

## Subsequent scanner/startup milestone

The native API-31 scanner has now been built reproducibly, deployed under a
separate hash-named path, identified on-device and started/stopped through the app
against the running game at approved safe breaks. Actual Start/Stop/restart and
composed-screen synthetic overlay tests passed. Two loaded matches produced no
world candidate or feed frame: the live end-to-end baseline failed before transport.
Four small ordinary read checks succeeded, but the exact discovery rejection is
unresolved; source accuracy and live transition clearing remain unverified.
The app pins that identity; cancellable startup and bounded, non-retrying root
commands replace the unresolved first-repair startup path. Original work and
passing regressions are preserved. See [current deployment report](SCANNER_DEPLOYMENT.md)
for exact hashes, source changes, durable evidence paths and current validation
limits. Historical first-repair results above remain evidence for that build.

Current milestone validation: **25 JVM tests, 8 Android device tests passed**;
app/native builds and lint passed (0 errors/169 warnings). No game restart,
commit or push. Exact deployed app/scanner identities, current files changed,
raw-evidence locations and the next diagnostic device test are in the linked
scanner deployment report. Radar services/scanner are stopped; game preserved.
