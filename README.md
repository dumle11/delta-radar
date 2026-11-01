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
