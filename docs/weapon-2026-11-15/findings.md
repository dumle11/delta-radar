# Weapon debugging, 2026-11-15

Final status: user confirmed **“Both are now steady”** for camera and bullet
marks with build `ed554a5fcae807b2851d0c7876c5a23c2ad7ffbf74cd631e7cabc62d79b47be7`.
Local/device hashes match. This is physical firing-range confirmation, not a
quantitative guarantee across all weapons/scenarios. The investigation below
records intermediate failures; “pending” statements there are historical.

The intermediate validation.log and screenshot precede the final pose-factor
change. Both weapons bound successfully, but the user still reported climb.
The sparse base-camera logs returning to the same pitch did not establish
steady camera movement during firing.

The matching SDK exposes WeaponRecoilModifierConfig.RecoilByPose at +0xA28,
with horizontal/vertical factors for standing, crouching and prone. A focused
static disassembly of the locally supplied libUE4 confirmed that
WeaponFireRecoilDFM virtual slot +0x2E0 (+0xBBC6184) selects those fields at
+0xBBC65C8..+0xBBC6600 and carries them into per-shot factors. Both hip and
aim configs must be handled. Subsequent tracing showed these are type-2
additive factors: aggregation initializes their multiplier to 1 at +0xBA42490,
adds factors at +0xBA42714, and clamps negative totals at +0xBA42730. Zero
therefore preserves recoil; -1 cancels it before additional modifiers. The
latest build applies -1 and preserves/restores that exact applied value.
Internal diagnostics confirmed the firing component matches both recoil
back-pointers and the active first-person weapon, with no host rewrites.
This is static evidence; the final live check
must still establish whether it removes the reported climb.

The SDK confirms that GetFireRecoilInfo routes through
GPWeaponDataComponentFiring.FireRecoilMethod (+0x578). Static code at
+0xF1C0054 reads that field. FireShake.FireRecoil is a DFM-specific cache and
was null on the second equipped weapon. Its FDFiring cache could also be null;
the data-component weak list supplies the authoritative firing component.

Host regression checks actual production helpers against synthetic objects.
It verifies selected fields, rollback/restoration, preservation of runtime
containers/pointers, weak serial validation, source replacement and failed
writes. It does not simulate the game's recoil algorithm or measure spread.

Two unexpected process exits occurred during earlier checks. The available
crash logs did not establish a cause. External process-memory reads were
discontinued; the final build logs typed component identity internally.

Additional native findings:

- WeaponFuncComponentGunKick is an independent typed component. Its native
  constructor is +0x1144C8C0 (class size 0x360); final view transforms multiply
  by scalar fields +0x25C/+0x260 at +0xB9D02EC and +0xB9D06C8. The newest build
  zeros those two scalars and restores them when disabled/switched.
- WeaponFireSpreadDFM.GetSpreadAreaInfo does not read the authoring config
  directly. +0xBBD7658 interpolates cached hip/aim half-angles from +0x3D0 and
  +0x598. The HUD reads current angles at +0x760 (+0xF1BFB58). The newest build
  zeros those three verified pairs (six floats), preserving surrounding data.
- The additive-cancellation build logged zero native recoil targets on both
  guns while their shot counts increased. User feedback still reported slight
  movement, so that evidence did not establish complete no-recoil behavior.

The 24-field spread-cache version still scattered. Read-only completed shot
records showed roughly degree-scale variation in SpawnRotation/FireRotation.
The OptionalData camera/aim fields were all zero, so those fields were not
useful for attribution. Latest logging reads AController.ControlRotation
(+0x3D8) instead and samples spread before the scanner writes it.

Native spread initialization at +0xBBD5844 copies config SpreadMax (+0xC) to
runtime +0x2C, and SpreadMaxFinal (+0x14) to runtime +0x34. Runtime hip and aim
structures start at +0x3D0/+0x598. The latest build clears those eight cached
limit floats as well (32 spread fields). Native firing skips spread when
current +0x760/+0x764 are zero (+0xBBD81A0). Physical confirmation is pending.

A fixed-coordinate ADB burst reproduced angular variation, ruling out a
manual fire-button drag as the sole cause. Native update +0xBBD489C compares
SpreadMaxFinal to zero and substitutes a large fallback for nonpositive
values; clearing it disabled the final clamp. The newest build uses a positive
1e-6 cap in both authoring and cached final limits. This is subpixel, while
keeping native limiting enabled. Recoil's additive pose cancellation now uses
-1000 so attachment terms cannot leave the residue that -1 allowed; native
negative-channel clamping supplies the zero multiplier. Live verification is
still pending. Shot logs are read-only and capped at 256 records per weapon.

Even the positive-cap build still showed controller rotation changing during
a stationary burst. Separate WeaponFuncComponentGunSway native paths were
traced: constructor +0x119F0DBC (size 0x1690); scale cache +0x1CC returned at
+0xB9DCE58; propagation at +0xB9D8630 uses +0x260; final operators set scales
at component +0xF80 (C70+2B0+60) and +0x1350 (1270+E0), through setters
+0xBA08D0C/+0xBA03150. A separate reversible group now overrides those four
scalar fields. Host operator dirty flags and internal containers remain intact.
This latest build still needs a controlled burst; do not claim success.

The SDK also exposes the camera's dedicated effect switches. The latest build
walks only the local PlayerCameraManager.ModifierList (+0x4260), validates
CameraOwner (+0x30), and overrides bEnable on GPCameraModifier_WeaponRecoil
(+0x44), GPCameraModifier_WeaponGunSway (+0x44) and GPCameraModifier_Fire
(+0x48). Each instance is snapshotted and restored separately. Other camera
modifier types are untouched. Synthetic regression covers all three switches
and restoration; latest physical burst is pending.

With the three camera modifier switches overridden, the recorded controller
rotation became identical across a stationary burst, but shot angles still
varied. The latest change disables the spread component's native enable gate
at +0x1E0. Native getter +0xBBE4C94 returns null when this flag is false, and
shot callbacks +0xBBE4DF0/+0xBBE4FAC/+0xBBE515C skip the strategy. This avoids
within-frame spread regeneration that periodic cache writes cannot prevent.
The flag is tracked in its own identity-checked reversible group. Regression
covers disable/restore. Latest shot-angle verification remains pending.

The latest NoSpread group also covers the reflected distribution/modifier
settings: MaxRotateAngle and CenterSlideValue (+4/+8) and ModifyFunc.RandomRadius
(+0x2C/+0x30) in both modifier configs (+0x350/+0x390). Sampling counts, ratios
and CustomDistributionPos arrays are preserved. Total: 40 strategy fields plus
one component gate. Latest burst verification is pending.
