# Delta Radar

Android-only overlay for Delta Force Mobile. The rooted native scanner reads
the local UE4 actor state and sends complete snapshots over an abstract Unix
domain socket directly to the Android foreground service. No HTTP server, LAN
listener, browser UI, SSE endpoint, or web assets are included.

## Components

- `scanner/memscan-legacy.c` — external actor, transform, health, death, and team reader used by the app.
- `scanner/launch.sh` — starts the app-installed reader and the game.
- `android/` — floating menu and on-device overlay renderer.

## Build

```bash
JAVA_HOME='/Applications/Android Studio.app/Contents/jbr/Contents/Home' \
  ./android/gradlew -p android assembleDebug
```

The scanner and Android app must use the same abstract socket name:
`delta_radar_feed`.

## Reader

Press **Start Overlay** to start the external reader. It uses
`process_vm_readv`; the previously experimental reader is now the sole app
reader. Its source and binary retain their `memscan-legacy` filenames. Players
and bots have been observed rendering without a game exit, but a full-match
stability and correctness test has not yet been completed.

Player detection uses the same raw root-position behavior as master. Some
remote character positions are encrypted, so their lines and boxes can still
appear underground or in other incorrect locations. Correct decoding remains
unresolved.

The reader now recognizes selected loot, containers and death boxes using SDK
class ancestry, and skips encrypted or invalid loot transforms. Class indices
and the expanded streamed-level scan have not been comprehensively verified.
The user observed loot rendering correctly in a match, but some loot may still
be missing, and stability under the additional reads is not yet established.

To rebuild the reader with the Android NDK:

```bash
"$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang" \
  -O2 -Wall -Wextra -Werror scanner/memscan-legacy.c -o scanner/memscan-legacy -lm
```
