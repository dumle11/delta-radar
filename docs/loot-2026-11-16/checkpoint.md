# Loot / camera / actor tracking checkpoint — 2026-11-16

Current work is uncommitted. Read this checkpoint before the older handoff sections.

## Confirmed user results

- ESP/aim cadence recovered after removing an accidental full-catalog text retry loop.
- FOV camera jumping was fixed using PlayerCameraManager.LockedFOV (+0x38c), with identity/ownership checks and checked scalar writes. The stale-pointer FOV worker responsible for the 11:54/11:55 tombstones was removed.
- User tentatively confirmed pitch/FOV overlay alignment and Head selection. Head now resolves semantic skeleton bone names, checks selected head first, and falls back only after a confirmed blocked trace.
- Latest unresolved report: reconnect process exit, and some player overlays missing. PlayerStart exclusion was installed immediately before that report; its live respawn result remains unconfirmed.

## Implemented, needs live confirmation

- PlayerStart actors no longer qualify via capsule-only discovery; real CHARACTER/Pawn ancestry required. Class recycling/world cache invalidation covered by regression.
- Replicated impending-death state preserves downed players until actual death/death box. No final user confirmation yet.
- Loot dedicated menu, rarity masks, actual game palette, distance and minimum reference value **per item**, container contents, numeric ItemID Category*10000+Sequence, localized text fallback. Catalog has 17,344 rows and six live rarity colors. Actual garbage-bin contents/names/color correctness still needs verification; earlier screenshot showed generic unknowns before these changes.
- Current SDK accessor 0x106136b0 is called by K2_GetComponentLocation (0x11326e5c). Decoder also retains older accessor 0x107bbb8c, but accepts either only with exact captured redirect AND provider/codec signatures. No unknown provider is executed.
- Internal character position acceptance now prefers independently verified plain world translation when bounds are protected; rejects undecodable coordinates instead of publishing finite ciphertext. Counters distinguish decoded/world fallback/rejected. External CLI raw-observation behavior remains available.
- Native query gating validates active viewport world, registered current world/controller/pawn/camera and ownership; tests destruction bit9 at actor+0x94, which matches the native getter. Rechecks world/possession/destruction before physics queries and suppresses work on invalid transition frames. This narrows a lifecycle race; it is NOT proof that the reconnect exit is fixed.

## Reconnect evidence and limits

PID24511 exited around19:07:24; restarted PID25202 loaded a match successfully and ran for many minutes. No fresh tombstone/fatal signal/Java exception/LMKD report was found for that exit. Latest tombstones remain the old FOV-worker faults at11:54/11:55. Do not claim an anti-cheat cause or confirmed crash fix without new evidence. Do not alter anti-cheat/stealth code.

## Architecture caveats

The current source uses scanner-thread direct aim/fire/physics calls. engine_fire.ef_hook is a no-op from earlier sessions; older handoff text claiming an active GameThread bridge is stale. No new hooks were introduced here.

render_projection.h remains a **disabled native-matrix probe**: runtime LocalPlayer method RVA109f1c20 was observed, but a deliberate reject guard prevents calling it off-thread. Rendering currently uses fresh coherent camera POV + LockedFOV and roll-aware fallback projection. Do not claim frame-synchronous/pixel-perfect native projection; verifying invocation/thread requirements remains outstanding.

Match metadata work can still cost ~34–140ms in loot-heavy scenes; range ~6–11ms. Native text cache now avoids redundant layout reads for cached native conversions. Further optimization is pending if still visible. Do not retry all missing catalog localizations per frame.

## Device / workflow

adb device20f9aa6c, packagecom.proxima.dfm, UIDu0_a556. Root shell session63911 avoids repeated root prompts. Diagnostics append to /data/data/com.proxima.dfm/cache/d, not logcat. Never run abandoned repeated su monitors; prior ones caused root toasts and were killed. User explicitly permits restarting any time. User signs in. Do not start a new match unsolicited.

Build: NDK_HOME=/opt/homebrew/share/android-ndk bash internal/native/build_lib.sh. Overlay Java changes require overlay/build_overlay.sh first. Deploy libue4patch.so to app private path with mode755, owneru0_a556 and SELinuxu:object_r:app_data_file:s0:c44,c258,c512,c768. Save exact installed hash and live results below.

## Checks

Host regressions cover loot metadata/filter/palette/sparse maps, camera FOV ownership/restore, semantic aim and blocked-only fallback, downed lifecycle, character discovery, manual/native-matrix math, internal position acceptance and transition query gating. NDK build succeeds. Some older broad fixtures have stale architecture extraction or resize expectations; see tests/README.md. Live pending results are explicitly separate from host tests.

## Latest installed build

SHA256 `b3b08d4cda487331456df6b4893c11da72424f55e1b6c45293e851c6c2448e79`, 215376 bytes, local/device match. Restarted PID26554. Screenshot `/private/tmp/reconnect-hardened.png` shows **login screen**, so no match verification of this build yet. Async user request asks for reconnect and missing-player reproduction. Scanner startup retries while login has no world; that alone is not a crash.

NDK build, test_position_acceptance.py, test_engine_frame.py, test_engine_loot.py, test_camera_fov.py, test_aim_target.py, test_entity_life.py, test_character_discovery.py, test_projection.py and diff whitespace check passed for the current sources.

For subsequent deployment, force-stop before replacing the library or copy to a new file in the app directory and atomically rename it; do not truncate a shared library while the old process maps it. This is deployment hygiene, not established as the reported reconnect cause.

## Teammates missing — follow-up

User reproduced missing nearby teammates on PID26554. At frame8400 only self and four world NPCs were admitted; position rejected=6137, decoded=0, fail step1. This proves the decoder is incompatible and protected actors are being rejected, rather than only a teammate draw toggle. It does not alone identify each rejected actor.

Added `engine_position.h`: internal fallback binds exact native Actor.K2_GetActorLocation (exec11904f88) and SceneComponent.K2_GetComponentLocation (exec11326e5c), verifies 12-byte return-only ABI/return offset/native flag/owner/exec signatures. Getter outputs must be finite, nonzero, within50cm of each other, with registered actor/root identity, unchanged root link and current viewport/possession before and after. No arbitrary encryption callback is invoked directly; no unknown provider signatures relaxed. Existing verified plain/software paths are preferred. Counters include native success and native stage.

Latest build installed with process stopped and atomic rename: SHA256 `86840141dbf71cdde767b9181ae00c49c96fb289e07b82e8d51a66e216e7beae`, 218240 bytes, local/device match. Restarted; awaiting user reconnect/near-teammate check. This path is not yet live-confirmed. test_engine_position.py exercises actual binder/acceptance with mock native dispatch: mismatched ABI/exec, stale world, receiver invalidation, root reassignment, nonfinite values and disagreeing getters all reject. It and test_position_acceptance.py pass; NDK build and diff whitespace pass.

### Reconnect process exit; invocation evidence inconclusive

Tapped the existing-match Reconnect prompt on build86840141. PID27486 exited ~19:36:38 and restarted28156. Last old-process counters were native=0/native_stage=1; this records no successful recovery, not the number of actual invocations. No fresh tombstone/crash-buffer fatal report. Last diagnostic sequence: old world null -> consecutive read failures -> `rescan f=6 z=966 gw=0` -> process exit. This is correlation, not proven cause.

Changed reconnect recovery: when the cached Engine and viewport remain registered, wait for viewport travel and let the normal per-frame viewport discovery select the new world. Do not call parse_maps (which resets module globals) or full heap scanning just because World/PersistentLevel is temporarily null. Cold recovery remains for an unavailable viewport. `test_engine_frame.py` covers registered viewport survival across invalid world and rejection of unavailable objects.

Newest installed SHA256 `8159b6bdad837301a7240a2365a762371dc8e39cacd847310b4c664b8c61897e`, 218784 bytes, local/device matching. Stopped before atomic deployment and restarted. Native position getters and reconnect behavior still await a live successful-match test; pending async teammate check remains relevant. Do not report teammate rendering fixed until native successes/user confirmation.

## User-requested position failure notice

User tentatively confirmed teammate boxes/lines now appear: “yes, i think so.” Requested a small internal notice like the external version for entity position/decryption failures. This confirmation is subjective; do not equate it with a captured native-success count.

Added current-frame `position_frame_failures`, reset at the start of every entity read and incremented only after plain-world, software-decoder and SDK-getter recovery all fail for a character. Internal renderer shows **“Position recovery failed: N entity/entities”** in small17px amber text with a dark background at lower left. It uses existing Java `dt` so no Java/dex change was necessary. Notice clears on the next successful frame, is suppressed outside a valid controlled gameplay camera, and can render even when all actor coordinates are unavailable (without projecting any boxes). Successful protected-coordinate recovery does not generate a warning; lifetime totals do not drive the notice. Character discovery failures/loot omissions are not counted by this notice. Detailed logs retain decode/native stages and cumulative counters.

NDK build, updated position-acceptance regression (rejection count versus successful recovery), and diff whitespace pass. Installed/restarted SHA256 **`e1ec0cb579bf71a6c6e7d65ef221a3787fad9d8ac48d5013ee48b501a6da231c`**, 219136 bytes, local/device match. Notice appearance under an actual induced live failure has not been verified; no fake failure was introduced into gameplay.

## Crash report / restart communication — latest steering

User reported possible crashes, then clarified: “i think it was crashing durring reconnects, not sure. its stable at the moment, ill let you know if it crashes again - if you are killing or restarting it, let me know in the chat so i know”. **Announce every intentional stop/restart before doing it.** Permission to restart from earlier remains, but communication is now explicit. Avoid further speculative behavior changes while the user observes stability.

Android events distinguish intentional force-stops from unexpected exits: unexpected process deaths27486 at19:36:38 and28968 at19:40:05; deliberate stops26554 at19:35:28,28156 at19:39:15,29648 at19:41:50. Latest process30372 (started19:41:51) remained open through isolation. No new tombstones beyond11:55. No proof of a specific cause.

Temporarily stopped the scanner using existing sentinel `/data/local/tmp/.s7xk`; it logged `scanner stopped` at monotonic35298.818. Removed the root-owned sentinel afterwards (app could read but not unlink it). The game was in spectator view and stayed open. This is not a controlled proof that scanning caused the earlier reconnect exits: the reconnect had already progressed before the scanner stopped. Restoring scanner requires an announced restart.

Correction to earlier inference: `native=0` counted successes, and `native_stage` was only the last attempt. Those did not prove zero native calls. Added distinct SDK invocation/return counters plus first-four entry/return breadcrumbs. test_engine_position.py covers balanced actual dispatch counts. New logging adds no additional engine calls and does not change position acceptance.

Announced restart completed to restore the stopped scanner and install call/return breadcrumbs. Latest local/device SHA256 `0a4010c58a903a876d2dfc50aeee2ab7dd849aea7749e9c60ae169ccff66e5e1`, 219568 bytes. Build, focused native-position test and whitespace checks pass. Stop sentinel removed. No crash cause established; user will report another unexpected exit. Leave game running and announce any future intentional restart in advance.
