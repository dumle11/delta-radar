Run host regressions from the repository root:

```sh
python3 internal/native/tests/test_safe_reads.py
python3 internal/native/tests/test_projection.py
python3 internal/native/tests/test_visibility.py
python3 internal/native/tests/test_engine_visibility.py
python3 internal/native/tests/test_engine_aim.py
python3 internal/native/tests/test_engine_weapon.py
python3 internal/native/tests/test_entity_types.py
python3 internal/native/tests/test_fnames.py
```

The tests compile the production helper functions into isolated host programs.
The weapon fixture checks active-weapon selection, weak-reference serial layouts,
typed firing-component lookup, both recoil layouts, spread fields, restoration,
opaque-data preservation, recycled objects and checked-write failures.
The older engine visibility/aim fixtures currently reference removed GameThread
bridge markers and fail during extraction; they need updating for the current
scanner-thread implementation.
Memory tests use unreadable/unmapped pages and verify kernel-copy errors,
subsequent valid copies, partial/large transfers and unchanged host signal handlers.

Run the overlay regression on Android (adjust SDK paths if needed):

```sh
mkdir -p /tmp/delta-overlay-test
javac -source 8 -target 8 -Xlint:-options \
  -bootclasspath ~/Library/Android/sdk/platforms/android-31/android.jar \
  -d /tmp/delta-overlay-test \
  internal/native/overlay/O.java internal/native/tests/OverlayRegression.java
~/Library/Android/sdk/build-tools/35.0.0/d8 --min-api 26 \
  --output /tmp/delta-overlay-test /tmp/delta-overlay-test/*.class
adb push /tmp/delta-overlay-test/classes.dex /data/local/tmp/delta-overlay-regression.dex
adb shell 'CLASSPATH=/data/local/tmp/delta-overlay-regression.dex app_process /system/bin OverlayRegression'
```

This runs synthetic frames using the actual Android bitmap and Canvas APIs;
it does not start the game. It checks resize, frame publication, expiry,
completed-frame retention during an incomplete/busy producer, pinned-reader
exclusion, 2,000 reused frames, menu dragging/clamping, tabs, skeleton and colors.
It writes menu previews to `/data/local/tmp/delta-menu-{0,1,2}.png`.
It does not validate live camera/entity data, sustained flicker or game stability.
The visibility fixture checks production ray math and collection against synthetic
bounds: parallel/boundary intersections, partial cover, body-sized sampling,
query modes/channel responses, thin/large walls, managed bounds, nearby/endpoint
cover and unknown visibility when geometry is missing or incomplete. The Android fixture also checks
independent per-entity visible/occluded colors and persistence to disk.

To exercise the same checked-read fixture on Android, emit its C source with
`python3 internal/native/tests/test_safe_reads.py --emit-c /tmp/checked-reads.c`,
compile it with the NDK arm64 compiler and `-I external/scanner`, then push/run
the executable under `/data/local/tmp`. It reads only its own synthetic memory.

The entity fixture checks real-player team routing, world-NPC ancestry, native
class/parent validation when names fail, and one simulated-player category for
both teams. Visibility also checks target-owned component Outer exclusions.

Raycast debug is under **Status → Raycast debug**. It is off by default and
persists its four controls. Actor rays use BaseEyeHeight plus validated controller
rotation, with a labelled body-rotation fallback; the local ray uses CameraCache.
LOS draws the same five body samples used by visibility. Markers stop at the
nearest collected bound, and optional amber wireframes identify that bound.
These are bounds estimates, not engine physics traces. The nearest-hit fixture
checks list-order independence, exact entry distance, ownership exclusions,
missing geometry and origin-containing bounds without mutating LOS state.
The FName fixture covers the live encoded ASCII format, plaintext compatibility,
false sentinel rejection and unresolved wide names. The Android fixture checks
that debug controls reach native flags and persist independently.

The engine visibility fixture exercises the production request executor against
synthetic GObjects and a mock engine dispatcher. It checks the trace ABI,
centimetre inputs, explicit Visibility channel, self/target exclusion, all-five
blocked versus partial cover, failed dispatch, age and camera movement,
world/possession changes, recycled/pending-kill actors, and nonblocking locking.
It does not prove the native dispatch profile; that was verified against the
matching SDK, live metadata/native bytes and a firing-range run on 2026-11-15.

Internal body LOS colors now consume GameThread collision queries. Debug body
sample markers/LOS rows report those results. View-direction rays and optional
wireframes remain bounds estimates. Unknown or stale engine results are amber.

The internal aim fixture runs the production selector and GameThread controller
against a mock dispatcher. It checks the shared angular FOV/radius calculation,
zoom and boundary behavior, nearest eligible acquisition, sticky target identity,
rotation math, and one balanced SetIgnoreLookInput increment/decrement. It also
checks release on covered aim points, death, disable, expiry, possession change,
object recycling and scanner stop. The Android bitmap fixture checks an outlined,
centered acquisition circle and its presentation with ESP disabled.
