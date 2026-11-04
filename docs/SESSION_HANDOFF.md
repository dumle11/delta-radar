# Fresh-session handoff — 2026-11-04

## Current decision — accepted APK, OnePlus working branch

- User confirmed the installed APK works and requested pushing the code to the
  remote OnePlus branch. Use `OnePlus-all-working-lines` for ongoing development.
  Its previous tip is an ancestor of this source, so publication preserves the
  complete history with a fast-forward. Older branches remain available as
  historical references; the original OnePlus checkpoint tag is unchanged.
- Accepted APK SHA-256: `75c20508e740c0ece6a21a3fa0915b2b868c8952a3a268fd7bab4f31a1ed52db`.
  Matching native SHA-256: `b7e4964591076cbefc65add5e94f59b0cdadea53982bb2f0f38374aff93f72c4`.
  This is overall user acceptance, not independent verification of every
  rendering edge case or pre-search container contents. The statuses below
  describe earlier steps and are superseded where this decision applies.

## Latest update — root-grant toast fix installed, root restored

- User reported repeated Delta Radar superuser-grant toasts. The cause in our
  code is root process inspection every three seconds despite fresh scanner
  socket traffic. Installed the app/native fix described in
  [SCANNER_DEPLOYMENT.md](SCANNER_DEPLOYMENT.md#root-grant-toast-fix-installed--2026-11-04).
- Fix uses fresh, boot-clock-timestamped traffic from the launch-verified socket
  peer PID; idle native heartbeats begin before world discovery. Root inspection
  remains the fallback on missing communication. Startup identity, Stop lease,
  uncertain-outcome handling and bounded recovery are preserved.
- Candidate APK `75c20508...`, native `b7e49645...`. Passed 66 JVM tests, both
  Android builds, zero lint errors and owned native camera ASan/UBSan checks.
  Artifacts/validation are in `evidence/local/session/root-toast-20261104/`.
  Both artifacts are installed and hash verified; native embedded identity is
  verified. Current pin is `b7e49645...`. Visual settings were preserved, scanner
  request is false, radar services/project scanners are stopped. No game process
  was running afterward. User subsequently reported that the APK is working;
  no separate detailed report on recurring-toast behavior was supplied.
- A read-only Magisk CLI schema query failed; its daemon then refused root
  connections. No root database update or notification-policy change occurred.
  Avoid that schema query. User reported Magisk additional setup failed and is
  updating the Magisk app. They are not playing. User then explicitly authorized
  a normal reboot; the agent issued it, Magisk's daemon and root access returned,
  and the user unlocked the phone. Agent installed the tested pair after that
  recovery. Agent did not initiate Magisk setup/update or further game-memory
  reads. Installation proof is in `install-20261104/installation.json` alongside
  the artifacts; recovery is recorded in `recovery.json`.

## Latest update — candidate installed for user testing

- User explicitly authorized APK installation and will test manually. Installed
  the matching POC integration APK and content-addressed native scanner on
  OnePlus 7T `7492618f`. Verified installed APK hash `e8775b8d...` and scanner
  hash `778232f8...`, plus native embedded source/build identity. The normal
  `scanner/deployment.properties` now matches this installed pair.
- Cleared a pre-existing enabled scanner request through normal STOP_SCANNER
  before replacement. Visual settings hash is unchanged. After installation,
  scanner request is false, radar services and project scanners are stopped.
  Agent did not launch the app or scan; game PID stayed `5228`.
- Previous installed APK is backed up alongside exact installation proof at
  `evidence/local/session/poc-integration-20261104/install-20261104/`.
- User also requested checking the POC decoder for container loot discovery.
  Source review: it decodes type-45 spatial bounds, not container item lists.
  The POC loot branch still uses plaintext transforms. Our existing native
  container/linked-pickup metadata path is preserved. There is no demonstrated
  decoder path for pre-search contents; prior matched safe captures found no
  safe items in the checked native/Nearby sources until search.
- Manual user testing is authorized; no new automated live captures, game
  actions, or dismissed pickup/aim features were performed or added.

## Latest update — selective poc-wip integration (offline)

- User authorized importing the recommended improvements after reviewing
  `origin/poc-wip` at `ea410c5` against the local dirty tree.
- Health/active pawn checks, complete-array/roster discovery, 17-joint animated
  poses, independent monotonic camera sampling and direct bounds decoding have
  been integrated. Android 12 libc profile was derived from the saved OnePlus
  libc and tested with synthetic memory; live compatibility remains untested.
- Preserve existing startup/recovery, loot filtering and dismissed-feature
  removals. No aim/auto-shoot or pickup code was imported. The camera thread
  obeys the scanner session lease independently of the main reader.
- See [integration details and limits](POC_INTEGRATION_2026-11-04.md).
  The deployment pin is unchanged; candidates have not been installed/deployed.
  Live testing remains halted. Existing dirty changes are still uncommitted.

Read this first. Current branch: **`codex/audit-cleanup`**, based on
`codex/auto-pickup` at `5c80a1f`. The user ended live testing for sleep, then
requested an offline audit and a saved handoff. No phone actions occurred during
the audit. On 2026-11-04 the user resumed testing and requested installation;
the user tested the archived audited APK and reported that it was good and less
laggy after scanner startup was recovered. They then requested reliable startup;
the follow-up APK below is installed and verified.

## Latest decision — auto-loot and aim assist dismissed

- The user supplied the notice text: **“account suspended” and their UID only**,
  with no reason/duration/code. They suspect auto-loot, but cause is unknown;
  no automatic looting or game input occurred in the reader check.
- The user explicitly dismissed **Take best loot / all auto-loot and enhanced
  aim assist**. These are cancelled, not deferred. Do not resume their research,
  captures, app integration, touch input, or backpack replacement work. Lines,
  boxes and ordinary loot markers remain the project scope.
- Pickup-only app planners/layout code, the native `pickup-context`/`pickup-ui`
  commands and headers, and dedicated tests/capture helper have been removed
  from active source. Their prior source is archived locally at
  `evidence/local/session/take-best-loot-20261104/dismissed-feature-sources.tar.gz`.
  Historical evidence/notes and the working radar/startup changes are preserved.
  No aim-assist implementation existed. This cleanup is offline; no replacement
  APK has been installed and no scanner restarted. The installed APK remains
  the startup fix `c65c666c...`, with its unused planner classes until replaced.
- Offline removal validation passed: **51 remaining JVM tests**, Android app
  and instrumentation APK builds, lint (**zero errors, 145 warnings**), **8
  native ASan/UBSan programs**, and ARM64/API31 diagnostic compilation. Checked
  the candidate APK's dex: pickup planner/layout classes absent; radar classes
  present. Production scanner source and deployment pin were unchanged.
  Candidate archive: `evidence/local/session/take-best-loot-20261104/
  delta-radar-no-pickup-debug.apk`; exact hashes/removal evidence are in
  `dismissed-feature-removal.json` alongside it. Candidate was **not installed**.
  Build used one worker, an in-process Kotlin compiler and a single-use Gradle
  daemon. That build's daemon exited. A separate existing IDE Gradle daemon
  (`5737`, configured for 2 GiB) was observed and left untouched.
- Radar/services/project scanners remain stopped following the suspension;
  live testing remains halted. Removing unused features does not establish
  account safety for the remaining memory reader/overlay.

## Previous event — account suspension, live work halted

- On 2026-11-04 at approximately 10:02 UTC the user reported **“account has
  been suspended”** during the open-container reader check. Reason/duration
  have not been supplied; cause is unestablished. Do not treat a read-only
  diagnostic or successful tests as evidence of game permission/account safety.
- Immediately sent normal `STOP_SCANNER`: verified **zero project scanner/probe
  processes and no radar services**. No game restart, login, input, or further
  game-memory diagnostics followed the report. The active startup request was
  cleared through the normal Stop path. Older running-PID notes below are
  superseded. Preserve evidence; do not resume live testing or request a match
  without addressing the suspension with the user.
- Private record/logs: `evidence/local/session/take-best-loot-20261104/
  account-suspension.json`, `suspension-radar-services.txt`, and
  `suspension-radar-logcat.txt`.
- Just before the report, the previously deployed diagnostic `4a27d2c5...`
  passed the combined `pickup-ui` **open ordinary container** path: a Flight
  Case contained Manometer `15020040003` and S.Panel `15030050016`. UI GID,
  catalog/count/shape matched the separate Nearby inventory records for both.
  Root/cell geometry and guards were complete/stable; captures were bound to
  live camera/game PID `8406`. Source cells had `rawShow=false`, so that flag
  remains unsuitable as visibility proof. No backpack/rig were equipped in
  the screenshot; their zero-sized/empty grids must not be mistaken for a
  populated carried-inventory layout.
- Captures: `container-open-ui-1.json`, `container-open-context-1.json`, and
  `container-open-screen-1.png` in the same private directory. No automatic
  pickup, drop, touch input, game write, or app-button implementation occurred.
  Viewport-to-touch conversion, clipping/occlusion, window generation and
  action lifecycle remain unfinished. Live pickup work is now halted.

## Previous state — startup reliability fix

- **Resume after laptop restart:** the user accepted the startup fix and is
  restarting their sluggish laptop, then wants to continue loot work. Gradle
  daemon PID `1784877` and its Kotlin compiler daemon PID `1785207` were stopped;
  both were verified gone, freeing about 3.2 GiB RAM. No builds/tests remain
  running. ADB server was left available; no phone state was changed during
  laptop cleanup. Device PIDs below are last verified, not current guarantees.
  Reconnect/check the phone and installed identity after reboot; do not repeat
  installation or startup tests without a new reason. Resume the combined
  ordinary-container `pickup-ui` open-container check described below, verifying
  the separate diagnostic identity first. Do not repeat price panels or the
  established Stapler drop/re-pickup cycle.
- Startup changes are saved in the working tree on `codex/audit-cleanup`;
  they have not been committed/pushed. Start now belongs to EntityFeedService,
  survives screen recreation, waits for the game/UE4 mapping, and restores a
  durable enabled request after Android sticky-service recreation. Stop clears
  the request. Failed startup shows Retry even if the listener remains active.
- Readiness waits for exec; background PID/session checks allow three recoveries
  per minute only after a confirmed exit. Uncertain commands are not relaunched.
  Each session retains a `.status` file. See the current section in
  [SCANNER_DEPLOYMENT.md](SCANNER_DEPLOYMENT.md).
- **Current installed APK SHA256:**
  `c65c666c7481d8508d364b38fd6acdb3528b92f952fab5b763a6745c45261223`.
  Archive, source hashes, reports and device evidence:
  `evidence/local/session/scanner-startup-20261104/validation.json`.
  The older audited APK installation below is historical and is superseded.
- Build/lint passed: **84 JVM tests; zero lint errors, 145 warnings**.
  **16 controlled phone tests and one opt-in real-scanner test passed**.
  The latter exercised two real Start/Stop cycles and Activity recreation.
  Actual radar-process SIGKILL restored exactly one new owned scanner; a
  separate TERM of the verified owned scanner also recovered exactly one.
  Visual settings were compared before/after and preserved.
- **Final device state:** app PID `8148`, scanner PID `8661`, session
  `eb20bbaf-cab7-4347-9dc4-5859a4948707`, running and waiting for a match.
  Game PID remained `8406` throughout; game was not restarted or force-stopped.
  Native binary/pin remain unchanged at `135e3893...`. Recheck current state
  before device work. Instrumentation APK is installed; controlled tests are
  separate from the opt-in live test (`-e liveScanner true`).
- The original morning missing-scanner cause remains unproven; the fixed
  lifecycle/retry gaps were established by code inspection and regression
  checks. Full-match stability and SIGBUS remain open. No pickup work was added.

## Saved state

- Offline audit is complete: [results and checks](CODE_AUDIT_2026-11-04.md).
- Audited APK is built, archived locally, and **installed on OnePlus 7T
  (HD1903), serial `7492618f`, on 2026-11-04 at 08:58 UTC**:
  `evidence/local/session/audit-20261104/delta-radar-debug.apk`.
  SHA256: `3f4b6077b257ccf461ff6f92cc0d4e259e859e28c0a663dae211e135435057d3`.
- Installation used `adb install -r` and returned `Success`; the installed
  `base.apk` SHA256 matches the archive. Evidence:
  `evidence/local/session/audit-20261104/installation.json`.
- Previous installed production APK SHA256:
  `21cdaadfad5223cf5b187514fa415bbbe8d846ac53cedec9d2dd9c0ce9a86e6c`.
  Native deployment remains pinned in `scanner/deployment.properties` to
  `135e3893f9e502f50c71f6d39d1801e0470d9bd05215b9223dee802e6f535d14`.
- Frozen branch `OnePlus-all-working-lines` and annotated tag
  `working-v1-2026-11-03` still resolve to
  `a9523c9db7101e782946a38c3fa6b3b77b046dc8`.
- Radar/scanner/app were stopped at the end of the last live session; zero
  project scanner and radar app processes were verified then and again after
  this installation. The app was not launched and no scanner was started.
  The deployed scanner hash was reverified against the pin above. Recheck
  current device state before future work. Preserve unrelated
  `/data/local/tmp/memscan`.
- **Live-test update, 2026-11-04 09:14 UTC:** the user reported no loot or
  combat markers. App/overlay/feed services were running, but no scanner
  process existed and capture files still dated from the preceding session.
  Player/bot/loot feature settings were enabled. Sent the existing feed
  service `START_SCANNER` action using root `am`; verified startup launched
  pinned scanner PID `10346`, session `4352ba3e-4f69-4738-8315-a103601beaf1`,
  owner app PID `8050`. Fresh accepted frames came from peer PID `10346`;
  one sample had 7 players, 14 AI and 106 loot entities. Overlay draw evidence
  reported 19 entities with 41 ms source age. App and game (PID `8406`) were
  not restarted; no APK/native changes were made. **Scanner is now running**;
  recheck rather than relying on the earlier stopped-state bullet. Original
  startup-failure reason is unestablished because its logs were no longer
  present; do not call this a confirmed audit regression. User-visible recovery
  was subsequently confirmed by the user's good/less-laggy report. Private
  evidence: `evidence/local/session/audit-20261104/missing-markers/recovery.json`
  and corresponding feed/overlay captures.
- Generated Android build directories are now untracked and ignored. **Do not
  restore them from older commits.** Private captures remain ignored locally.

## Archived auto-pickup checkpoint — do not resume

The user has dismissed this work; the following checkpoint is historical.

There is **no operational auto-pickup button or actuator**. Full findings:
[AUTO_PICKUP.md](AUTO_PICKUP.md). Historical live notes:
[LIVE_SDK_HANDOFF.md](LIVE_SDK_HANDOFF.md); their older test totals/branch names
describe the preceding session, not this audit.

The next live check is the combined ordinary-container `pickup-ui` reader's
open-container path. Its closed/no-selector path passed. Separate exploratory
item-to-cell linkage and geometry, manual pickup, and the Stapler ground-drop /
re-pickup cycle already passed: do not repeat price panels or those cycles
without a new reason. The prior Reader drop remains ambiguous.

Then finish viewport-to-touch conversion, clipping/occlusion, current-window
generation, action confirmation/cancellation and the manual “Take best loot”
button. Raw render coordinates are not touch points; do not hardcode the
observed scale or use `bIsShow` as visibility. Player-body UI routing and stacks
remain unsupported. The desired replacement behavior is to remove cheaper
backpack loot only when the complete replacement improves value; the game
handles placement. Existing offline planning is not proof of live operation.

The audit cleaned diagnostic headers and built a new candidate, **not
deployed**. `scanner/build/pickup-probe-build.json` still identifies the last
live diagnostic; do not mistake it for a build of the current cleaned headers.
Candidate binaries, exact hashes, source hashes, JUnit XML and lint reports:
`evidence/local/session/audit-20261104/validation.json`. Verify the diagnostic
identity explicitly before any later capture.

The audited APK was tested and superseded by the startup fix above. Full-match
stability still needs device checks; SIGBUS remains unresolved. Container contents
before searching remain paused by user agreement. Deferred feature requests
are in [FEATURE_BACKLOG.md](FEATURE_BACKLOG.md); none were implemented here.
