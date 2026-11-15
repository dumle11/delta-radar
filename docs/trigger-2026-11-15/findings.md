# Internal trigger bot, 2026-11-15

Verified build SHA256: `a6f0d566b1b939a264967c59428adefef91e806b46d853ac49571a1440ef1c73` (local/device match).
User confirmation: “Yes, it fires and stops correctly”.

## Shipped-artifact observations

SDK: `dumps/1.202.37119.3601/20261112-113826/CppSDK/SDK/`.

- GPGameplay_classes.hpp: GPCharacterBase.StartFire and StopFire take one int32,
  parameter size 4. Native exec RVAs are 0xC595A6C and 0xC595C18.
- CoreUObject_classes.hpp: UFunction.ExecFunction is +0xD8. Runtime binding checks
  it together with Native flags, parameter extent, function name and owner.
- Engine_structs.hpp: FHitResult.Actor is weak index/serial at +0x68/+0x6C.
  Runtime object-item weak serials are at +0x14 for this profile; object headers,
  destruction flags, registered pointer/index and serials are checked.
- MetaDorian_classes.hpp: GetGGameThreadId exec is 0x199193C0. Bounded native
  disassembly reads GGameThreadId at 0x2067C028. IsInGameThread at 0x1991A194
  compares that same value with the current platform thread ID; initialized flag
  is 0x2067C024. Production checks accessor instruction bytes before reading.
- Actor::ProcessEvent RVA 0x107E2070 and vtable slot 0x45 are reused from the
  earlier verified dispatcher profile, with live instruction/pointer checks.

REA Android analysis could not run because REA_JADX_MCP_JAR is unconfigured.
Local JADX and targeted NDK llvm-objdump were available for bounded static
inspection. Static inspection alone did not establish firing behavior.

## Runtime and regression evidence

`live-validation.log` records native crosshair hits, starts/stops, callbacks,
engine thread ID, and rejection bits. The captured tail reaches 32 starts and
32 stops, thread 7586 and reject=0. `verified-range.png` records the range screen.
Earlier observed captures showed consumed ammo and increased mannequin damage.
The user confirmed both automatic firing and release on death/aiming away.

Host `test_engine_trigger.py` tests center-ray geometry, nearest eligible enemy,
wall/teammate/self rejection, death, health/visibility unknowns, failed traces,
recycled hit identity, disabled Aim, sustained hold and release decisions.
`test_engine_fire.py` tests production method binding, zero function serials,
GameThread-only dispatch, recursion exclusion, balanced calls, target/weapon/
possession switches, expiry, death, disable, stale identities and scanner stop.
`test_engine_weapon.py` still passes; existing weapon override code is unchanged.
The Android OverlayRegression verifies the restored menu switch, independent
trigger setting, menu pause, and the existing rendering/settings behavior.

The implementation calls SDK StartFire(0)/StopFire(0) on the game thread. It uses
no touchscreen injection, fire-button coordinates, helper process, or app root
permission. A validated ProcessEvent class-table bridge schedules gameplay calls;
the tables retain process lifetime and original dispatch always runs first.

Coverage is a firing-range machine-gun check, not all weapons/modes or extended
live-player sessions. Existing historical aim/visibility test fixtures are stale.
