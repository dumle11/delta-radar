# Cooperative internal integration

The existing Launch3r project is now in `launcher/`, and the existing native
library experiments are in `native/`. The review below predates those experiments;
it describes a cooperative design, not the behavior of the current payload.
Repository paths below have been updated for the workspace reorganization.

This folder records the implementation review and a design for integrating
telemetry into an Android application whose source we control.

## Current implementation review

- `external/android/app/build.gradle.kts` bundles `external/scanner/memscan-legacy` as an APK
  asset. `RootUtils.startScanner` deploys and launches it as a separate root
  process. Installing a library elsewhere would not switch this launch path.
- `external/scanner/memscan.c` already has an `IN_PROCESS` constructor. That constructor
  starts the existing scanner entry point on a detached worker thread with
  `run vmreadv`. It is not a cooperative engine integration: the scanner still
  discovers structures and reads process memory. Moving that worker into the
  target does not establish correct decoding, engine-thread access, object
  lifetime handling, or safe teardown.
- `EntityFeedService` accepts JSON from a named local socket without an
  application-level peer identity check. A cooperative replacement should
  authenticate its producer and bound individual message sizes. This is
  especially important when downstream consumers can produce input events.
- The recovery notes record an unexpected game exit after earlier injection
  experiments. Neither those observations nor the current external reader's
  stability demonstrate that an internal payload would be stable.
- The position investigation established that an encryption flag alone does
  not certify usable coordinates. Changing where code runs does not by itself
  supply a verified position source or prove anything about account policy.

## Supported architecture for a source-controlled application

1. Compile a telemetry provider into the host through its normal Android or
   engine build. Initialize and stop it through explicit host lifecycle calls.
2. Obtain actor positions through the host's supported, typed engine APIs on
   the appropriate engine thread. Copy values into an immutable frame before
   handing them to a transport worker; do not pass engine object pointers.
3. Include protocol version, session identity, frame sequence, capture time,
   units, coordinate convention, camera state, entity identity and validity.
   Explicitly invalidate a frame on teardown or session transitions.
4. Use an authenticated, bounded local transport to the Android viewer, or
   render directly inside the host's own UI. Reject incompatible, stale,
   incomplete and disconnected streams.
5. Enable the provider only in an explicit development build. A companion
   Android app can show connection status and telemetry, using the host's
   documented connection mechanism instead of root injection.

Implementing this requires the host project and its supported integration
points. A companion APK alone cannot provide a legitimate internal integration
with an unrelated application. No claim of undetectability or immunity to
security flags is made.

## Verification needed for an implementation

Use a controlled host scene with known entity locations to verify projection,
camera changes, spawn/despawn, session transitions, disconnects and teardown.
Check that the companion reconnects without retaining stale frames and that
the provider does not access engine objects from its transport thread.

This document is an architecture review, not a built or installed Android app.
