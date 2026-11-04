# Auto-pickup implementation checkpoint — 2026-11-03

## Scope and current status

The user paused pre-opening container-content research and chose a manual
**Take best loot** button for an already open container. The user subsequently
clarified that the same button must replace cheaper backpack loot when space
is full, using the game's normal automatic placement for pickups.
The phone reconnected for a completed live manual-transfer test. No new APK
was installed and no automatic item-transfer or game-memory-write code was
added. Subsequent lobby navigation used ordinary ADB taps to inspect UI only.

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
  slots at 64/group, copied-table guards at 4,096, data output at 4,096 records,
  memory reads at 131,072 and process lifetime at eight seconds. Limits/read
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

- 73 JVM tests pass, including 16 planner/session tests, ten replacement
  tests and seven layout-decoder tests. Tests cover value
  order, exclusions, fragmented storage, rotation, full storage, invalid
  identities, stale/future captures, cancellation, late/ambiguous confirmation
  and context changes.
- Android lint: zero errors, 168 existing warnings.
- Native owned-memory fixtures pass under AddressSanitizer/UBSan. They cover
  signed/high-bit GIDs, fractional/nonfinite values, concurrent payload change,
  invalid headers, exhausted reads, a realistic 1,725-name globals table,
  whole-context item/pricing output, mismatched GIDs and bounded output.
- ARM64/API-31 diagnostic compiles with warnings treated as errors.
- Ten Python capture tests pass without ADB: hash/staleness rejection, remote
  failure, truncation, partial coverage, changed process/context, lobby labeling,
  unexpected action flag and invalid manifest path.

Private build identity/reports: ignored
`scanner/build/pickup-probe-build.json` and
`evidence/local/session/auto-pickup-20261103/`.
The initial diagnostic was built while disconnected. On reconnection it was
deployed separately and exercised in the lobby (see below). Do not mistake
the older deployed loot probe manifest for this candidate.

## Reconnection and lobby preparation

USB/root access and the unchanged installed radar/native hashes were verified.
The game was in the lobby with one existing project scanner; it was left
running. No game restart, reinstall or item interaction was performed.

The initial diagnostic reached its 32K-read budget after 135 of 255 cached
Player items. The one-shot budget is now 128K, with the eight-second deadline
and output bounds unchanged. Player had 107 cached slot records, including
stash slots. The reader now selects only the freshly resolved backpack/rig
enum slots from that group, instead of rejecting the entire set at 64 slots.
This behavior is covered by a 107-slot owned fixture.

The latest separate diagnostic is `c4be83fc...`, deployed and hash-verified.
`reconnected-lobby-3.json` is a complete capture of its output: 264 cached
items across Player/Nearby/DeadBody, 705 copied tables and about 69K reads left.
Backpack/rig dimensions are zero in this lobby snapshot, and the inner native
completeness flag remains false. These are cached lobby records, not verified
live carried contents or an actionable pickup context. `actionReady` remains
false. The next useful sample is an actual open container in a match.

Private proof is under `evidence/local/session/auto-pickup-20261103/`.

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
Automatic removal was subsequently requested as part of the button; see the live-session scope below.

## First live pickup session and replacement scope

The user clarified that full-backpack replacement is part of the desired
button, not a separate future feature. `BackpackReplacementPlanner.kt` now
plans both free-space pickups and profitable replacements. It prioritizes
incoming individual value, then finds the cheapest complete set of blocking
backpack items needed to fit that object. The incoming value must strictly
exceed their combined value. Unknown prices, unsupported types, stacks and
explicitly protected items cannot be removed. Rig and secure-case grids are
not drop destinations. It emits a plan only; no automatic drop/input exists.

Ten replacement tests cover cheapest replacement, free space, multi-item
space requirements, strict combined-value gain, protected/unknown items,
fragmentation, whole blocking objects, stale/incomplete occupancy and incoming
value priority. All 66 JVM tests pass. Native ASan/UBSan fixtures and API-31
compilation pass; lint remains zero errors/168 warnings. Ten Python capture
tests pass. Reports are saved privately before restoring upstream-tracked
generated Android outputs.

Live observations (all transfers performed manually by the user):

- Reader `15030040011`, value 2,401, moved from Nearby slot 302 into Player
  backpack slot 108001 with the same GID. The screen changed from 0/15 to
  1/15 used cells. The user confirmed double-tap as the pickup gesture.
- In a player's loot box, Wood Pipe `15080050075`, value 14,171, appeared
  horizontally across two cells. Raw item `length=2,width=1` means columns=2,
  rows=1; raw slot `Length=3,Width=5` was the 3-column/5-row backpack. Position
  `Length/Height` are the placed footprint, including rotation. The game moved
  the same Pipe GID into the secure case, Player slot 109001, after double-tap.
  A predicted backpack fit is not an instruction controlling the destination.
- `_spaceBatch` exposes actual rig compartments with separate `index`,
  `length`, `height` and UI-relative coordinates. Normalize these individually;
  the rig's outer dimensions do not define one usable rectangular space.
- `_itemsPosition` provides X/Y, inclusive bounds, Length/Height, SubIndex and
  bRotated. The diagnostic now exports these structures and space batches,
  plus scalar bounds from absolute-location maps, without game calls.
- `LootingServer.GetCurrentSelectorData` is a getter for `_currentSelectorData`.
  The open storage box had selector type 1, with one interactor; closing that
  window removed the selector. A player box had type 2. The reader now emits
  selector state and the freshly resolved SceneBox enum, group completeness
  and root/header-stability flags. This does not yet map an item to a control.
- A later user-reported Reader drop removed its GID from Player, but that GID
  was present in the DeadBody cache's backpack slot. Ground placement is
  **unverified**; it could be a transfer or stale cache. Do not use simple
  disappearance as a successful ground-drop confirmation. The user confirmed
  dragging the Reader onto the normal trash/drop icon. This establishes the
  gesture, not the item’s final location in memory.
- The user upgraded the backpack during play (15 to 18 cells). Re-read actual
  dimensions and locations; never retain a spawn-time capacity assumption.

The live capture helper originally mistook diagnostics `accepted` for a bool.
It is the accepted snapshot object. Validation now requires that object and a
matching camera, with regression fixtures reflecting the app's actual schema.
No native/installed APK changes were needed for that host-side correction.

Main private evidence: `match1-container-open-1`, `match1-reader-picked`,
`match1-selector-and-layout`, `match1-closed-selector-ui`,
`match1-player-box-baseline`, `match1-pipe-picked`,
`match1-replacement-baseline`, `match1-reader-dropped`, and their comparison
summaries under the existing ignored session directory. An earlier second-box
sample was not claimed to be untouched after the user's correction.

Remaining work: connect the tested layout decoder to verified live table
copies, bind actual visible item controls, verify a real ground drop and
re-pickup, implement cancellation and
per-action confirmation, then integrate the button. Automatic replacement
remains unimplemented in the running app.

The user subsequently reported successful extraction with 1.4 million loot
(value not independently verified). Radar was stopped and zero project
scanner processes remained. No crash was reported during this match. The
match is finished; do not request further steps from that session.

## Postmatch UI research

The read-only diagnostic adds bounded Lua userdata inspection, registered
object discovery by class fragment, and cached-widget geometry inspection.
Candidate native objects must agree with their current registry slots;
geometry requires the verified Widget class/method and stable cached bytes.
These modes make no remote calls, writes or reference-count changes. They
are research helpers, separate from the production scanner. Cached widgets
may survive closure, so neither registry membership nor geometry proves a
visible, enabled item control.

The lobby inventory screen and a single-item details panel were opened
without transfers, sales, sorting or drops. The previously used Lua active
view deque was empty there despite the visible panel. Do not interpret that
manager as a complete inventory-widget source; current work checks native
widget candidates instead. UI binding and ground-drop proof remain open.

`PickupStorageLayout.kt` now converts typed copied slot/space/position fields
into planner grids and exact GID placements. It normalizes raw item length to
columns and raw width to rows, treats each rig compartment separately, and
checks inclusive bounds, rotation, capacity, duplicate/missing identities and
overlap. Locked, ambiguous or incomplete spaces reject the copy. It does not
parse native JSON, construct a full actionable snapshot or emit screen
coordinates. The live data adapter is still required. Seven synthetic tests
pass; all 73 JVM tests pass, lint remains zero errors/168 warnings, ten Python
checks pass, native owned-memory ASan/UBSan fixtures pass and the API31
diagnostic builds with warnings as errors. Reports are saved privately under
`layout-ui-checks/`.

Native candidate discovery completed across 258,412 registered objects and
returned 175 ItemDetail class candidates in this lobby sample. Most had no
usable cached geometry; several visible panel/button candidates did. An
ItemDetailView retained its positive cached geometry after the panel closed.
This directly confirms that geometry alone is insufficient to establish an
active control. The measured geometry also uses a different render coordinate
space from the 2400×1080 screenshot; a 1.2 scale matched this sample visually,
but no verified viewport-to-touch conversion has been implemented or pinned.
Do not hardcode that sample scale for input. Lua userdata-to-UObject inspection
compiles but has not yet been exercised against a real active view wrapper.
The next live hypothesis is to inspect the active in-match view's item data
and native widget wrapper together, then compare geometry with the visible
cell. No further item-value or pre-search-container test is needed for this.

## Second live match — 2026-11-04 checkpoint

The user entered another match with radar enabled and free backpack space.
Spawn/open-container/backpack-only captures all passed transport, live-frame
and native table-completeness checks. This match is still in progress at this
checkpoint; no outcome has been reported. All item actions remained manual.

The open Military Med Kit contained Medical Stapler `15080050238` (6,900),
toothpaste `15010010004` (2,420), and first-aid kit `14030000002`. No new price
panel comparison was needed. The Stapler was selected with one tap, then
picked up with the user's normal double tap. Its same GID moved from Nearby
slot 302 to Player backpack 108001; the source UI collection fell from three
entries to two and retained the other two items.

The relevant live hierarchy is:

- `Facade.UIManager._viewportPersistentRootUI` → `RootUI._childWidgets`
  → `MobileBagPanel._currentSubView.__ins_data__` → `LootingMainView`.
- Ordinary container: `_wtLootingLitter._wtContainerPanel` →
  `_wtContainerSlotView` (`InvSlotView`) → `_allItemViews`.
- Own backpack/rig: `_wtLootingEquipPanelV1._wtBagContainerView` /
  `_wtCHContainerView`. Right-side player-body panes are separate under
  `_wtLootDeadbodyPanel`; their cached slots are not the ordinary source.
- `_allItemViews` maps **ItemBase table keys to IVWarehouseTemplate values**.
  The value's `item` field exactly matched its key, and that ItemBase GID and
  catalog ID matched the inventory map for all three visible cells.
- `__cppinst` is a 24-byte Lua userdata whose payload offset 16 held the
  registry-verified `WBP_WarehouseItemTemplate_C` object directly. The native
  geometry helper worked for all three item cells. This exercises the earlier
  previously unverified userdata path against real live wrappers.

Correction to earlier exploration: `_deactiveUIList` is a cached-panel path,
not an established active-view list. It contained an old ItemDetailPanel while
none was visible, then became empty when the real details panel was selected.
Do not build action identity from it. String-key-only table listing also
concealed `_allItemViews` because its keys are tables. The diagnostic now
labels opaque table/userdata/lightuserdata keys without treating them as
commands. Raw keys/pointers remain private.

Cell `bIsShow` was false for all three visible items; `_is_enable_` was true,
`_has_destroy_` and `_bDraging` false, and `_viewSearchState` zero. Do not map
these raw flags to visibility/search semantics without further proof. Native
render-to-touch conversion and clipping still need explicit verification.

A separate ground-drop test now resolves the earlier Reader ambiguity. With
the container closed, a fresh backpack-only capture had no selector. Before
dropping, a complete nearby current-world registry capture had no Stapler
world pickup. The user dragged only the Stapler onto the normal drop icon.
It vanished from Player/cache items, and one new `BP_InventoryPickup_C`
appeared within six metres with **the same InventoryGID**, catalog ID,
count 1 and `IsInContainer=0`. This is positive ground-placement evidence.

The user picked that Stapler back up. Its same GID returned to backpack
108001. The old world actor still retained its InventoryGID/count briefly,
but its PickupItemInfo catalog cleared to zero/unknown. Do not require actor
disappearance after pickup, or accept a GID-only actor match as current loot.
A fresh valid matching catalog/count plus absence from carried inventory was
needed to establish the ground drop. The earlier Reader result remains
unverified and is not retroactively reclassified.

Main private evidence: `match2-container-open`, `match2-stapler-selected`,
`match2-source-view-set`, `match2-cell-geometries`,
`match2-cell-binding-comparison`, `match2-stapler-ui-transfer`,
`match2-backpack-only`, `match2-ground-before-drop-2`,
`match2-ground-after-drop`, `match2-ground-drop-comparison`,
`match2-stapler-repicked`, and `match2-repickup-comparison`. The first ground
capture failed before reading world data because camera IDs have an `ent_`
prefix; the private helper was corrected and a new baseline label used.

The user was released to continue playing. No further manual steps have been
requested. A bounded live UI acquisition adapter, reliable viewport/clipping
conversion, action lifecycle and app button remain to implement. There is
still no operational auto-pickup APK, automatic touch input or game write.

## Bounded UI reader candidate — 2026-11-04

`loot_probe.c pickup-ui` now uses copied Lua tables to follow the fresh
persistent RootUI and MobileBagPanel current subview. For an ordinary
SceneBox selector, it reads the source, own backpack and rig view maps. It
verifies ItemBase→cell→ItemBase pointer agreement, GID/catalog/count/shape,
slot group/type and the observed stable userdata wrapper before copying
registry/class-verified native geometry. Duplicate GIDs in one view map reject
the capture. It emits root render geometry and per-cell affine transforms,
raw flags and private widget/view tokens; these are not phone touch points.

Acquisition is one-shot, limited to 32K Lua reads, 64 root children, 256 cells
per slot, 4,096 output records, copied-table guards and the existing eight-
second process deadline. The same header/payload-copy and final-header
caveats apply: this is not an atomic inventory/UI transaction. Output always
keeps `actionReady: false`; it makes no remote calls or writes and sends no
input. Missing/unsupported UI paths remain explicit. Only ordinary SceneBox
is currently supported; player-body view routing remains to implement.

The capture tool accepts `--mode pickup-ui`, retaining pinned live-frame
before/after guards, current PID, schema/count/completion checks and private
non-overwriting labels. Thirteen Python fixtures pass, including separate UI
framing/truncation/action-flag/mode checks. Native owned-memory fixtures now
cover the live table-keyed link shape, signed high-bit GIDs, normalized
dimensions, raw-show=false preservation, wrong reverse link/group/slot, dead
view, wrong wrapper size and concurrent wrapper payload change. ASan/UBSan
and API31 warnings-as-errors builds pass. No Android sources changed in this
reader step; the prior 73 JVM tests/lint results remain the latest.

The deployed diagnostic hash and exact source inputs are recorded privately
in `scanner/build/pickup-probe-build.json`. Live `pickup-ui` captures with no
selector returned complete/stable/no cells, bound to the current match. The
normal pickup-context mode also passed a live regression capture after the
new dispatch was added. **The combined native reader's open-container branch
has not yet been live-verified.** The earlier successful cell observations
used separate exploratory reads and do not replace that check.

An optional request is pending for the user to confirm the next ordinary
container opened during normal play. Do not demand another price panel or
repeat the ground-drop test; both needed observations are already saved.
The match is still in progress with no outcome reported, and radar remains
running for play. No automatic input/button or updated production APK exists.
