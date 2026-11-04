# Offline code audit — 2026-11-04

Historical audit. Its branch/build status and pickup research notes are
superseded by [CURRENT_STATUS.md](CURRENT_STATUS.md). Pickup and aim features
were subsequently cancelled; outstanding instructions below are not active work.

Completed on `codex/audit-cleanup`, starting from `codex/auto-pickup` at
`5c80a1f`. No device access, installation, match testing or deployment-pin
changes. The frozen working branch/tag remain unchanged. Start future work
with [SESSION_HANDOFF.md](SESSION_HANDOFF.md).

## Findings and changes

- Removed legacy overlay rows/listeners that were immediately hidden, their
  unused status callback, unused root helpers and duplicate log storage.
  Kept the saved-preference migration and current per-category controls.
- Removed obsolete pre-API26 branches; the app's minimum SDK is 26. Reused the
  existing app-name resource.
- Reused a bounded array of label rectangles instead of allocating a list and
  rectangles each draw. Collision order and placement are preserved. This is
  an allocation reduction, not a measured on-phone frame-rate improvement.
- Added bounded UTF-8 feed-line reading: LF, CRLF, CR and final EOF records
  are supported; oversized records terminate the client instead of becoming
  partial frames. Limits are 2 Mi characters per frame and 1,024 entities,
  above the current scanner's 512-actor / roughly 1 MiB bounds.
- Reject older captured timestamps even inside the freshness window, so a
  buffered frame cannot rewind a newer camera/world snapshot. Added regression
  tests for timestamp rejection and oversized input.
- Stop the command-output reader and close its stream after the bounded
  drain, including when a descendant continues producing after parent exit.
  Added a deterministic fake-process regression test.
- Moved accepted-client credential/timeout initialization inside cleanup scope.
- Diagnostic pickup tables now allocate to their bounded actual capacity
  rather than zeroing the maximum entry array for every small table. Shared
  the repeated final-header stability guard. Production scanner sources and
  deployment identity did not change.
- Added `scanner/tests/run.py` for reproducible native ASan/UBSan checks.
- Removed 612 generated Android build/cache files and Finder metadata from
  version control in `d914100`; local generated files remain available and
  ignored. Updated README build/check instructions. Never restore these
  directories from an older commit as part of future build cleanup.

## Final verification

- Android debug APK, instrumentation APK, JVM tests and lint built successfully:
  **81 tests passed; zero failures/errors.**
- Android lint: **zero errors, 145 warnings**, down from 168 at the preceding
  baseline. Remaining warnings have not all been remediated.
- **9 native regression programs passed ASan and UBSan.**
- **13 Python host-capture tests passed.**
- ARM64/API31 production scanner and diagnostic built successfully; diagnostic
  used `-Wall -Wextra -Werror`. Neither binary was deployed.
- `git diff --check` passed. Deployment pins and frozen working refs unchanged.

Reproduce the Android check with JDK17:

```sh
env JAVA_HOME=/usr/lib/jvm/temurin-17-jdk-amd64 ./android/gradlew -p android testDebugUnitTest assembleDebug assembleDebugAndroidTest lintDebug --console=plain
python3 scanner/tests/run.py --output scanner/build/audit-20261104/native-tests
python3 -m unittest discover -s scanner/tests -p 'test_*.py'
```

Archived APKs, native candidates, manifest, JUnit XML, lint reports and
`validation.json` are in ignored `evidence/local/session/audit-20261104/`.
The validation record includes exact changed-source and artifact hashes.
The production scanner manifest records base revision `5c80a1f` and exact
unchanged native inputs; it does not claim to stamp the final audit commit.
The existing last-live diagnostic manifest was deliberately not overwritten.

## Limits and remaining work

No instrumentation tests ran on the phone. The audit APK is not installed;
full-match stability, actual rendering latency and SIGBUS remain unverified.
Auto-pickup is still research/planning with no automatic input or working
button. The combined UI reader's open-container path and action lifecycle
remain outstanding. No new feature work was included in this audit.
