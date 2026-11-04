# Animated skeleton rendering — November 4, 2026

The installed reader and APK now send and render animated world-space bone
positions with each mesh's real parent hierarchy. Skeleton mode no longer draws
the fixed screen-space stick figure. Player, teammate and bot poses use the same
verified path. Existing position and camera checks remain in force.

The old path never resolved a dependable mesh on the local pawn, guessed sixteen
humanoid indices, interpreted scale at the wrong packed offset, and added raw
actor coordinates without rotation or scale. The overlay also intentionally
ignored raw player bones and substituted a schematic body.

## Runtime layout and transforms

Bounded external `process_vm_readv` captures and offline LLVM disassembly of the
native getters establish this build's layout:

- `ACharacter::Mesh` is at `+0x3d0`, mesh asset at `+0x718`.
- The active component-space pose is selected by `int32(mesh+0x794)` from the
  two arrays at `+0x750/+0x760`. It is not the local/reference-pose array.
- The native bone-name/count getters use the asset's final reference hierarchy
  at `+0x1c8`, with twelve-byte records containing a parent index at `+8`.
- Packed transforms are 48 bytes: quaternion at zero, translation at sixteen,
  scale at twenty-eight. The tested rigs contain eighty bones.

The hierarchy is read directly; name-pool access and guessed anatomical indices
are unnecessary. The renderer connects actual parent/child indices, including
fingers and the rig's helper/attachment bones. These are projected 3D joint
positions, not a rendered skin mesh.

Mesh origin is computed from the verified capsule position and the mesh's clear
relative attachment translation, including parent rotation and scale. Each
component-space joint then receives the mesh's world rotation and scale. Parent
translations are not accumulated a second time because the pose is already in
component space.

Remote raw transform rotations can be zeroed. For managed transforms the reader
resolves the current actor-owned transform record and requires identical rotation
and scale bytes in all ten selectable slots. It uses those shared fields only;
it does not use or average the transform slots' unverified world translations.
The existing bounds decoder supplies position. Large-match initialization also
resolves the named bionic mapping from map metadata when the scanner's general
VMA inventory reaches its cap.

## Validation and rendering

The reader pins the native bone-count getter's instruction pattern. It checks
asset/pose counts, parent ordering, quaternion normalization, finite coordinates,
attachment identity and clear relative-location tags. Pose bytes and critical
state are read twice; changes retry once and then omit the pose. Asset hierarchy
caches include process, asset, array, class and name identities.

The wire format carries `bonesVerified`, `boneCount`, `bones` and `boneParents`.
Skeletons can contain up to 512 joints; packet reservations scale with the actual
bone count, and the existing truncation marker remains. The overlay accepts only
verified poses, projects every joint with perspective, skips rear-camera
segments, and lets Canvas clip offscreen segments. It interpolates matching
hierarchies between frames. Missing bones never become an invented body.

Validation on device `20f9aa6c`, game PID 27845:

- Ten live native samples returned eighty verified animated bones. Their
  root-relative joint positions changed across samples.
- Installed-app validation initially waited for startup, then all nine populated
  samples had a verified camera and eighty-bone poses for five human players
  (including the local player), alongside bot poses. Parent counts and ordering
  matched every verified packet.
- Android unit tests passed, including perspective/depth, offscreen projection,
  rear-camera rejection, malformed coordinates and invalid hierarchies.
- Native math and owned-memory integration tests cover rotation, nonuniform
  scale, attachment translation, active-buffer switching, parent/wire data and
  invalid-state rejection. The existing reader regression also passes with
  512-joint packets and bounded snapshot truncation.
- The rebuilt APK contains the matching reader and is installed. The same game
  process remained running during these tests.

This supports the captured game layout and ordinary primary meshes. Master-pose
meshes requiring a separate bone-index mapping, managed attachment offsets,
unknown layouts and inconsistent data are omitted. A full-match endurance test
and detailed visible-body alignment across every animation/rig remain outside
this bounded validation. No game hooks, protected proc-mem reads or game-memory
writes were introduced.
