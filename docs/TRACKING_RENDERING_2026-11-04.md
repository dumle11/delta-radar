# Tracking, camera timing and compact skeletons — November 4, 2026

The updated reader and APK are installed. This replaces the previous eighty-joint
rendering path with nineteen measured body joints and changes sampling,
publication and projection to reduce trailing during actor/camera movement.

## Delay sources removed

The previous pipeline added a 33 ms native sleep after extraction, a separate
33 ms projection timer, and a 50 ms screen-coordinate interpolation. It sampled
camera fields separately before reading all actors. The interpolation could
blend projections made with different camera orientations. Body geometry also
used edge-clamped capsule endpoints and a capped box height.

The native reader now has a separate read-only camera sampler and abstract
`delta_radar_camera` socket. Camera packets do not wait behind entity scans or
large pose packets. It double-reads the camera cache as one coherent block,
validates the handler and finite values, and records the first observation of
an advancing game camera timestamp. Frozen camera frames expire after 100 ms.
The source cache/world pair is published with an atomic sequence counter.

Entity extraction targets a 16 ms total interval, without adding a fixed 33 ms
delay to an already expensive iteration. Positions carry their own sample time.
The feed publishes immutable complete world snapshots; drawing does not take
the ingestion lock or repeatedly serialize every bone. Camera packets must
match that snapshot's process and world identities.

Projection runs on Android Choreographer callbacks and submits directly on the
main thread before traversal. Screen-coordinate interpolation is removed for
tracers, boxes and bones. All geometry uses the same camera, including roll.
Body boxes enclose projected joints with small padding; their height is not
capped, and body points are not clipped to tracer-edge positions.

A bounded world-space velocity estimate compensates running actors toward the
selected camera sample time. It never predicts the camera, never predicts more
than 75 ms, and rejects stale samples, implausible speeds and time reversal.
Combat entities older than 150 ms are omitted. This is latency compensation,
not a guarantee of correct prediction through abrupt motion changes.

## Compact body shape

The supported rig is identified by its complete 75-entry raw parent topology.
The selected nineteen actual joints cover pelvis, torso, neck/head, shoulders,
elbows, wrists, thighs, knees, ankles and toes. Intermediate parent links are
compressed to the nearest selected body ancestor. Finger, IK, weapon and other
helper chains are omitted. The positions remain animated 3D data; the old
screen-space stick figure has not been reinstated. Unknown topologies fail
closed instead of receiving guessed body indices.

## Validation and limits

- All 43 Android unit tests pass. New cases cover camera process/world identity,
  freshness, camera roll, bounded motion and teleport/time-reversal rejection.
- Native reader and compact-skeleton integration regressions pass. The owned
  camera-socket fixture verifies coordinates, changed rotation and frozen-frame
  expiry without connecting to the game or production camera socket.
- The final populated live capture contains 24 valid samples over 7.309 seconds,
  with camera yaw ranging about 12.7 degrees and pitch about 17 degrees.
  Camera state advanced 29.96 times/sec, camera packets arrived 115.06 times/sec,
  projection callbacks ran 53.36 times/sec, and entity packets arrived 49.94
  times/sec. Every verified skeleton had nineteen joints.
- Camera sample age was 19 ms median and 41 ms maximum in that capture. These
  are sample ages, not measurements of end-to-end visible alignment error.
  The capture contained 64 entities; ADB diagnostics also add some load.
- The same game PID remained running during installation and testing. Temporary
  investigation helpers were removed; no game hooks, protected proc-mem reads
  or game-memory writes were introduced.

Android display-vsync scheduling does not synchronize an external overlay to
the exact frame submitted by the game's render thread. Independently scheduled
sampling, animation, rendering and composition leave residual phase/error.
This update therefore does not claim zero lag, pixel-perfect alignment through
every motion, a guaranteed 60 FPS, or exact game-present locking. Exact locking
would require supported integration into the game's rendering pipeline, which
is not part of this external reader/overlay design.
