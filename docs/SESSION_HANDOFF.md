# Fresh-session handoff — 2026-11-04

Read this first. Current branch: **`codex/audit-cleanup`**, based on
`codex/auto-pickup` at `5c80a1f`. The user ended live testing for sleep, then
requested an offline audit and a saved handoff. Do not request another match
until the user is ready. No phone actions occurred during this audit.

## Saved state

- Offline audit is complete: [results and checks](CODE_AUDIT_2026-11-04.md).
- Audited APK is built and archived locally, **not installed**:
  `evidence/local/session/audit-20261104/delta-radar-debug.apk`.
  SHA256: `3f4b6077b257ccf461ff6f92cc0d4e259e859e28c0a663dae211e135435057d3`.
- Last installed production APK SHA256:
  `21cdaadfad5223cf5b187514fa415bbbe8d846ac53cedec9d2dd9c0ce9a86e6c`.
  Native deployment remains pinned in `scanner/deployment.properties` to
  `135e3893f9e502f50c71f6d39d1801e0470d9bd05215b9223dee802e6f535d14`.
- Frozen branch `OnePlus-all-working-lines` and annotated tag
  `working-v1-2026-11-03` still resolve to
  `a9523c9db7101e782946a38c3fa6b3b77b046dc8`.
- Radar/scanner/app were stopped at the end of the last live session; zero
  project scanner and radar app processes were verified then. Recheck current
  device state before future work. Preserve unrelated `/data/local/tmp/memscan`.
- Generated Android build directories are now untracked and ignored. **Do not
  restore them from older commits.** Private captures remain ignored locally.

## Resume auto-pickup

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

Optional next installation: the audited APK, once the user is ready. Its
instrumentation APK was built but not run; responsiveness and full-match
stability need device checks. SIGBUS remains unresolved. Container contents
before searching remain paused by user agreement. Deferred feature requests
are in [FEATURE_BACKLOG.md](FEATURE_BACKLOG.md); none were implemented here.
