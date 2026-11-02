# Live SDK repair — 2026-11-02

## Current result

The user confirmed nearby loot items and loot-container markers align perfectly
following the camera fix. Bots, loot and red human-player markers render on the
phone. Human-player coordinates are still experimental: one living named player
jumped tens of metres between samples. Visible human box alignment has not been
verified. Do not describe the full player overlay as complete.

The camera feed now runs independently of coordinate emulation (approximately
20–25 frames/s measured live). The latest draw was about 109 ms after feed receipt.
The user confirmed responsiveness improved. A debug capture
file is deliberately throttled to one write per 500 ms; its update cadence is
not the scanner frame rate.

## Repairs

- Matching bundled SDK offsets and bounded native-class inheritance anchors;
  matching both class and superclass indices avoids unstable blueprint indices.
- Current world transforms use scene component +0x210, translation +0x220.
  Encrypted transforms run in a local read-only Unicorn worker, separately from
  camera collection. Completed positions expire after 750 ms.
- Camera FOV is FMinimalViewInfo +0x1c. +0x20 is DesiredFOV. The original
  FPPCameraFOV override was also wrong for final projection. Live actual FOV was
  102.9 degrees; the wrong value was 85.7 degrees.
- All streamed world levels (up to 2047) and the battlefield subsystem roster
  are inspected. Dead actors are skipped before expensive emulation.
- Rendering accepts still-fresh, same-world projected frames while a newer
  frame arrives; this prevents the previous sequence-equality starvation.
  Display projection uses the real landscape display dimensions.
- Explicit background START_SCANNER/STOP_SCANNER service actions allow deployment
  without switching away from the game. Opening the app still does not start it.

## Validation and limits

29 JVM tests passed on the latest app build. Native SDK discovery/name/class
anchor/bounded-read fixtures passed. Eight Android instrumentation checks passed
before decoder integration, including composed-screen visibility, Hide/Show and
expiry against a 100 Hz synthetic feed. Do not run synthetic device tests during
an active match: they replace the live feed. Latest native integration and camera
responsiveness were additionally checked on the real device.

The two earlier game exits were recorded as SIGBUS; their cause was not proven.
The later session used process_vm_readv exclusively, with no ptrace, proc-mem
fallback, target writes or game restart. Do not attribute crash resolution to a
proven cause. No new game process exit occurred during the live repair session.

## Build and installed identity

Use Java 17, Android SDK and NDK 27.1.12297006. Set the machine SDK path in
android/local.properties. Build native and pin it before building the app:

```sh
python3 scanner/tools/build_scanner.py --ndk "$ANDROID_NDK_HOME" --pin-app
./android/gradlew -p android testDebugUnitTest assembleDebug lintDebug
```

The native manifest hashes every decoder source/header/static library and the
class-anchor include, in addition to memscan.c and the build script. The decoder
is GPL-2.0; its pinned provenance and modifications are documented in
[the dependency README](../scanner/vendor/dfm-decoder/README.md).

Current scanner SHA-256: `bc01d8424466b26c6017fd4b59f770d82f0cccea47fe48a55b09df6409e95a15`.
Current deployed path: `/data/local/tmp/delta-radar/scanner-api31-bc01d8424466b26c`.
The existing unidentified /data/local/tmp/memscan remains untouched.

Local screenshots, captures, APK backup, final-debug.apk, deployment proofs and
build logs live under ignored evidence/local/session. They include player names
and are intentionally excluded from commits. The final native binary and full
manifest are under ignored scanner/build/api31.

## Next live investigation

Responsiveness was confirmed by the user. Compare an actually visible live
human against its marker. Repeated remote decoder results for the first living
human were spatially inconsistent despite valid quaternion checks. Do not smooth
those into apparent accuracy or declare the decoder complete. Investigate whether
returned cache slots, emulated execution context or the supplied distant actor
state causes the inconsistent coordinates. The game process and all addresses
must be rediscovered for a new session.

The extraction ghost was captured as RUSNEKTO, alive health but AActor::bHidden
set (byte +0x90 = 0x85). The final scanner excludes this SDK hidden flag before
decoding. Actual bots had 0x04 and local pawn 0x84 (both unhidden). This explains
that ghost without establishing the earlier jumping player's exact cause.

Checkout used origin/master (381e876); origin has no main branch. The alex-dr
handoff was merged locally as 36cff65. No changes have been pushed.

The final filter was installed during extraction. The next capture showed the
lobby and an empty feed, so the exact phantom disappearance cannot be attributed
to the filter alone. A fresh visible-player comparison is still required.
Screenshot evidence: extraction-phantom.png and matching extraction capture JSON.
