# Delta Radar

Android-only overlay for Delta Force Mobile. The rooted native scanner reads
the local UE4 actor state and sends complete snapshots over an abstract Unix
domain socket directly to the Android foreground service. No HTTP server, LAN
listener, browser UI, SSE endpoint, or web assets are included.

## Current live repair

See [the live SDK handoff](docs/LIVE_SDK_HANDOFF.md) for current results, build
identity and remaining human-player coordinate issues. Nearby loot alignment is
confirmed; the full player overlay is still under verification.

The [offline performance audit](docs/PERFORMANCE_AUDIT.md) describes the latest
latency and cache fixes, regression results, and remaining device checks.

The [saved OnePlus checkpoint](docs/ONEPLUS_WORKING_CHECKPOINT.md) records the
2026-11-03 live-tested build, exact executable hashes and remaining acceptance
checks. It is preserved on branch `OnePlus-all-working-lines` and tag
`working-v1-2026-11-03`.

[Auto-pickup development](docs/AUTO_PICKUP.md) contains the offline planner,
read-only inventory diagnostic and remaining live integration work. Auto-pickup
is not enabled in the installed radar.

## Components

- `scanner/memscan.c` — native actor, transform, health, death, and team reader.
- `scanner/tools/build_scanner.py` — builds the pinned scanner; deployment is explicit.
- `android/` — floating menu and on-device overlay renderer.

## Build

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
world; missing actors are removed on the next frame. A validated camera frame
expires after 750 ms without fresh input. The UI distinguishes connected,
idle, stale, and waiting states. Hide invalidates pending rendering work.

Local checks (Java 17 and a correctly configured local Android SDK required):

```sh
./android/gradlew -p android testDebugUnitTest assembleDebug assembleDebugAndroidTest lintDebug
```

Linux native owned-memory and host capture checks (no device access):

```sh
python3 scanner/tests/run.py
python3 -m unittest discover -s scanner/tests -p 'test_*.py'
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

[MobileDumper-7 assessment](docs/SCANNER_RESEARCH.md) is source-only research;
listed package support is not proof of compatibility with the installed build.
