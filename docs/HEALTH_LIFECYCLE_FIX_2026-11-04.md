# Health and extracted-pawn rendering

The packaged legacy reader now resolves current/max health through actor-owned
components and attribute sets. Its previous fallback required the component's
`HealthMAX` default to equal the runtime `MaxHealth` attribute. Live bots disproved
that assumption: the default was 100 while their actual health/max health was
95/95 or 350/350. Those actors consequently had `healthKnown=false` and no bar.

Health discovery now checks both UObject Outer backlinks (+0x10) against the
actor, validates a complete attribute snapshot, and treats the component default
as a shape check rather than the actual maximum. Cached offsets are revalidated
on every actor. Sibling classes can reuse a validated layout, failed discovery
can retry after 250 ms, and health pointers use the telemetry pointer predicate
instead of a historical heap-address cutoff. Missing health is never replaced
with invented 100/100 values.

For non-local characters with PlayerState, the reader requires an exact active
pawn backlink (+0x3f8) and rejects finished (+0x4c0), spectator, and inactive
states. A null backlink previously left an old level-array pawn eligible for
rendering. State-less AI remain eligible. The local camera anchor is preserved.
The Android feed now removes missing combat actors on the next snapshot; the
12-frame streaming grace applies only to loot.

## Validation

- Built the reader with Android NDK 28.2, `-O2 -Wall -Wextra -Werror`.
- Ran the expanded native regression on device. It covers 95/350 maximums,
  damage updates, zero/dead health, invalid values, foreign ownership, component
  replacement, recovery after an uninitialized class, and finished/unpossessed
  full-health pawns retaining their old position. All passed.
- Android debug build and all 43 JVM tests passed.
- Installed the APK and restarted its bundled reader. Packaged, source, and
  device reader SHA256:
  `05145e7c29eca53ef86af9974e458e06c9b148c8c0cede50db113347a5da236e`.
- The first populated installed snapshot had nine combat actors, all with known
  health, including a 350/350 bot, several 95/95 bots, and injured players.
- A subsequent 35-snapshot capture spanning 11.949 seconds contained 138 combat
  actor observations, zero unknown health values, and zero stale combat entries.
  A screenshot confirms visible health bars for the sampled on-screen bots.
- Game PID 27845 remained running. Reads used `process_vm_readv`; no game hooks
  or game-memory writes were introduced. `kptr_restrict` remained 2.

A live extraction was not observed during this verification; extraction removal
was exercised with the owned native lifecycle fixture, including the previously
allowed null-pawn case. This change does not alter position verification or
increase the game's replication distance.
