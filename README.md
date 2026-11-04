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

## Aim controls

Open the overlay bubble and select **Aim** beside **Visual**. Enable
**Automatic aim · no hold button** to track verified enemy players' and bots'
torsos inside the circle. No activation-button setup is required. **FOV
radius** changes the circle from 5–45% of the shorter screen dimension;
**Aim strength** controls each camera adjustment. Drawing and targeting use
the same radius, independently of the Visual switches.

**Customize camera drag area (optional)** selects where touchscreen camera
drags begin; it is not an aim activation button. The default needs no setup.
Use **Use default camera drag area** to remove custom calibration. Centre/left look points from the first
Aim build fall back to the measured right-hand area (70% across, 55% down). For **Auto shoot on crosshair**, first choose **Set fire
button** and tap the centre of the game's fire control. Calibration captures
the tap without forwarding it. Back or the instruction banner cancels it.
Saved locations scale with screen resolution.

Aim uses root touchscreen gestures, without game-memory writes. It pauses
when the overlay menu opens, the local player dies, the game loses focus, or
the camera sample is older than 300 ms. Teammates, loot, unknown teams, dead actors
and off-screen actors are excluded. Automatic input also requires the reader
metadata for unmanaged handler indices (0xFFFF), clear encryption flags,
plaintext bounds and a plaintext world transform agreeing within
50 cm, positive known health, a plaintext camera and a recently rendered mesh.
The local pawn must be alive and linked to its controller and PlayerState.
Missing metadata is rejected. Encrypted positions
remain unsupported; player markers without passing position and current
camera checks are hidden and cannot be targeted automatically. The overlay
shows a warning with the number hidden. The mesh's rendered flag is a necessary filter,
not a guaranteed line-of-sight test.

Each small camera gesture waits for a newer camera timestamp and a measured
reduction in targeting error. Missing response or increasing error stops assistance until it is reset
with the automatic aim switch. Large manual camera movements and zoom changes
yield briefly to the player instead of latching a calibration fault. Input gestures may interrupt simultaneous manual touches.
Torso alignment and firing have been exercised against visible firing-range
dummies; encrypted multiplayer targets and full-match behavior remain unverified.
See [the investigation notes](docs/AIM_POSITION_INVESTIGATION_2026-11-03.md).

## Reader

Press **Start Overlay** to start the external reader. It uses
`process_vm_readv`; the previously experimental reader is now the sole app
reader. Its source and binary retain their `memscan-legacy` filenames. Players
and bots have been observed rendering without a game exit, but a full-match
stability and correctness test has not yet been completed.

Player detection now requires native Character ancestry or a named PlayerState
with a matching pawn backlink and capsule. Cached actor classes are rechecked
on each frame to reject reused storage. Human player and teammate markers are
shown when the current snapshot has either agreeing unmanaged bounds/world
transforms, or decoded bounds that pass the new root/mesh consistency checks.
The decoder supports the captured Proxima 1.202 software path on the tested
Android 10 bionic build; unknown profiles and changing records remain hidden.
Camera plaintext and freshness checks still apply. Bots and loot retain their
existing behavior. Skeleton mode uses verified animated world-space bones and the mesh’s actual
parent hierarchy, with perspective projection of each joint. It has no schematic
body fallback: unavailable or inconsistent poses are omitted. See the
[skeleton rendering notes](docs/SKELETON_RENDERING_2026-11-04.md). See the
[decoder follow-up](docs/POSITION_DECODER_FOLLOWUP_2026-11-03.md) for the recovered
path, live validation and remaining version/stability limits.

The reader now recognizes selected loot, containers and death boxes using SDK
class ancestry, and skips encrypted or invalid loot transforms. Class indices
and the expanded streamed-level scan have not been comprehensively verified.
The user observed loot rendering correctly in a match, but some loot may still
be missing, and stability under the additional reads is not yet established.

### Logs diagnostics

The overlay's **Logs** tab displays the most recent scanner sample, including
the GWorld slot, world, persistent level, actor array and its reported count.
The horizontally scrollable table shows up to 64 entities already published
by the reader, including dead entities, their class address and any class name
already resolved, health when known, and position-verification status. It is
not an inventory of every actor in the array: discovery, classification and
publication filters apply, and a synthetic self entry has no actor address.
An asterisk on `alive*` means only that no death flag was detected.

The latest sample also reports failed or short reads and the last affected
address. An errno is a read error, not proof of encryption or of a particular
memory-map permission. Unavailable fields and disconnected or stale samples
are labelled; a successful plaintext check is not a guarantee of correct
projection. The table reuses existing scanner reads and does not add a decoder,
injection, protected-memory access, or new target reads. For the forum
findings and their limits, see
[forum position evidence](docs/FORUM_POSITION_EVIDENCE.md).

### Opt-in position trace

In **Logs**, select **Start position trace** before playing and **Stop position
trace** afterwards. It is off by default, stops after 30 minutes or when the
feed service stops, and records at most one existing scanner snapshot per
second. A busy writer may skip samples; the status and later records report
skips. File errors stop recording and appear in Logs and logcat. The trace
does not add game-memory reads, inject code, decode positions, or prove that
positions match visible players. Scanner publication filters still apply.

The app-private JSONL files `files/position-trace-current.jsonl` and
`files/position-trace-previous.jsonl` are each capped at 2 MiB. On rotation
the oldest file is replaced, and new sessions append until rotation. Records
omit player names but contain process addresses, coordinates, team, health,
camera metadata and read failures; treat them as sensitive. Missing handler
values are `-1`, and a missing entity list is distinct from an empty one.
Starting with trace schema 2, loot is excluded from trace records to preserve
space for character positions; the number omitted is recorded. Up to 512
other entities are included.
Records report both scanner-side and trace-side entity truncation.

For a debuggable build, retrieve any existing files with:

```bash
adb exec-out run-as com.deltaradar.app cat files/position-trace-previous.jsonl > position-trace-previous.jsonl
adb exec-out run-as com.deltaradar.app cat files/position-trace-current.jsonl > position-trace-current.jsonl
```

The previous file does not exist until the first rotation. Stop the trace before
retrieval to avoid reading a file during rotation. A scanner build can be
selected without replacing the existing tracked binary by passing
`-PscannerBinary=/absolute/path/to/reader` to Gradle.

To rebuild the reader with the Android NDK:

```bash
"$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang" \
  -O2 -Wall -Wextra -Werror scanner/memscan-legacy.c -o scanner/memscan-legacy -lm
```
