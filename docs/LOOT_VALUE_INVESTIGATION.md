# Loot value investigation — 2026-11-03

Historical investigation. Implementation, installed build and working-branch
decisions advanced afterward; see [CURRENT_STATUS.md](CURRENT_STATUS.md).

Development branch: `codex/loot-value-investigation`. Preserve remote
`OnePlus-all-working-lines` and tag `working-v1-2026-11-03` as the working baseline.
The installed APK, pinned production scanner and production sources were not
changed for this investigation. The user enabled the existing radar and played.

**Update:** the investigation below led to an implementation on this development
branch. The first live integrated check exposed an empty auction cache; the
latest correction uses ShopServer's dynamic guide prices instead. See the last
section for the current candidate. The preserved working branch/tag are unchanged.

## Intended feature

The user initially requested 50,000, then changed the test/default threshold to
**each individual item worth strictly more than 20,000**. The implemented minimum
is adjustable from zero to 500,000 in steps of 1,000. Use unit
value for eligibility; do not promote a stack of individually cheap items just
because its combined value exceeds the threshold. Confirm the price basis
against the value displayed in the game before implementing the threshold.

## Live observations

A read-only diagnostic used reflected properties from the current game process.
It read item identities, counts and container state without calling game
functions, changing game memory, pausing the game or replacing either app.
The canonical name-pool globals required an `O_RDONLY` proc-memory descriptor;
the older supposed readable name-pool alias is unrelated text and was excluded.

The user stood beside an unopened clothing container, searched it while leaving
its items inside, displayed an item's details, then picked up only that item.
The same actor was compared across those three phases:

| Phase | Search complete | Locally readable item entries | Empty flag |
| --- | --- | --- | --- |
| Beside unopened container | No | None | No |
| After search | Yes | Document + Portrait Photo, one each | No |
| After picking up the photo | Yes | Document only | No |

This establishes that the container position was available before its contents
were available through this reader, and that the item list updated on pickup.
It does **not** establish when the server generated the loot, whether another
client knew its contents, or the behavior of every map/container type. An empty
local array in an unsearched container means unknown contents, not proven empty.
The remaining container marker can be valid because the container still exists.
The user was attacked and died after the pickup check; no old actor address may
be carried into a new match. A completely emptied-container check is outstanding.

Loose pickup actors also exposed item identifiers and stack counts. Identifiers
in `InventoryIdName` agreed with `PickupItemInfo.ItemID` in the observed samples:
the decimal item ID is Category * 10000 + Sequence. Validate both sources rather
than treating the per-instance InventoryGID as a catalog item ID.

## Price findings

Two loaded tables were found by traversing the bounded UObject registry and
validating the DataTable class and row-structure identity:

- `ItemPriceTable`, using `FItemPriceRow`, contained 166 sparse slots in the
  sample. It did not have an entry for the tested photo or document.
- `GameItem`, using `FDFMCommonItemRow`, contained 16,426 sparse slots and
  resolved all 30 distinct item/container catalog IDs in the initial comparison.
  Its fields distinguish initial guide price, recycle price, quality and counts.

The **Portrait Photo** (item ID `15090010026`) displayed **2,072** in the game's
item-details panel. Its `GameItem.InitialGuidePrice` was **1,864**, and its
`RecyclePrice` was zero. Thus neither the static initial guide price nor a
missing/zero recycle price can silently stand in for the displayed value.
The document's item ID was `15080050056`, initial guide price 814; its displayed
value was not checked. No value near or above the 50k cutoff was verified.

Read-only inspection of the native price methods supplied further leads:

- `GPInventoryBase.GetPrice` / `GetSellPrice` native wrappers are at module
  `+0x15d3f1c4` / `+0x15d3f200`. Their CDO virtual slots `0x7c0` / `0x7c8`
  resolve to module `+0x14b006d8` / `+0x14b007ec` in this build.
- The ordinary GetPrice path converts Category/Sequence into the catalog ID,
  calls module `+0x1606a3c8` and multiplies by item count.
- That helper includes patch-dispatch paths and a converted-row lookup. Its
  ordinary branch prefers a positive unreflected row slot `+0x23c`, then the
  initial guide price at `+0xdc`. This is an analysis lead, not a verified
  algorithm for the value displayed in the UI.
- The unreflected slot was zero in the original GameItem table rows inspected.
  Reading it directly from those rows did not solve the discrepancy. The actual
  converted/runtime value source and any patch-dispatch behavior remain open.

Do not apply a guessed multiplier to reconcile the two photo prices. Do not
claim that the displayed price is necessarily a current market quote merely
because it differs from the initial guide price.

## Diagnostic tools and validation

`scanner/tools/loot_probe.c` is a separate, one-shot diagnostic. Modes inspect
schemas, specified loot actors, relevant tables/rows and price-method addresses.
Property/array traversal is capped, the UObject registry traversal is capped at
300,000 entries, arrays/bitmaps are checked, and an eight-second alarm bounds
each invocation. It does not connect to the overlay feed or run persistently.

Build with the installed API-31 NDK compiler:

```sh
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android31-clang" \
  -std=gnu11 -O2 -Wall -Wextra -Werror -fPIE -pie \
  -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
  scanner/tools/loot_probe.c -lm -o scanner/build/loot-probe-api31
```

Deploy that diagnostic under its own content-addressed temporary filename, not
the production scanner path. The local deployment record is
`scanner/build/loot-probe-deployment.json`; it is not an application deployment
pin. `scanner/tools/capture_loot_snapshot.py` samples an already running radar,
requires the pinned scanner hash and a fresh frame, then inspects a bounded set
of nearest/watched loot actors. It never starts/stops or installs either app.

The API-31 diagnostic compiled with warnings treated as errors. The host capture
script passed Python compilation and CLI checks. Both executed successfully on
the phone; the matched before/search/pickup samples are the meaningful live
validation. Production build/tests were not rerun because runtime sources were
unchanged.

Private evidence is ignored under `evidence/local/session/loot-20261103/`:
matched phase snapshots, screenshots, table rows, native code excerpts and
`comparison-summary.json`. Do not commit screenshots, actor addresses, copied
game pages, player identities or account logs. The diagnostic process ended
after each sample; the user retained control of the existing radar.

## Next work

1. Run the integrated candidate in a new match, checking metadata availability,
   positions and camera/feed responsiveness. Test real values around 20k and
   verify expensive/modded equipment separately before assuming its cached
   default quote accounts for all condition/attachment adjustments.
2. Repeat the before/search/pickup/empty sequence for other container types;
   include proximity observations with fresh actors from the current match.
3. Catalog item names are not implemented; labels retain their existing names
   and append the known unit value. Single-item stash/interactable metadata is
   unknown unless a separately enumerated pickup supplies identified contents.
4. Unknown/unsearched containers must not qualify as valuable without identified
   contents. Preserve a separate optional general-container display.
5. Preserve the baseline and existing coordinate/render scheduling paths; recheck
   camera responsiveness and full-match stability after actual integration.

## Initial auction-cache candidate (superseded)

This section records the first candidate. Its numerical matches were real, but
the next match proved that this auction cache can be empty. Its source and build
identities below are historical; use the correction in the last section.

The second match supplied loose-item details and a battery-only pickup check.
That pickup removed exactly item ID `15080050111` from the fresh loot feed,
confirming the battery's identity. The user died after those samples were saved.
No old match actor address was reused afterward. Lowering the test threshold
does not change the price-source requirement.

Read-only inspection of the Lua main-thread registry revealed this runtime path:

```text
_G.Server.AuctionServer._mapItemId2AuctionPrice[itemId]
    .mapAucitonPrice[0].auctionPrice
```

`mapAucitonPrice` is the actual spelling. The surrounding record's
`currencyClientType` must equal 1. This is a cached client value: the reader does
not request new prices, invoke Lua functions or contact a server. Three runtime
values matched the captured item-details displays exactly:

| Item | Catalog ID | Captured UI | Runtime cache | Static initial guide |
| --- | --- | ---: | ---: | ---: |
| Portrait Photo | `15090010026` | 2,072 | 2,072 | 1,864 |
| 9V Battery | `15080050111` | 2,798 | 2,798 | 2,613 |
| Leveler | `15020040004` | 3,672 | 3,672 | 3,681 |

The separately inspected `AuctionProp` data table contained only 91 sparse slots
and had no entry for these items; it did not resolve the discrepancy.
The observed native price-dispatch override bytes were zero, so simply applying
the static native GetPrice path would not reproduce these displayed values.

The main VM pointer is at module `+0x1fc9cd70`. The reader validates thread type,
global state, registry type and its main-thread entry before following globals.
In this build: global state is thread `+0x18`, the registry TValue is global
state `+0x40`, and registry array entries 1/2 are main thread / globals.
Lua table key/value layouts were validated read-only in this process; don't
assume these offsets remain valid across a game update.

The new `scanner/loot_value.h` path uses bounded table reads and validates prices
as positive, whole, finite values no greater than one billion. Missing values
remain explicitly unknown. A standalone invocation of this production header
on the phone matched the three prices, rejected an absent item ID, and measured
2 ms for discovery plus its five sample lookups. This is one sample, not a
latency guarantee. The SDK was the source for actor/item layouts; Tencent's
[slua LuaState source](https://github.com/Tencent/sluaunreal/blob/master/Plugins/slua_unreal/Source/slua_unreal/Public/LuaState.h)
was consulted as background, and the actual game layout was independently
validated through the registry and captured price comparisons.

Implementation behavior:

- Verified InventoryPickup descendants expose item IDs and unit values. For a
  container, the feed selects its highest **known individual item value**, not a
  total. Stacks do not multiply eligibility. Empty/unavailable contents provide
  no invented price.
- New feed fields are `lootItemId`, `lootUnitValue`, `lootValueKnown`,
  `lootContentsKnown`, `lootContainer`, and `lootValueSource`. The only accepted
  known source was `auction-cache` (superseded below).
- Metadata is cached for at most 500 ms per actor; actor caches clear on world
  change. Runtime price nodes refresh at one second, with root rediscovery at
  five seconds. Each frame has a 4,096-read metadata budget. Native inheritance
  admission is cached by class pointer/index. Coordinate and decoder paths stay
  separate from metadata.
- A dedicated read-only proc descriptor may read only the known eight-byte
  Lua main-state global if normal reads fail. It does not change the scanner's
  general reader/name decoding or force-read arbitrary game memory.
- The native packet buffer was enlarged to accommodate the extra fields at the
  existing entity limit.
- Visual → Loot now includes **Valuable loot only** and an adjustable minimum,
  default 20,000. Filtering defaults OFF to preserve existing settings; turn it
  ON for the new behavior. Known labels append a unit value. OFF preserves the
  all-loot display; ON hides unknown contents/prices and exact-boundary values.

Candidate identities (local evidence `loot-value-build-20261103`):

- APK SHA256: `e281215515aa7e73fdb982f15c9304f76e6ff8cbc0478d6471a8193bb3b8e2b8`.
- Native SHA256: `6cbc42d416665f12862c13e9c8ba16211d90f0eafa4f5941d4525db739e628cc`.
- Native source-input SHA256: `913170a2f8ad8ab3079fa9555072d7ea1b88ce50ca91f76c05818b67eb75250e`.
- Manifest provenance base: `ced06d3`, plus the hashes of the modified inputs.

Validation passed: API-31 native build; owned-memory loot fixtures under ASan and
UBSan (including unknown/empty contents, individual stack values, pickup updates,
currency checks, budget exhaustion/recovery and disconnected price state);
enemy/loot coordinate-source, roster, timestamp and discovery regressions;
40 JVM tests; APK/device-test builds; lint with zero errors and 168 warnings.
Installed APK hash and embedded native pin were verified, and the native binary
identity/hash was verified at its content-addressed path. The preexisting
unrelated scanner was preserved. The game's PID remained unchanged throughout
installation and tests.

Two on-phone instrumented tests passed: synthetic values through the real feed
and overlay service (20k strict boundary, cheap stack, unknown/static values,
qualifying container item, all-loot toggle and changing to 50k), and the existing
composed-window pixel/Hide/Show/expiry check. These are labelled fixtures, not
live-position/full-match evidence. Original preferences were restored and the
radar was stopped afterward, awaiting a fresh-match check. SIGBUS remains open.

New private evidence is under `evidence/local/session/loot-price-followup-20261103/`
and `evidence/local/session/loot-value-build-20261103/`. Keep raw Lua table addresses,
captures and screenshots local. The working checkpoint branch/tag was not moved.


## Live regression and dynamic-guide correction — 2026-11-03

The user entered another match with the value filter OFF. The expected scanner
was running and the new metadata fields were present. A short sample observed
about 21 accepted frames/second; this does not establish full-match performance.
The scanner identified searched container contents and loose item IDs, but all
prices were unknown. The auction-cache table had no entries, including after
opening an item's details. Matching three earlier auction quotes was insufficient
proof that this cache would remain populated across matches.

Two new screenshots supplied exact displayed prices. The safe held a Pirate
Gold Coin and a cutlass; the coin's individual details showed **57,757**. The
briefcase held Trade Documents, whose details showed **8,005**. The user died
after these captures; the scanner was stopped and no further actor inspection
used those match addresses.

Read-only inspection of the loaded item-details and ItemBase methods led to
`ShopServer.GetShopSingleDynamicGuidePriceByItem`. `ItemBase.GetSingleSellPrice`
and `GetSingleMallSellPrice` reference that method. The normal shop's live map is:

```text
_G.Server.ShopServer._data.Normal._mapItemId2DynamicGuidePrice[itemId]
```

This map contains direct integer prices and had 8,192 hash slots. It reproduced
both new UI prices and the three previously captured displays:

| Item | Catalog ID | Captured UI / dynamic guide | Static initial guide |
| --- | --- | ---: | ---: |
| Trade Documents | `15070040002` | 8,005 | 7,961 |
| Pirate Gold Coin | `15080050009` | 57,757 | 61,304 |
| Portrait Photo | `15090010026` | 2,072 | 1,864 |
| 9V Battery | `15080050111` | 2,798 | 2,613 |
| Leveler | `15020040004` | 3,672 | 3,681 |

The revised production header returned these five prices from the phone in one
bounded standalone invocation (2 ms in that sample). Neither it nor the separate
diagnostic calls game functions, writes game memory or requests network prices.
The diagnostic now supports bounded Lua-closure constants/upvalues/instructions;
its layouts were checked against the official
[Lua 5.3 object definitions](https://github.com/lua/lua/blob/v5.3.6/lobject.h)
and validated on the target's actual closure/prototype tags. All copied runtime
content remains in ignored local evidence.

The filter now accepts only `shop-dynamic-guide` prices. It rejects the old
auction source, static guesses and malformed prices. Root discovery also checks
`ShopServer._shopType == "Normal"`; it fails closed in other shop modes. The
existing refresh intervals, actor cache, read budget and projection behavior
remain unchanged.

Known eligibility is limited to **EItemType.CollectableItem (15)**, ordinary
subtypes 1–9 excluding **ECollectableType.Key (5)**, verified from the runtime
enums. Weapons, equipment, medicine, keys and other special types can
need condition/attachment adjustments; an unadjusted map quote must not mark
them as valuable. They remain unknown and are hidden when the value filter is
ON. Their IDs are still reported, and the all-loot display remains available
when OFF. The UI explains this scope. Expensive gear support is outstanding.

Current candidate (private artifacts: `loot-guide-collectibles-build-20261103`):

- APK SHA256: `7e584957705c64eaa19091a82b08bfc5f86585a3c36fe447b6b683493d42552a`.
- Native SHA256: `de99f1ca597ad8e3efb29ff7015a5b7346463603aeb968081cd043bda0c0a375`.
- Native source-input SHA256: `b08115d00649ecdffc50e30f09af0802e008b8eab96b5f2acfb54b4305014181`.
- Manifest base: `be20703`, plus modified source-input hashes.

Validation: API-31 native build; updated owned-memory fixtures under ASan/UBSan
covering the dynamic path, invalid prices, excluded unadjusted gear/keys, shop-mode
mismatch, stack unit values, container updates, budget and disconnect behavior;
40 JVM tests with no failures/errors; APK/device-test builds; lint with zero
errors and 168 warnings. Installed APK/native hashes and the embedded native pin were verified, and
the game PID was unchanged. Installation/service checks are recorded in the
current build's private evidence. Both on-phone tests passed (synthetic value filtering and composed-window
pixel/Hide/Show/expiry). The fully integrated scanner still needs
a new-match comparison after this correction. SIGBUS/full-match acceptance
remains outstanding.

The match samples, screenshots, runtime price-path records and diagnostic dumps
are under `evidence/local/session/loot-value-live-20261103/`. Keep them private.
The working baseline branch/tag was not moved.

## Corrected candidate: first integrated live filter pass

On the next spawn, the content-pinned `de99f1ca...` scanner supplied fresh,
non-test player/AI/loot frames. The initial sample had seven known loot prices,
including a loose item worth 33,624. Before enabling the filter, one sampled
overlay output included 25 unknown and two known cheap loot markers alongside
qualifying loot. Counts/framing changed while the user played.

The user enabled **Valuable loot only** at **20,000**. Four fresh, valid overlay
output samples were matched by entity ID to the live native feed. Every
projected loot marker had a known `shop-dynamic-guide` value strictly above the
threshold; none was cheap, unknown or unmatched. The observed remaining item
had catalog ID `15020010015` and runtime value **42,478**. That value was not
separately compared to this item's in-game details panel. Player/bot output
continued. The user confirmed: **"Looks right; tracking is responsive."**

This passes the integrated filter/output and user visual-response check for
this match. It does not resolve gear/keys, complete pickup/empty behavior,
cross-map acceptance or full-match/SIGBUS stability. The user then died and
chose to test another map; no crash was reported. The scanner was stopped
between matches and the filter setting was retained at ON/20k. Future samples
must rediscover actors in that new match.

Private before/after feed and overlay captures, a screenshot and the summarized
result are under `evidence/local/session/loot-guide-verification-20261103/`.
No source/runtime changes were needed for this verification.

## Repeat Zero Dam load; other-map test deferred

Other-map matchmaking was too slow for the user's lunch break, so they reported
returning to **Zero Dam, Normal**, with the radar ON. The filter remained ON at
20k. The current feed peer's `/proc/PID/exe` matched the pinned scanner path.
Initial samples contained unsearched containers; later samples supplied 12–13
priced loot entries and a qualifying **22,137** item. Player/bot output continued.

Capture comparisons need care: scanner and overlay diagnostic files are each
throttled independently at 500 ms. Seven of eight sampled pairs contained no
cheap/unknown projected loot. One pair differed by nine feed sequences / 497 ms
while a container's contents changed; it cannot establish a filter violation.
A subsequent short capture found no exact sequence pairs, so this repeat is
recorded as price availability across a new match, not an additional strict
per-frame filter acceptance. Prefer matching sequences or explicitly treat
transitional comparisons as inconclusive.

The game was subsequently observed on its post-match season-pass progress
screen. The user has not confirmed whether they died or extracted in this
repeat. Scanner was stopped; no app/runtime changes were needed. Another map,
pickup/empty-container behavior and full-match/SIGBUS acceptance remain pending.

Operational checks: `ps ARGS` did not reliably include the executable's full
path on this device, so prefix matching can falsely report no scanner. Use the
fresh feed's peer PID and verify its `/proc/PID/exe`. A fresh scanner diagnostic
timestamp alone also does not establish an active match: check overlay age and
game state before interpreting it. Private evidence is under
`evidence/local/session/loot-guide-zero-dam-repeat-20261103/`.


## Container coverage and edge-marker candidate — 2026-11-03

The user reports loose map loot working, but inconsistent detection from flight
containers, sewers, bird nests, suitcases and safes, including special safes that
appear at varying locations. Treat these as unresolved per-family coverage;
the earlier visual filter pass does not establish universal container support.

Further review found a same-sequence capture (3271) of a nearby container with
item `15040050002`, runtime unit value **2,050,520**, eligible at 20k. The
corresponding overlay output contained it, but `onScreen=false` at normalized
(0.4275, 0.982). Its origin was about 1.1m below the camera. This is consistent
with the reported red/bust item, but its exact name/container class was not
captured. The renderer skipped offscreen bodies, leaving only whichever line
or tiny label features were enabled. The candidate draws an inset loot-edge
diamond when body display is enabled, while retaining normal on-screen boxes.
A diamond indicates direction toward an offscreen origin, not a world-space box.

A separate source gap was explicit: kind-3 stash/interactable values were always
unknown. The candidate now follows these verified native layouts:

- `Interactor_SingleItemContainer` (class 7559 / parent 7557): `CachedPickups`
  at 0x1128, plus `CachedOpenBox` at 0x1180.
- `ChangeableContainer` (class 6322 / parent 57): `CacheSpawnActor` at 0x378;
  preview actors are ignored.
- Linked actors must independently pass native class/superclass validation.
  Pickup/container/death-box descendants use their corresponding existing item
  reader. Hidden pickups (`bIsHiddenItem` at 0xfc0) do not retain stale values.

Class definitions and field offsets were checked against the SDK and read-only
reflection of the phone's loaded classes. Contents are walked at most four
levels and 128 distinct actors, with array bounds and the shared 4096-read/frame
budget. Cycles/invalid references terminate without inventing prices. The parent
uses the highest known individual child-item value, never a sum. The 500ms actor
cache and ordinary-collectible price scope remain. Class-kind caches reset on
world change. Actor positions and character decoder paths are unchanged.

This adds a missing path; **live coverage for each reported container family is
still pending**. Unsearched or unreplicated contents remain unknown. A special
container with neither readable linked pickups nor an open-box/spawned actor
will still have no value marker while the filter is ON. No old match addresses
were inspected after death; class reflection used only current static objects.

Candidate identities (ignored `container-links-build-20261103`):

- APK: `21cdaadfad5223cf5b187514fa415bbbe8d846ac53cedec9d2dd9c0ce9a86e6c`.
- Native: `135e3893f9e502f50c71f6d39d1801e0470d9bd05215b9223dee802e6f535d14`.
- Source inputs: `0501dd19430030b649815baf2f80b3c2bf531d05fdcadc5a5634be1a8cb5009d`.
- Build base: `c14dcb6` plus modified input hashes.

Native graph fixtures pass under ASan/UBSan (nested links, hidden children,
invalid arrays, cycle termination and class/superclass mismatch). Position,
feed timestamp, roster and discovery regressions pass. API31/APK/device-test
builds, 40 JVM tests and lint pass (zero lint errors, 168 warnings). The new
on-phone test reproduces the captured safe/camera offset using labelled synthetic
input, body-only loot style and a composed-screen shown-versus-hidden pixel
comparison. All three on-phone tests passed, including that composed-pixel check. Installed
APK/native hashes and embedded native pin match; game PID was unchanged. The
user's pre-test minimum was 0 with the filter ON and was preserved; 20k remains
the intended normal-play threshold. Device results and exact artifacts are
saved locally. Live family
and pickup/empty comparisons are the next required checks.

### Later feature: automatic loot sorting (requested, not implemented)

When the user opens a loot container or player box, take its most valuable loot;
if the backpack/rig is full, replace lower-value carried loot with higher-value
loot. Record this as a later feature, after container coverage is reliable.
It needs verified complete inventory contents, item dimensions/occupied slots,
stack/value handling and transfer/drop-result confirmation. Current collectible
pricing alone is insufficient to rank gear/keys and all player-box contents.
No automatic item transfers or drops have been added.


## Candidate live check: regular safe search, pickup and empty passed

On the installed `135e3893...` candidate, a fresh pinned native feed and its
actual peer executable were verified. The user was on **Zero Dam Easy**
(observed HUD), with value filtering ON and minimum 0 for container diagnosis.
The matched actor was `BP_Interactor_Container_SafeBox_C`, metadata kind 2.

| Phase | Readable item count | Highest known unit value | Marker |
| --- | ---: | ---: | --- |
| Unsearched | 0, search incomplete | Unknown | Hidden |
| Searched, items left inside | 3 | 58,809 | Present |
| Music Box picked up, other two left | 2 | 14,593 | Present at minimum 0 |
| Remaining silver coin/telescope picked up | 0, empty flag set | Unknown / zero | Removed |

Music Box catalog ID `15080050137` displayed **58,809**, exactly matching both
native and overlay values. The user left to fight and returned before the
pickup sample; the safe's updated contents were still readable. The emptied
safe remained in the actor feed with known-empty contents and no projected
marker, rejected by the value filter. At the intended 20k cutoff, the two-item
14,593 phase would be excluded; that conclusion follows the verified threshold
policy rather than a separate live toggle in this sequence.

The new edge diamonds were visible on the captured composed screen for other
loot with offscreen origins. This regular safe used an on-screen marker while
searched; it does not validate every special safe or the newly added kind-3/5
linked-content path. A second unopened safe was the same class, so the user was
released to loot it normally rather than repeat the same diagnostic sequence.
The next requested sample is a bird nest, sewer or filing cabinet.

Private evidence: `evidence/local/session/container-links-live-20261103/`,
including matched snapshots, screenshots and `regular-safe-summary.json`.
A root `/proc/*/exe` check found exactly one project scanner, the pinned candidate;
old `.active` filenames alone do not establish live scanner processes. No
runtime/source change was needed for this live verification.


## Flight case: searched contents and item-price comparison passed

The user stood beside a closed flight case. The matched runtime class was
`BP_InteractorContainer_AirCargoContainer_C`, metadata kind 2. Before search,
`bClientSearchDone=0`, readable item count was zero and value was unknown.
After searching and leaving the items inside, the same actor exposed four
items: armor `11050003001`, collectibles `15080050020`, `15040010021` and
`15080050022`. The marker showed **14,524**, the highest supported collectible
unit value, and was present/on-screen in the captured overlay output.

The Firearm Parts details panel displayed **14,524**, exactly matching the
native/marker value for catalog ID `15040010021`. Armor was detected but excluded
from pricing under the current condition-adjustment limitation. Thus this case
is visible at the diagnostic minimum 0, but its supported collectible values
would not qualify at the intended 20k threshold. This was not a live threshold
toggle test and does not claim the armor itself is below 20k.

The user was released to loot normally; the full pickup/empty sequence was not
repeated for this flight case. Both tested safe and flight-case contents became
readable only after search. These observations do not establish server spawn
time or pre-search valuable-container detection. The remaining priority test is
a bird nest, sewer or filing cabinet to exercise the new linked-content family.
Private proof is `container-links-live-20261103/flight-summary.json` with matched
snapshots and the captured UI value. No runtime changes were needed.


## Bird nest: searched contents and Medical Alcohol price matched

The user-selected bird nest was `BP_Interact_Nest_C`, metadata kind 2.
Before search its readable item count was zero, search was incomplete and the
value was unknown. Searching exposed three items, catalog IDs `15060010007`,
`15080050007` and `15090910023`. The native reader and separate diagnostic
agreed on a highest known individual value of **9,066** for `15060010007`.
The composed screen showed the on-screen container marker with that value.

The user opened the EtOH bottle's details: its full name was Medical Alcohol
and its displayed value was **9,066**, exactly matching the native/marker value.
The filter remained ON at diagnostic minimum 0. At the intended 20k minimum,
this nest would be excluded based on its known values; a live threshold toggle
was not repeated. The user was released to loot normally without another
pickup/empty sequence.

This establishes searched-content coverage for this bird-nest class, not
pre-search visibility, every nest variant or the new kind-3/5 linked readers.
A nearby unopened HalloweenRaven SingleItemContainer was a different actor
and was not the nest tested. The next optional sample is a sewer or filing
cabinet. Private proof is `container-links-live-20261103/nest-summary.json`
with before/after snapshots, overlay output and the item-details screenshot.
No runtime changes or app restarts were needed.


## Sewer / Hidden Stash: searched contents and Personal Note price matched

The user stood beside an unopened sewer lid, shown by the game as Hidden
Stash. Its class was `BP_Interact_WellLid_C`, metadata kind 2. The initial
snapshot had search incomplete, zero readable items and unknown value.
After search, the same actor exposed three items: `15020010014`,
`15020010019` and `15090910023`, shown as SetSqre, Pliers and S Letter.
Both the native feed and separate diagnostic reported a highest known
individual value of **3,269**, catalog ID `15090910023`.

The S Letter details panel named the item Personal Note and displayed
**3,269**, exactly matching the visible on-screen container marker. The filter
was ON at diagnostic minimum 0; at the intended 20k minimum this stash would
be excluded based on known values. No live threshold toggle or additional
pickup/empty sequence was repeated. The user was released to extract with
no further container tests requested this match.

This verifies the searched sewer/stash class, not pre-search valuable-loot
detection or server-side spawn timing. All four tested visual families
(regular safe, flight case, bird nest and sewer) used metadata kind 2.
Live kind-3/5 linked-content coverage, filing cabinets and special safe
variants remain outstanding. No runtime changes or app restarts were needed.
Private proof is `container-links-live-20261103/sewer-summary.json`, matched
before/after snapshots, overlay output and the Personal Note details screenshot.

The user subsequently reported successful extraction, completing this run
with the installed candidate and all four container checks. No crash was
reported. The scanner was stopped between matches, and a root `/proc/*/exe`
check found zero project scanner processes remaining. This is one completed
live run, not a diagnosis or resolution of historical SIGBUS exits. The user
was reminded that the retained diagnostic minimum is 0 and normal play should
use the intended 20k cutoff. Private outcome proof is `match-outcome.json`.


## Second match: user-selected special safe also passed after search

The user spawned on Zero Dam Easy with radar ON and minimum 0. The native feed
was fresh and its actual peer executable matched the pinned `135e3893...`
candidate. At spawn several unopened SingleItemContainer safe actors had empty
CachedPickups/CachedOpenBox fields; this does not establish that those actors
were the later user-selected safe or that no alternate client state exists.

The first requested safe capture instead showed a Computer/Ammo Crate. It was
explicitly rejected as an unconfirmed target. After the user aimed at the actual
safe prompt, a separate before sample matched `BP_Interactor_Container_SafeBox_C`
within about 1.1 m: metadata kind 2, search incomplete, zero readable contents,
unknown value. The class was the same as the previously verified regular safe,
despite the user's description of it as special.

Searching exposed one item, catalog ID `15080050001`, with native/diagnostic
value **13,186** and a visible on-screen marker. Its details panel named it
Empress Earring and displayed **13,186**, exactly matching the marker. At the
intended 20k minimum it would be excluded; no live threshold toggle was repeated.
The user was released to loot normally, and a filing cabinet was requested next.

This extends verified safe coverage in a fresh match but does not validate the
new kind-3/5 linked-content reader or pre-search valuable-loot detection. No
source/runtime changes, app restarts or pickup/empty repeat were needed.
Private proof is `container-links-live-20261103/special-safe-summary.json`.
Only `special-safe-confirmed-before.json` is the accepted safe before sample;
`special-safe-before.json` belongs to the earlier Computer/Ammo Crate location.


## Tool Cabinet passed; extra unopened-container fields inspected

The user found a Tool Cabinet while looking for a filing cabinet. The game
prompt and nearest actor matched `BP_InteractorContainer_ToolCabinetLarge_C`,
metadata kind 2. Before search it exposed zero items and unknown value.
Searching exposed one item, `15020010034`, with native and diagnostic value
**14,954**. The user already had the Basic Fuel details panel open; its value
was **14,954**, exactly matching the visible container marker. Minimum remained
0. At 20k this item would not qualify; no live threshold toggle or repeated
pickup/empty sequence was needed. Private proof: `tool-cabinet-summary.json`.
This is Tool Cabinet coverage, not filing-cabinet or kind-3/5 acceptance.

To investigate another possible pre-search source, the separate one-shot
`loot_probe.c` diagnostic now reports reflected special-container state flags
and `DropItems_ForReplace`. It validates a fully readable array header, count
and capacity limits (128/1024), element stride and field bounds, and records
per-entry read success. These replacement definitions are not established as
actual current contents and are not used by production pricing. The API-31
compile passed `-Wall -Wextra -Werror`; the final deployed diagnostic hash was
verified. Live output parsed as JSON, and twelve still-current unopened kind-3
actors exposed valid but empty replacement arrays alongside empty cached
pickup/open-box links. This does not prove that no alternative client state
exists or when the server generates loot. Nonempty replacement definitions
have not been validated live. Private proof: `replacement-drops-unopened-validated.json`.

The diagnostic remains a separate content-addressed, bounded read-only tool.
The production scanner/APK, deployment pins and live radar were unchanged.
An office filing cabinet remains the next requested target; the user can
continue playing until one is encountered.


## Office/filing cabinet: child items track after opening and reclosing

The user found an unopened office cabinet, with the game's Open prompt visible.
The closed cabinet parent was not represented in the nearby emitted loot feed;
the nearest emitted loot was about 4.9 m away. Thus its parent class and cached
link fields were not established by this capture. The two later child actor IDs
were absent from the entire captured before feed, not just the nearest sample.
This is an observation about the emitted client feed, not proof of server-side
creation time or absence from every other client data structure.

Opening the doors exposed two shelf items, independently emitted as
`BP_InventoryPickup_FromSingleItemContainer_C`, metadata kind 1:

| Item shown | Catalog ID | Native/marker unit value | UI price comparison |
| --- | --- | ---: | --- |
| Solar Panel | `15030050016` | 25,749 | Exact match, 25,749 |
| Waybill | `15090910022` | 4,451 | Not repeated |

Both items had composed on-screen markers. After the user left both untouched
and closed the cabinet doors, the same child actors remained in the feed at
the same prices and both markers remained on-screen over the closed cabinet.
Minimum remained 0. At the intended 20k cutoff, the Solar Panel qualifies and
the Waybill does not; no live toggle or pickup/empty sequence was repeated.

This establishes tracking for items exposed by this cabinet, including after
reclosing it. It does not validate the parent's kind-3/5 linked-content reader
or visibility before the first opening. No production/source changes were
needed. Private proof: `container-links-live-20261103/filing-cabinet-summary.json`
with before/opened/reclosed frames, composed screenshots and the Solar Panel
details panel. The user was released to extract; scanner remains running until
the match ends. No further tests were requested this match.

The user then confirmed they were out of the match. Radar was stopped between
matches; a root process-executable inventory found zero project scanners
remaining. No crash was reported. The user's message confirms the match ended,
without a separate extraction-result screenshot. Private outcome proof is
`second-match-outcome.json`. The diagnostic minimum remains 0 until restored
for normal play; intended minimum is 20k. Production APK/native are unchanged.


## Preparation for actual contents before the first open/search

The user explicitly requested pre-opening loot visibility after the two live
runs. All accepted price samples so far were after opening/search. A new
read-only diagnostic now investigates two independent questions: whether the
container is missing only from production discovery, and whether alternate
client inventory state contains its actual items before opening.

Static audit found a first-2,000-actors-per-level ceiling in production discovery
and a 0.5 m position-based duplicate filter that can suppress distinct nearby
loot. Neither has been changed in this preparation, and neither establishes
that pre-opening item data exists. The SDK's LootBoxRepData.SpaceRepData describes
grid sizes/update counters, not a separate item list. AInventoryPickup has a
ContainerSpace reference, but most native ItemSpace internals are opaque.

`loot_probe.c nearby-loot X Y Z RADIUS CLASS_FRAGMENT [SELF_ACTOR]` performs a
separate registry pass capped at 300,000 objects. It uses class-verified loot
inheritance, excludes class default objects, and retains at most 32 nearby
matches plus 16 matching actors with unresolved transforms. With SELF_ACTOR,
its owning World must resolve and only actors owned by that World qualify.
The existing eight-second deadline applies. Per-actor output now includes a
ContainerSpace snapshot only when the referenced object's class matches the
reflected ItemSpace class and its full size is readable and at most 1,024 bytes.
No arbitrary pointers in that opaque object are followed; no contents/value
claims are made from those bytes.

`capture_loot_snapshot.py --registry-filter Cabinet --radius 15` invokes that
mode from a fresh pinned frame, using its current self actor as the world anchor.
It retains private evidence before rejecting an incomplete, truncated or
unverified-world pass. The helper now uses `adb shell -T` for the diagnostic
so the remote process's exit code is preserved; `exec-out` did not preserve
the negative-test exit codes on this device. Arguments use `shlex.join`.

Validation: API-31 compile with `-Wall -Wextra -Werror`, Python compile,
hash-verified separate diagnostic deployment (`9d384db4...`), full lobby
registry traversal in about one second, rejection of invalid coordinates and
invalid anchors, and Python rejection of nonfinite radius, invalid class
filter and incompatible watch/registry arguments. The lobby pass had no
matching cabinet actors; it does not validate positive inventory/world-anchor
behavior or establish match contents. Those checks need a fresh match.
Private proof: `unopened-container-20261103/diagnostic-validation.json`.

The user has been asked to spawn with radar ON/minimum 0 and take cover.
Capture the closed target through the new data paths before requesting its
first opening, then compare actual items. The installed working APK, native
scanner and deployment pins remain unchanged. No pre-opening display feature
is claimed ready, and empty data must remain unknown.


## Third match: closed flight case and the native item reader

Fresh pinned production feed and peer executable were verified. The first
independent cabinet registry pass returned no match despite a cabinet in the
production feed. This exposed a diagnostic ownership error: a streamed Level's
Outer can be its package World. The diagnostic now follows reflected
Level.OwningWorld, validates the World class, and uses that active World for
both the current self anchor and candidate containers. A subsequent full pass
matched the Tool Cabinet. Do not interpret the earlier zero-match pass as
absence of client data.

The user then stood beside a closed Flight Case with its interaction prompt
visible. Both the nearest-feed capture and independent current-world registry
pass matched `BP_InteractorContainer_AirCargoContainer_C`. The registry completed
without truncation and found the same actor. Its RepItemArray was valid with
count zero, search incomplete, and ContainerSpace null. The diagnostic also
records the reflected InventoryGID privately for cache correlation. The user
died before the requested closed-to-searched comparison; no matched after-search
sample exists for this run. No game crash was reported. Radar was stopped and
zero project scanner processes remained (`match-outcome.json`).

Fresh Lua metadata identified `Server.LootingServer` and its methods.
Read-only closure/prototype captures showed InitPickupBoxData_SceneBox using
LootBoxRepData.SpaceRepData and FetchItemsInPickupBox; the latter calls
GetSubPickups, then constructs item data. This is evidence from method constants
and native disassembly, not an executed game call. InventoryServer's shallow
maps were also captured, but no closed-container content mapping was established.

The diagnostic's new `pickup-methods` mode validates the native pickup and
container classes, their immediate parent indices, class default objects and
module-bounded vtables before reading the GetSubPickups virtual method slot
identified in the SDK wrapper. Offline module-relative reads/disassembly showed
the native container implementation copying RepItemArray.Items from the same
offsets and 0x800 stride already used by production. GetSubPickupSpace returns
the ContainerSpace field. The relevant pre/post GetSubPickups and GetSubPickupSpace
hotfix flags were zero when checked after the user's death, so that check did
not expose an alternate patched reader. It does not establish that every
Blueprint override or every other cache has been ruled out.

This supports pre-search item availability as the remaining blocker; it does
not prove server-side spawn timing or universal absence of an alternative client
source. No pre-opening display feature is implemented. The separate diagnostic
API-31 build passed warnings as errors and its deployed hash (`9afa728a...`)
matches. Production APK/native and pins remain unchanged. Private evidence:
`unopened-container-20261103/`, including closed feed/registry frames, method
captures, native disassembly and dispatch flags. Positive nonnull ItemSpace
capture remains unvalidated. Identify a concrete new data path before requesting
another match; repeating already-passed price panels will not resolve this gap.


## One-command Nearby-cache capture prepared; user released to queue

Further read-only LootingServer method inspection showed SyncItemsBeforeFetch
working on already supplied item records and slots. Its dirty-container handler
also obtains GetSubPickups before incrementally updating Nearby slots. The
separate `InventoryServer._mapGid2Items` is grouped by ESlotGroup; Nearby is a
distinct group from Player and DeadBody. Previous shallow Player-map samples
do not establish that unopened contents are absent from the Nearby group.

Added `scanner/tools/capture_loot_cache.py` to capture that remaining cache path
efficiently before/after one interaction. It verifies the content-addressed
diagnostic hash, requires a valid fresh pinned radar frame for live captures,
rediscovers fresh Lua roots and the Nearby enum, and records bounded snapshots
of LootingServer's _cachedData, _slotItemMap, _nearbyPickupMapping and _fetchScope,
plus InventoryServer's Nearby item/slot maps. Child/table/sample limits and
partial flags are retained; no Lua functions are invoked and no items moved.
All raw cache values remain under ignored evidence/local. A --lobby option
marks its output explicitly as preparation, never a live-match result.

The lobby check completed in 4.486 s / 25 bounded diagnostic calls and read
all named cache roots. Nearby's item map was empty in that lobby check; the
slot map existed. _cachedData contained an InvMgr userdata reference, which
was not dereferenced or interpreted as target loot. Validation also confirmed
stale/stopped-frame rejection before any Lua reads, duplicate-label refusal
without changing the earlier sample, invalid-label rejection and Python compile.
No APK/native changes were needed.

The user has been told they can queue now and asked to confirm spawning.
The specific next experiment is a native/cache snapshot while a target stays
closed, a second native check that search has not occurred during capture,
then matched native/cache samples after opening. No price details panels are
needed. This tests an additional data path; pre-opening item visibility remains
unproven. Private preparation evidence: `lobby-cache-startup.json` and
`cache-collector-validation.json` under `unopened-container-20261103/`.


## Fourth match: matched unopened safe vs Nearby-cache comparison

The user spawned with radar ON/minimum 0 beside a safe. The interaction prompt
and native sample matched `BP_Interactor_Container_SafeBox_C`, metadata kind 2.
The cache capture was bracketed by native snapshots of that same actor: both
showed search incomplete, zero readable items and null ContainerSpace. The
live cache capture was complete and the Nearby map's single item was nearby
ammunition (`37250200002`, count 16), not a safe item.

After the user searched and left everything inside, the same safe's native
array and complete Nearby item-map sample both contained three catalog IDs:
`15080050011`, `15080050015`, `15080050012` (telescope, wine cup and figurine
shown in the composed loot window). None of those IDs was in the before Nearby
map. Neither before nor after Nearby item-map capture hit its entry/child limits.
The marker's highest known individual value was 62,800. No item-details price
panel was requested, so this is not another exact UI price comparison.
ContainerSpace remained null even after search.

LootingServer._slotItemMap had 74 root keys before and 77 afterwards, all below
the 1,024-entry cap. Its keys use a compound slot format; an attempted direct
GID lookup did not correlate them to the safe items. Its child payloads were
sampled, so do not claim a full content/GID comparison for that map. The decisive
comparison here is the complete InventoryServer Nearby item-map sample against
the native safe array. Private summary: `safe-cache-comparison-summary.json`.
Cache captures took 8.484 s before and 8.209 s after, with fresh pinned frames.

The user was released to take the loot and keep playing. No further safe
interaction steps were requested. Radar remains running until this fourth
match ends. No production APK/native change was made. These observations show
the additional checked cache does not expose this safe's items before search;
they do not rule out every client source or establish server-side spawn timing.
Pre-opening item display remains unimplemented.


## Separate native InventoryDataCenter cache traced during play

Read-only module disassembly of the SDK item allocation/GID lookup methods
identified an opaque native InventoryDataCenter TMap at +0x48, mapping a GID to
a shared FInventoryItemInfo in 32-byte sparse entries. The diagnostic's new
`inventory-cache` mode validates the base native class/parent and the final
SOL/PropHunt subclasses, excludes default objects, and caps the registry at
300,000 objects / 16 centers. Array/bitmap bounds, allocation popcount, stored
item GID agreement and a stable header are recorded; output caps at 4,096 items.
It reads only known item fields, never changes reference counts or invokes
game functions. The existing eight-second alarm remains.

An initial base-class-only pass found no instances; adding the SDK's verified
final subclasses found one center. A later read during play reported 11 active
entries, 10 valid type-18 catalog items and one unreadable/invalid entry, with
allocation counts matching and a stable header. None of the valid GIDs matched
the three searched safe items. This does not establish it as a container-content
cache, and the single unreadable entry prevents a claim of complete item
enumeration. No matched before/after experiment was performed on this native
cache. The result supplies no demonstrated source for unopened valuables.

API-31 compile with warnings as errors and content-addressed deployment hash
verification passed; current diagnostic hash is `ea4a2030...`. Raw module
fragments, disassembly, GIDs and addresses remain private under ignored
`unopened-container-20261103/`. Working APK/native and deployment pins are
unchanged. Do not promise pre-search loot display from this diagnostic path.

The user subsequently reported ending the fourth match (no extraction/death
detail specified). Radar was stopped between matches and the process-executable
inventory found zero project scanners remaining. No crash was reported.
Private outcome record: `unopened-container-20261103/fourth-match-outcome.json`.
