# Loot value investigation — 2026-11-03

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
