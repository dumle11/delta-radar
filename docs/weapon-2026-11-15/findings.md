# Weapon debugging, 2026-11-15

Current status: user reported partial improvement after additive cancellation,
with residual upward crosshair movement and spread. A newer gun-kick/cache
build is installed and awaiting physical confirmation. The zero-pose-factor
build still climbed.

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
