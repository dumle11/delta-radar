# Delta Radar

Android-only overlay for Delta Force Mobile. The rooted native scanner reads
the local UE4 actor state and sends complete snapshots over an abstract Unix
domain socket directly to the Android foreground service. No HTTP server, LAN
listener, browser UI, SSE endpoint, or web assets are included.

## Working branch

Use [`OnePlus-all-working-lines`](https://github.com/dumle11/delta-radar/tree/OnePlus-all-working-lines)
for ongoing development. The user selected this as the working branch and
confirmed the installed APK works on 2026-11-04. Earlier experiment branches
remain historical references; the `working-v1-2026-11-03` tag preserves the
original OnePlus checkpoint.

Start with [current status and next work](docs/CURRENT_STATUS.md). The accepted
source is frozen at tag `oneplus-working-2026-11-04`; the matching APK/scanner
rollback bundle is archived locally with checksums and restore instructions.

## Current installed build

The [selective POC integration](docs/POC_INTEGRATION_2026-11-04.md) adds health,
roster, animated-pose, camera-timing and direct-decoder improvements to local
source while preserving startup/recovery and loot values. The matching APK and
scanner were installed on 2026-11-04, followed by the root-grant toast fix. The
user confirmed this APK works. The installed scanner identity is pinned in
`scanner/deployment.properties`. See [deployment and validation](docs/SCANNER_DEPLOYMENT.md)
for the tested pair; individual rendering and container-discovery limits remain
documented in the integration notes.

The [earlier live SDK handoff](docs/LIVE_SDK_HANDOFF.md) is historical. Use the
current-status page for installed identities and remaining device checks.

The [earlier performance audit](docs/PERFORMANCE_AUDIT.md) explains previous
latency/cache changes. Current stability and performance measurements are
tracked on the current-status page.

The [saved OnePlus checkpoint](docs/ONEPLUS_WORKING_CHECKPOINT.md) records the
2026-11-03 live-tested build, exact executable hashes and remaining acceptance
checks. Its frozen reference is tag `working-v1-2026-11-03`; the OnePlus branch
now advances with accepted changes.

Auto-loot / **Take best loot**, backpack replacement, and enhanced aim assist
were dismissed by the user on 2026-11-04. Pickup-only planners and diagnostic
commands have been removed from active source. Historical findings are retained
in [the archived pickup notes](docs/AUTO_PICKUP.md). Radar lines, boxes and loot
markers remain the project scope. The user resumed manual testing and accepted
the installed APK after the earlier account-suspension report; its cause remains
unknown. Automated game-memory diagnostics have not resumed.

## Components

- `scanner/memscan.c` — native actor, transform, health, death, and team reader.
- `scanner/tools/build_scanner.py` — builds the pinned scanner; deployment is explicit.
- `android/` — floating menu and on-device overlay renderer.

## Build

The local [container navigation candidate](docs/CONTAINER_GUIDE.md) adds
**Visual → Loot → Container guide**: yellow lines, boxes, names and distance to
detected containers, even with unknown contents or loose loot disabled. It uses
the accepted scanner; this Android update has not been installed/live-verified.

```bash
# Use a Java 17 JDK.
./android/gradlew -p android assembleDebug
```

Configure your local SDK location in `android/local.properties`. SDK paths,
Gradle caches and generated build outputs stay local and are ignored by Git.

The scanner and Android app must use the same abstract socket name:
`delta_radar_feed`.

## Baseline reliability work

See [the baseline review](docs/BASELINE_REVIEW.md) for the verified checkout,
device/binary inventory, known source limitations, and test procedure. A
successful build does not establish correct game positions or a working feed.

Frames are complete snapshots. An explicit empty frame clears the previous
world; missing actors are removed on the next frame. Entity frames expire after
750 ms; the separate camera must be at most 100 ms old, and timestamped combat
samples over 150 ms old are omitted. The UI distinguishes connected,
idle, stale, and waiting states. Hide invalidates pending rendering work.

Local checks (Java 17 and a correctly configured local Android SDK required):

```sh
./android/gradlew -p android testDebugUnitTest assembleDebug assembleDebugAndroidTest lintDebug
```

Linux native owned-memory checks (no device access):

```sh
python3 scanner/tests/run.py
```

The original service tests use only test fixtures and local sockets. Additional
window tests deliberately show labelled test overlays; cancellation tests use
delayed mock verification. They never launch or restart the game. Keep these
separate from an explicitly coordinated real-scanner/game-data test. Run them at an agreed safe break, with no
live scanner connected. Preserve the existing scanner executable and establish
its identity before using the app's Start action.

## Verified scanner and explicit startup

Use [the verified deployment workflow](docs/SCANNER_DEPLOYMENT.md) to build for
ARM64/API 31, pin the app to that exact binary and deploy without replacing an
existing scanner. The old launch scripts are disabled. Opening the app does not
start a scan; explicit Start never launches or restarts the game. Stop revokes
its owned session rather than killing processes by name.

Start is owned by the foreground service, so rotating/leaving the app does not
cancel it. If the game has not finished opening, startup waits. Android service
restoration resumes an enabled scanner; explicit Stop clears that request.
Confirmed scanner exits allow at most three recoveries per minute. Failed or
uncertain startup commands show **Retry scanner**, including when the feed
listener is still running. Readiness and PID checks run off the UI thread.

[MobileDumper-7 assessment](docs/SCANNER_RESEARCH.md) is source-only research;
listed package support is not proof of compatibility with the installed build.
