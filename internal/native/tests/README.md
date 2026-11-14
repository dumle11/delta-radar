Run host regressions from the repository root:

```sh
python3 internal/native/tests/test_safe_reads.py
python3 internal/native/tests/test_projection.py
python3 internal/native/tests/test_visibility.py
```

The tests compile the production helper functions into isolated host programs.
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
