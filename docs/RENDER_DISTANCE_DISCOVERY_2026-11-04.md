# Player and teammate range discovery

The overlay already applies the same `render_distance` preference to enemy
players, teammates, bots, vehicles, and loot in `OverlayService.refreshEnemyLines`.
The slider maximum is 1000 m and the device preference was already 1000. There
is no separate 300–400 m player/teammate drawing cap.

The packaged legacy reader did have discovery gaps: only the first 2,000 actor
slots of each level were inspected, and player pawn changes could wait for the
periodic level-cache refresh. The reader now:

- Checks GameState's PlayerArray every feed update and follows mutually linked,
  active PlayerState/pawn pairs.
- Invalidates the actor cache when those pawns change and prioritizes them ahead
  of the level scan. Finished, spectator, inactive, and unpossessed players remain
  excluded; the local camera anchor remains available.
- Reads each complete validated level actor array, up to the existing 50,000-slot
  sanity bound, using the existing 2 MiB scratch area. The old 2,000-slot cutoff
  is removed.

## Evidence and validation

The native regression passed on device, including a new fixture containing:

- An enemy at 950 m and a teammate at 990 m, both absent from level actor arrays.
- An AI pawn at 1,250 m in actor slot 2,048. The reader publishes available actors
  independently of the UI range; the overlay applies the selected range.
- A roster-only pawn replacement detected on the next update without changing
  the level array, and immediate removal after its finished-game flag is set.

The Android debug build and all 43 JVM tests passed. The new APK was installed
and its bundled reader restarted. Source binary, APK asset, and device binary
SHA256 match:
`0b413c65986bfc2f97f94a43431edb0a2f3c6aa0f6ed395b4b6c83e9212de2ae`.

A live capture of 25 snapshots spanning 9.401 seconds observed 463 feed updates
(49.25/s), with verified health on all 125 combat-actor observations. Game PID
27845 remained running. `kptr_restrict` remained 2. No hooks or game-memory writes
were introduced.

The live roster initially contained one active remote human at approximately
302 m. Later that same unfinished PlayerState had a null pawn pointer, while
remaining in PlayerArray. Other remote humans were marked finished/dead. This
shows a live case where missing human markers are due to missing pawn data on
the client, rather than the overlay's selected distance. It does not establish
an exact server replication cutoff or prove late joining.

The 950/990 m cases were controlled native fixtures, not remote humans observed
at those distances in this match. Rendering at 1000 m still requires current,
verified position data on the device; this change does not increase server
replication range or resolve the separate unverified-position decoder case.
