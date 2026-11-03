# Auto-pickup implementation checkpoint — 2026-11-03

## Scope and current status

The user paused pre-opening container-content research and chose a manual
**Take best loot** button for an already open container. Initial scope is
pickup into free backpack/rig space; replacement/drop behavior is deferred.
The phone was disconnected during development. No new APK was installed and
no item-transfer, touch-input or game-memory-write code was added.

This branch contains tested decision logic and a separate read-only research
reader. **It is not a working auto-pickup APK.** The overlay button and live
data/input adapters remain unimplemented because the active-window identity,
item-to-screen mapping and transfer result have not been verified.

Working installed radar/native remain `21cdaadf...` / `135e3893...`, pinned by
`scanner/deployment.properties`. Frozen branch `OnePlus-all-working-lines`
and tag `working-v1-2026-11-03` are unchanged.

## Implemented offline

`android/app/src/main/java/com/deltaradar/app/PickupPlanner.kt`:

- Uses a strict individual-unit threshold supplied by its caller, then ranks
  by absolute individual value, smaller footprint and stable item identity.
  This is not a maximum-total-value packing solver or a value-per-cell policy.
- Accepts searched, nonstacking ordinary collectibles with verified dynamic
  guide prices. Gear, keys, unknown prices, unfinished search and stack merging
  are excluded from this first transfer contract.
- Finds a fitting rectangle in explicitly supplied free grids, including
  rotation; fragmented empty cells do not imply usable rectangular space.
- Returns one candidate at a time. Its session waits for that exact item GID
  and catalog ID in carried inventory, absent from the source, in a newer
  capture taken after the proposed action. Disappearance alone is insufficient.
- Stops on stale/incomplete data, changed match/container/window generation,
  backwards captures, cancellation or unconfirmed transfer timeout. A stopped
  session requires another button press. There are no retry/drop/swap commands.

No production service calls the planner. An eventual adapter must establish
all `Snapshot` fields from verified live state. In particular, do not turn a
world marker, last-searched object or the whole Nearby cache into an actionable
open-container snapshot. `session` must invalidate on match/life changes, and
`window` must change when a loot window is closed and reopened. Snapshots must
be immutable for the duration of a decision. Execute acquisition/planning
away from Android's main thread.

`scanner/tools/loot_probe.c pickup-context`, implemented in
`scanner/tools/pickup_context.h`:

- Revalidates the main Lua registry and resolves Player/Nearby/DeadBody group
  enums and named storage-slot enums from fresh roots.
- Copies per-item GID, catalog ID, count, dimensions, maximum stack count,
  slot group/type and supported dynamic unit prices.
- Copies slot dimensions and bounded opaque position/space-map entries.
  Their coordinate conventions, occupancy meaning, search state and UI
  correspondence are **not established**. `searchDoneStamp` is a raw field,
  not proof that an item is visible/searched for this player.
- Verifies map-key/GID agreement and per-table headers/payload rereads. Roots
  are checked again at completion. These checks do not make the multi-table
  read an atomic transaction.
- Bounds hash tables at 8,192 entries, arrays at 1,024, items at 256/group,
  slots at 64/group, copied-table guards at 600, data output at 4,096 records,
  memory reads at 32,768 and process lifetime at eight seconds. Limits/read
  failures remain explicit incomplete results, never known-empty contents.
  A missing inventory group also keeps native completeness false.
- Always emits `actionReady: false`. It has no Lua invocation or game writes.

`scanner/tools/capture_pickup_context.py` runs that diagnostic without
deploying, starting radar or sending input. It verifies the content-addressed
binary hash, accepted pinned frames before/after, unchanged camera identity
and current game PID. Lobby mode is explicitly not live-match proof. Capture
completion and native table completeness are separate fields; neither permits
actions. Raw output stays inside ignored `evidence/local/`, and a duplicate
capture label cannot overwrite existing evidence.

## Validation

- 56 JVM tests pass, including 16 planner/session tests. Tests cover value
  order, exclusions, fragmented storage, rotation, full storage, invalid
  identities, stale/future captures, cancellation, late/ambiguous confirmation
  and context changes.
- Android lint: zero errors, 168 existing warnings.
- Native owned-memory fixtures pass under AddressSanitizer/UBSan. They cover
  signed/high-bit GIDs, fractional/nonfinite values, concurrent payload change,
  invalid headers, exhausted reads, a realistic 1,725-name globals table,
  whole-context item/pricing output, mismatched GIDs and bounded output.
- ARM64/API-31 diagnostic compiles with warnings treated as errors.
- Nine Python capture tests pass without ADB: hash/staleness rejection, remote
  failure, truncation, partial coverage, changed process/context, lobby labeling,
  unexpected action flag and invalid manifest path.

Private build identity/reports: ignored
`scanner/build/pickup-probe-build.json` and
`evidence/local/session/auto-pickup-20261103/`.
The diagnostic is **built only, not deployed or live-tested**. Do not mistake
the older deployed loot probe manifest for this candidate.

## Next live work after reconnection

1. Verify the unchanged installed radar and deploy only the separate
   content-addressed pickup diagnostic. Run a lobby read first to check the
   actual Lua schema and read budget. Do not reinstall/restart the game.
2. With a searched ordinary container open and a cheap item left inside,
   capture context, screen and fresh feed together. Check every item, its slot
   and actual carried-grid occupancy, including unpriced gear and subgrids.
3. Trace `LootingServer.GetCurrentSelectorData` read-only to identify the
   current selector/window. Saved `GetCurrentInteractingBox` metadata calls
   it and checks SceneBox/NearbyDeadBody/DropContainer types. The last-search
   object and Nearby cache alone are not enough. Do not invoke these methods.
4. Manually pick up just one cheap item; compare source GID and carried GID,
   count, search state, slot and grid location before/after. Verify that rotation
   and container closure are detected. No expensive item is needed.
5. Establish an item-to-visible-control mapping and verify one deliberate
   transfer before wiring the overlay button. No guessed fixed-coordinate taps.
   Then test cancel/closed window, full inventory, interrupted transfer and
   another container. Player boxes and stacking need separate acceptance.

Keep auto-pickup's eventual minimum separate from the diagnostic marker minimum
(currently 0). The intended normal loot marker cutoff remains >20,000 per item.
Automatic removal of carried items remains a later feature.
