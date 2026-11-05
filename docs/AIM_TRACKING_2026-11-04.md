# Aim tracking validation — 2026-11-04

## Changes

Replaced the fixed 80 ms shell-swipe loop with a native multitouch bridge and a
16 ms controller that acts once per fresh camera pose. The bridge forwards
physical contacts and reserves separate aim/fire contacts. Contact output slots
append in arrival order; new contacts publish complete coordinates. Source axes
are cached from the event stream, and coordinates for inactive contacts never
reach the virtual screen. Short missing-camera intervals release aim input
without restarting the bridge or cancelling a walking finger.

The servo measures both axes with an isolated pulse, preserves measured gain
across tracking resets, scales gain on zoom changes, ignores older poses, and
subtracts camera corrections still in flight. Corrections are capped at 12 degrees
per update. Manual right-camera drags yield control for their full lifetime,
including crossing the screen centre; tracking resumes afterward.

Response is 10–100%, default 100%, replacing the old 60% cap. Bots, Enemy players,
and Friendly players / teammates are independent checkboxes, including none/all.
A live eligible acquired target remains selected outside the acquisition circle.
Missing data releases synthetic input immediately, with a 750 ms identity grace;
confirmed ineligible/dead actors cannot receive corrections. The torso target
uses the pelvis/spine blend for the verified compact skeleton and a capsule
fallback. It never uses an arm joint as the torso.

## Observed verification

- Android debug APK assembled and all **62 JVM tests passed** (zero failures,
  errors or skips). Coverage includes delayed camera feedback, calibration,
  reset/zoom behavior, physical-touch cancellation of probes, stable zoom and
  completed earlier corrections before probing, older poses, response levels, sticky selection, death,
  and target masks. Native regression passed on the device with warnings treated
  as errors: rotations, independent contacts, pointer order, rebasing, omitted
  axes, inactive-axis isolation, walking preservation and manual priority.
- Acquisition capture `acquisition-both-axes.json`: error decreased from
  421.34 px to 1.95 px within 0.38 seconds between recorded samples. This capture
  predates the later touch-forwarding fixes; it measures the current servo
  algorithm, not a zero-latency guarantee.
- `walking-gap-persistence.json`: 4.280 m sideways movement, same target in every
  sample, median error 1.96 px, maximum 3.43 px, zero faults. This was after the
  stale-source and camera-gap fixes.
- Range build, `walking-final-seeded-input.json`: nearer dummy retained
  throughout 1.447 m of sideways movement; camera yaw followed about 10.4 degrees.
  Median error 1.63 px, transient maximum 27.19 px, zero faults. Movement ended
  against range geometry. Small median error does not mean every frame is exact.
- `fire-source-cache.json` and `fire-gap-persistence.json`: actual firing consumed
  ammunition; the first also changed dummy health and the game's damage counter.
  No camera snap or calibration fault. The second retained the same target,
  median error 2.37 px, maximum 12.43 px through recoil.
- Live death: Bot 5 health decreased from 100 to 60.4, then a subsequent burst
  registered a kill in the range and removed it from the combat feed. In
  `death-release-live-second.json`, aim reported no eligible target at 0.30 s;
  the retained identity cleared at 1.09 s. Its label/skeleton disappeared while
  the range continued showing the dead mannequin. Synthetic corrections stopped.
- Range build, `manual-look-final.json`: moving the right camera finger
  yielded manual control and retained the same target. After release it resumed
  tracking at about 1.56 px error, with no fault.
- Device UI persisted checkbox masks 2, 0, 4, 5, 7, 3 and response endpoints
  10/100. Disabling aim removed the virtual touchscreen. Restored response 100%,
  bots/enemies enabled, friendlies disabled, auto shoot off. FOV was later changed
  on the device to 30%; this preference was preserved. The device subsequently
  entered a live match and automated gestures stopped.
- Game PID **27845** remained alive. Reader binary SHA256 remains
  `4eb927bf338df1d1b56d2c8b9464cd8cdb7b23b438729e1fff9b64de65bf8896`.
  `kptr_restrict` remains 2. No hooks or game-memory writes were added.

- A later status check exposed a false calibration fault during physical input
  and zoom overlap. The final build cancels a probe when physical input arrives,
  waits 400 ms after physical contact, waits for 100 ms of stable zoom, and waits
  for earlier corrections to finish before measuring sensitivity. Three new
  regression tests cover these cases. Earlier range-build passes alone did not
  establish this edge case; the failed status is retained in
  `calibration-overlap-fault.txt`.
- Final build read-only live-match captures: `live-match-isolated-probe.json`
  and `installed-final-status.txt` show measured sensitivity approximately
  0.20008/0.19996 degrees per pixel and tracked errors around 1–2 px before
  targets became ineligible. No fault was present in the captured samples.
  Overlay services were restarted directly without taking the game out of focus.

## Boundaries and corrected assumptions

The bridge currently supports the tested OnePlus 5 Synaptics s3320 touchscreen.
Android needs about 400 ms to register a newly created virtual touchscreen and
all existing fingers must be released before its first grab. Preparing it while
the Aim menu is open reduces this startup delay. Already-running tracking can
coexist with walking. Camera/touch latency, recoil, mesh availability and game
frame rate still apply; zero-delay or pixel-perfect tracking is not established.
The range supplies about 30 distinct camera poses per second. No new correction
is sent just because a display frame repeats an old camera pose.

Live firing/death tests used range dummies. Friendly/enemy player filtering was
verified in fixtures and menu persistence, not by firing on live players. A
recently rendered mesh does not establish line of sight; nearer obstacles can
block the selected target. Unverified positions and unknown health are excluded.

Some early input tests were invalid: evdev-injected coordinates do not update
all hardware-driver MT-slot ioctl state, and unchanged axes could be suppressed
on a repeated injected touch. The final private harness seeds both source axes
while inactive before DOWN; final walking/manual captures use that harness.
Earlier failed captures remain in the archive as diagnostic history, not passes.
The original transient-sensitivity resets and stale-coordinate forwarding were
also corrected; do not interpret pre-fix captures as final behavior.

Range selection also disproved the earlier guessed operator catalog. Only
**20003 → Hackclaw** currently has a verified name binding. Removed the eight
guessed bindings; other IDs show `Operator <id>`. See the corrected identity report.

## Installed artifacts

APK SHA256:
`4609c1915c1b8590fce8114eb0e442966e8dbbe715d89616c2d40cb2df1ce1e6`.

Native bridge / packaged asset / installed helper SHA256:
`98d00cce7b4255cde66986c0598b8923faf4499ca827e811fe342634a87694da`.

Installed APK and helper hashes matched the local outputs. Sources, binaries,
unit-test results, screenshots, input traces, failed investigations and final
validation captures are preserved privately with a SHA256 manifest at:

`/Users/dme/.copilot/session-state/95f558be-1d3e-4272-b379-88645453acdd/files/aim-tracking-final-20261104-180526`.
