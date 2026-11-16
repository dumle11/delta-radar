# Infinite stamina, 2026-11-16

Verified installed build: `2da59def7375cd1daa3b2537a6d490401ae2d0d8fc2d5541acf6b0d894358921`. Local/device SHA256 matched.
User confirmation: “Enabled: steady; disabled: drains”.

## Evidence and implementation

SDK profile: `dumps/1.202.37119.3601/20261112-113826/CppSDK/SDK/`.
`DFMGameplay_classes.hpp` defines DFMCharacter.GPStaminaDataComp at 0x3AD0.
`GPGameplay_classes.hpp` defines the component's GPOwner at 0x1D8 and
AttributeSetSpeed at 0x1E0. GPAttributeSetSpeed's StaminaSprintChangeRate is at
0x148 and StaminaCostRate at 0x1B8. `GPAttribute_structs.hpp` defines the scalar
BaseValue/CurrentValue at +8/+C within each 16-byte attribute.

The unfinished generic pointer scan was removed. The new handler checks the
exact classes, registered object identity, serials, ownership and all field
reads. Only four floats are overridden. It preserves actual stamina, maximum,
regeneration, attribute metadata and unrelated object data. Toggle-off and
lifecycle cleanup restore saved values only for matching live objects whose
field still equals the applied value. No network RPC or external process-memory
read is used.

`stamina-samples.log` contains internal diagnostics before/after enabling.
Disabled: stage=4 with rate pairs 1/1; stamina decreased while sprinting.
Enabled: stage=5/state=2 with rate pairs 0/0; stamina regenerated to 100 and
remained there. `live-validation.log` captures the corresponding session tail.
`verified-range.png` records the current range screen. The user independently
tested both toggle states and confirmed the expected bar behavior.

## Validation limits

Native build, synthetic production-handler regression and focused real Android
menu/preferences fixture passed. Existing broad rendering and weapon fixtures
have unrelated stale harness expectations/dependencies; see handoff.md. No aim,
trigger, zoom, camera or weapon override implementation changed in this task.
Coverage is the firing range and the user's running/sprinting check; it is not
an extended-match or all-game-mode verification.
