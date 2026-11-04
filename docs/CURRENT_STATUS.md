# Current project status — 2026-11-04

This is the entry point for current decisions, installed identities, and next
work. Dated investigation logs describe their own checkpoints; they do not
override this page or the user's later instructions.

## Working version

- Develop on `OnePlus-all-working-lines`. Local and remote accepted source is
  commit `3f2ad5156fc6a70799ec724ada8446743574ab3c`; later documentation commits
  can advance the branch without changing the accepted binaries.
- Frozen accepted-source tag: `oneplus-working-2026-11-04`.
- User confirmed the installed APK is working. This is overall acceptance;
  no separate detailed results were supplied for every rendering edge case or
  the recurring root-grant toast.
- Target device: OnePlus 7T, ARM64, Android 12 / API 31.
- The connected GitHub account can push but cannot administer this repository.
  GitHub's default branch remains `master`; the owner must select the OnePlus
  branch in repository settings. Other branches and older tags are preserved.

## Exact installed pair and rollback

The public [release manifest](../releases/oneplus-working-2026-11-04.json) records
complete artifact hashes, sizes, source identity, and validation. The app's
native pin is [scanner/deployment.properties](../scanner/deployment.properties).

| Artifact | SHA-256 prefix |
| --- | --- |
| Accepted APK | `75c20508e740c0ec` |
| Matching native scanner | `b7e4964591076cbe` |

The exact binaries, deployment properties, portable native manifest, checksums,
and restore instructions are archived together locally at:

`evidence/local/checkpoints/oneplus-working-2026-11-04/`

The matching `.tar.gz` and `.tar.gz.sha256` are beside that directory. The archive
was reopened and every bundled file checked against its checksum. No APK was
rebuilt for this checkpoint. Binary bundles stay ignored locally; the source tag
and artifact identity manifest are pushed to GitHub. Private captures, account
data, preferences, root policies, and signing keys are excluded.

For exact rollback, follow `RESTORE.md` inside the bundle. A new source build
can embed a different revision and produce a different scanner hash. The
accepted binary embeds pre-commit base `b95dd6a` plus its exact source-input hash;
that is expected provenance, not a claim that it was built after `3f2ad51`.

## Current behavior

- The foreground service owns startup, waiting, cancellation and bounded
  recovery. Start verifies the pinned binary and identity. Stop revokes the
  session lease; uncertain launches are not repeated. Confirmed scanner exits
  permit at most three recoveries per minute. The app does not start the game.
- Fresh socket traffic from the launch-verified scanner PID supplies liveness
  without a recurring `su` request. Missing traffic falls back to root process
  inspection. Startup, recovery and waiting for the game can still show root
  grant toasts. Magisk root access returned after the user-authorized normal
  reboot; no root policy/database mutation was made.
- Health ownership, active-pawn/roster discovery, animated skeletons, separate
  camera sampling and direct bounds decoding are integrated. Existing loot
  values and filters remain. See [integration details](POC_INTEGRATION_2026-11-04.md).
- Entity frames expire after 750 ms. The independent camera must be at most
  100 ms old; timestamped combat samples over 150 ms old are omitted. Loading,
  disconnect and world/process changes invalidate obsolete overlay state.

## Validation and remaining stability checks

| Check | Current evidence | Remaining device verification |
| --- | --- | --- |
| Android JVM tests | 66 passed, zero failures/errors | None for host result |
| App and instrumentation APK builds | Passed; lint zero errors, 145 warnings | Instrumentation APK build is not a device-test run |
| Native checks | 14 ASan/UBSan programs passed before heartbeat-only change; affected camera fixture passed again afterward; ARM64/API31 warnings-as-errors build passed | Runtime performance and long-session behavior |
| Installed identity/settings | APK/native hashes and embedded native identity verified; visual settings preserved | None for installation proof |
| Feed expiry/world transitions | Host tests cover stale/future/rewound frames, complete-frame removals and process/world invalidation | Observe transitions and reconnects with this accepted pair |
| Lifecycle and recovery | Earlier controlled device startup checks passed; current host coverage includes recovery budget and peer liveness | Current-pair Hide/Show, rotation, reconnect and controlled exit behavior |
| Root-grant toast fix | Idle native heartbeat and fresh/foreign/stale peer checks passed; user accepted APK overall | Separate confirmation that periodic toasts stopped during normal operation |
| Extended stability | General user acceptance; historical successful sessions are documented separately | Sustained session/extraction check; earlier SIGBUS cause is unresolved |

Saved validation and installation proof remain in ignored
`evidence/local/session/root-toast-20261104/`. Relevant host regression sources:
[snapshot expiry](../android/app/src/test/java/com/deltaradar/app/EntitySnapshotStoreTest.kt),
[camera/world protocol](../android/app/src/test/java/com/deltaradar/app/TrackingProtocolTest.kt),
[peer liveness](../android/app/src/test/java/com/deltaradar/app/ScannerTransportHealthTest.kt),
and [native camera/lease fixture](../scanner/tests/camera_stream_test.c).

No new phone interaction or game-memory capture was performed to create this
checkpoint. The user's last report is that the APK works; the installation-time
stopped-process observations are historical, not current phone state.

## Next work: stability, then measured performance

At the next coordinated device session, verify the current pair's lifecycle,
reconnect/expiry and root-toast behavior first. Keep the installed accepted pair
available for rollback. Do not introduce new features during that comparison.

Measure entity/camera sample age at drawing, projection/draw duration and missed
display frames, then scanner/app CPU and thermal/battery behavior. Record the
device refresh rate, duration, overlay options and rough entity load so repeated
measurements are comparable. Report typical and slow-end timings rather than
only an average. Existing opt-in capture changes the workload; establish a
capture-disabled baseline and compare enabled capture separately.

This is a measurement plan, not a new benchmark result. No performance claim or
optimization is justified by the current documentation cleanup. Host tests
cannot establish on-phone drawing latency, heat, battery drain or crash freedom.

## Scope and unresolved container discovery

Lines, boxes and ordinary loot markers/values are active scope. Auto-loot,
backpack replacement and enhanced aim assist are cancelled. Historical pickup
instructions must not be resumed. The user resumed manual testing and accepted
the APK after an earlier account-suspension report; its cause remains unknown.
Automated game-memory diagnostics have not resumed.

The imported decoder decodes spatial bounds. It does not decode container item
lists. Earlier matched captures found no safe items in the checked native/Nearby
sources before search, then matching items afterward. This does not establish
that every possible client source is empty. Pre-search discovery remains
unresolved; there is no demonstrated encrypted contents list for this decoder.

## Historical references

- [Complete earlier session history](SESSION_HISTORY_2026-11-04.md)
- [Deployment and startup implementation](SCANNER_DEPLOYMENT.md)
- [Original OnePlus checkpoint](ONEPLUS_WORKING_CHECKPOINT.md)
- [Earlier performance audit](PERFORMANCE_AUDIT.md)
- [Feature decisions](FEATURE_BACKLOG.md)
