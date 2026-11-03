# Loot value investigation — 2026-11-03

Development branch: `codex/loot-value-investigation`. Preserve remote
`OnePlus-all-working-lines` and tag `working-v1-2026-11-03` as the working baseline.
The installed APK, pinned production scanner and production sources were not
changed for this investigation. The user enabled the existing radar and played.

## Intended feature

The user wants a loot toggle/filter for **each individual item worth strictly
more than 50,000**, rather than the combined contents of a container. Use unit
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

1. Resolve and verify the value actually displayed in-game for several loose
   items and searched contents, including values on both sides of 50k.
2. Repeat the before/search/pickup/empty sequence for other container types;
   include proximity observations with fresh actors from the current match.
3. Add item ID, unit value and explicit value-availability/source metadata to
   the scanner feed, then implement the minimum-value setting in the APK.
4. Unknown/unsearched containers must not qualify as valuable without identified
   contents. Preserve a separate optional general-container display.
5. Preserve the baseline and existing coordinate/render scheduling paths; recheck
   camera responsiveness and full-match stability after actual integration.
