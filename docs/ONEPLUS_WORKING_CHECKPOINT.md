# OnePlus working checkpoint — 2026-11-03

Historical checkpoint. The accepted 2026-11-04 pair, new frozen source tag and
rollback bundle are documented in [CURRENT_STATUS.md](CURRENT_STATUS.md).

Saved at the user's request after the first live check of the offline audit build.
Remote branch: `OnePlus-all-working-lines`. Annotated tag:
`working-v1-2026-11-03`. The tested application/native source is commit
`8863a1adf4586a1fcd716c635c4e3f966b795f6c`; the checkpoint adds documentation only.
The tag remains the frozen historical baseline. On 2026-11-04 the user selected
`OnePlus-all-working-lines` as the active working branch and accepted the newer
installed APK; continue development there. This document describes the older
checkpoint, not the latest branch tip. See README and SCANNER_DEPLOYMENT.md for
the current installed pair.

## Exact build

| Item | Identity |
| --- | --- |
| Device | OnePlus 7T HD1903, Android 12 / API 31 |
| Game version | `1.202.37117.5601`, version code `2217` |
| APK SHA256 | `2662fdec679118292c2fe5ab2f0aac632e10f29f32d8a7ec7b521448ddbb27e6` |
| Native scanner SHA256 | `8b150f87497d11511769dfd535a2455c748e73f689305c316fc822af618bb620` |
| Native source-input SHA256 | `82cf74347c739228cfed65431bf12b01650d04a0cd44cc3aedd93161ab9fb37c` |

The native identity embeds pre-audit base commit `c0764e3` plus the source-input
hash. This is build provenance; the tested source checkpoint is `8863a1a` above.
Installing a rebuild can produce a different binary identity. Use the saved
artifacts when restoring these exact installed bytes.

## What passed

- Installed APK and deployed native scanner matched the saved hashes. The
  running native executable was independently verified against the same hash.
- Overlay permission and foreground feed service startup passed on the phone.
- Live snapshots contained players, AI and loot; screenshots showed red and blue
  overlay markers. One short sample measured 21.8 feed updates/second, 15 ms
  transport age and 40 ms draw-source age. These are sample measurements.
- The user reported: "I think it works," then reported dying and leaving the match.
  The radar was stopped between matches at the checkpoint.
- Offline validation: 35 JVM tests, native regressions, sanitizer checks, APK and
  device-test APK builds passed. Lint had zero errors and 163 warnings.

## Still needs verification

Follow-up user feedback on 2026-11-03: alignment is good, close/far tracking works,
and camera movement is responsive. Treat these as passing based on the user's
live observations. The user also thinks lifecycle/filtering behavior works;
individual edge cases have not all been independently checked.

1. A separate loot check: distinguish loose items from containers, confirm that
   picking up an item or emptying a container updates/removes the appropriate
   marker, and check loot lines during camera movement. Position markers alone
   do not establish that container contents or item prices are known.
2. Explicit teammate/AI/dead-character filtering, feed expiry/disconnect, and
   game-process restart checks where not already observed by the user.
3. Full-match and extraction stability under sustained load remains outstanding.
   The earlier SIGBUS game exit has no established cause and is not a proven fix
   in this checkpoint.

## Restore

Exact APK/native files, original build manifest, deployment properties,
`SHA256SUMS` and restore instructions are saved locally in:

`evidence/local/checkpoints/OnePlus-all-working-lines-2026-11-03/`

The matching `.tar.gz` archive and its SHA256 file are beside that directory.
Raw game captures, screenshots, player identities and account logs are excluded
from the bundle and remain ignored locally. The tag/branch preserve the source
remotely; the executable bundle is a local backup.
