# Delta Radar

Android-only overlay for Delta Force Mobile. The rooted native scanner reads
the local UE4 actor state and sends complete snapshots over an abstract Unix
domain socket directly to the Android foreground service. No HTTP server, LAN
listener, browser UI, SSE endpoint, or web assets are included.

## Components

- `scanner/memscan.c` — native actor, transform, health, death, and team reader.
- `scanner/launch.sh` — starts the scanner early enough to retain read access.
- `android/` — floating menu and on-device overlay renderer.

## Build

```bash
JAVA_HOME='/Applications/Android Studio.app/Contents/jbr/Contents/Home' \
  ./android/gradlew -p android assembleDebug
```

The scanner and Android app must use the same abstract socket name:
`delta_radar_feed`.

## Reader stability

The app does not start a reader until **Start Overlay** is pressed. Both the
current and historical readers have been followed by game termination; neither
is a verified stable feed. The experimental historical reader is opt-in and may
show incorrect or missing entities. It uses `process_vm_readv` and is based on
the reader from commit `67cc7a3`. The original binary survived one 45-second
live test, but its hard-coded heap range prevented camera and local-player
verification. The experimental source now accepts current heap addresses for
those fields, checks remote player states outside the old fixed heap range, and
uses the SDK's current camera, player-name, team, and death offsets; the
publication gate remains in place. This revised binary has not passed a live
stability or full-match correctness test.

To rebuild the experimental reader with the Android NDK:

```bash
"$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang" \
  -O2 -Wall -Wextra -Werror scanner/memscan-legacy.c -o scanner/memscan-legacy -lm
```
