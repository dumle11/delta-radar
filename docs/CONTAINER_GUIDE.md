# Container navigation guide — 2026-11-05

The requested feature marks container locations for learning unfamiliar maps,
without requiring an item price or readable contents before search.

## Candidate behavior

- **Visual → Loot → Container guide** defaults ON. It draws yellow tracer
  lines, location boxes, actor names and distance for feed entities marked
  `kind=loot` and `lootContainer=true`.
- The guide works with ordinary loot disabled, its draw mode OFF, and
  **Valuable loot only** enabled. Loose-item prices and thresholds retain their
  existing behavior. Turning the guide OFF returns containers to the ordinary
  loot settings and value filter.
- It uses the existing render-distance setting. Camera-facing containers beyond
  the viewport use the existing edge diamond; containers behind the camera
  appear after turning toward them. On-screen guide boxes have a minimum 8 dp
  size so distant locations stay visible. These are location markers, not
  measured outlines of the container meshes.
- Guide labels do not claim a contents value. Detected empty or previously
  searched containers can remain marked: the feature shows locations, not a
  guarantee of remaining loot. The feed's container flag also includes death
  boxes and supported stashes/interactables.
- Existing Hide, frame/camera expiry, world invalidation and snapshot removal
  rules still apply.

## Discovery and limits

This is an Android-only change using the accepted scanner's existing
`lootContainer` field. Native source, deployment properties and scanner identity
are unchanged. No additional game-memory read or map-coordinate database was
added.

Earlier matched captures identified a regular safe
(`BP_Interactor_Container_SafeBox_C`) and flight case
(`BP_InteractorContainer_AirCargoContainer_C`) as container metadata kind 2,
including before their contents became readable. See the historical
[safe and flight-case findings](LOOT_VALUE_INVESTIGATION.md#flight-case-searched-contents-and-item-price-comparison-passed).
This supports using container identity independently of contents, but does not
verify every container on every map. Only actors discovered and positioned by
the existing scanner can be marked; unloaded or unsupported map objects are
not supplied by this change. Names can be generic, such as “Loot Container”.

## Validation

Four JVM regression cases cover unopened/cheap/valuable containers, guide
independence from loose-loot settings, ordinary-loot filtering, and restoring
the old rules with the guide OFF. The full JVM suite passes: 70 tests, zero
failures/errors. Debug app and instrumentation APK builds pass. Lint passes with
zero errors and 148 warnings. `git diff --check` passes.

The candidate APK and validation manifest are saved locally in
`evidence/local/session/container-guide-20261105/`. The APK is
`delta-radar-container-guide.apk`, SHA-256
`40098e955fe2b26312ca4331ea4beaf44692cb0283fc59b556765f68c49418b4`.

The synthetic `LootValueDeviceTest` now includes an unopened container and
checks guide ON/OFF with value filtering and loose loot OFF. Its earlier
body-only green edge-marker case explicitly disables the guide to preserve
that separate rendering check. These instrumentation tests were compiled, not
run on a phone for this change. No installation or live game check has been
performed. The accepted installed pair remains the 2026-11-04 build.

For a coordinated check, enable the guide beside an unopened safe/flight case,
keep the value filter ON and toggle ordinary loot OFF. Verify yellow lines,
boxes and distance, turn toward a container outside the viewport, then check
Hide and guide OFF. Repeat on the desired unfamiliar map to assess actual
scanner coverage.
