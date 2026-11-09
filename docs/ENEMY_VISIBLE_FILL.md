# Visible enemy fill — offline investigation, 2026-11-04

## Requested behavior and result

The user requested red fill on the actual exposed enemy surfaces, alongside
the existing boxes/skeleton. An exposed arm should be red while covered parts
stay unfilled; a fully exposed enemy should be entirely red. On 2026-11-04 the
user authorized offline preparation and offered match assistance tomorrow,
2026-11-05. No device access, deployment, game-memory read, or game execution
was performed for this investigation.

**Result: there is no verified implementation yet.** The most relevant SDK lead
is the skinned mesh's effect/second material pass. A depth-tested red pass could
potentially use the game's own geometry and occlusion instead of exporting a
pixel mask to Android. This is an architectural inference from declarations,
not proof that this mobile build supports that behavior. It also crosses the
current read-only scanner boundary: reading material pointers does not enable
a material pass.

## Why the current overlay cannot infer the requested fill

- [The scanner entity](../scanner/memscan.c) exports position, health and
  world-space bones. It exports no skinned triangles, scene depth or pixel mask.
- [The skeleton reader](../scanner/skeleton-reader.h) selects 17 body joints;
  these are joint centers, not mesh vertices or the character's silhouette.
- [Projection](../android/app/src/main/java/com/deltaradar/app/OverlayService.kt)
  checks the camera-facing hemisphere and viewport. `onScreen` is a projection
  result, not evidence that an entity is unobstructed by a wall.
- [Drawing](../android/app/src/main/java/com/deltaradar/app/EnemyLinesView.kt)
  uses an Android Canvas in a separate overlay. This code receives no game depth
  attachment or material pass.

The same camera and joint coordinates can describe a character in open view or
behind a wall. The current feed cannot distinguish those scenes. Thickening
bones, filling a projected hull, or gating a whole silhouette with one visible
flag would not meet the partial-cover requirement.

## Exact local leads

The generated [SDK index](ENEMY_FILL_SDK_INDEX.json) contains 21 scoped
declarations with source lines, field/bit metadata, native entry offsets and
source SHA-256 hashes. Rebuild or verify it entirely offline:

```sh
python3 scanner/tools/audit_enemy_fill_sdk.py
python3 scanner/tools/audit_enemy_fill_sdk.py --check
```

| Lead | What the bundled declarations show | What remains unproved |
| --- | --- | --- |
| `Engine.SkinnedMeshComponent` | `EfffectPassMaterial`, effect/second-pass flags, `bEffectPassNeedDepth`, `SecondPassMaterials`, setters | Actual draw-path semantics, depth testing, usable red shader, mobile support and runtime ownership |
| `GPGameplay.GPOutLineEffectComponent` | `OutLineRed`, active outline meshes, `PlayOutLineEffect`, `EnableOutlineHDStencilMaskIgnoreDepth` | Whether a material is an edge effect or full fill; whether it shows covered geometry; whether it exists on the target enemy |
| `DFMGameplay.DFMCharacter` | An outline-component reference | Availability, lifetime and layout in the running process |
| `Engine.PrimitiveComponent` | Custom-depth enable, stencil value and write mask | Whether a pass is active; these fields do not contain a pixel mask or automatically select red |
| `Engine.Material` | `bDisableDepthTest` | Effective shader behavior; the flag alone does not create or recompile a suitable material |
| `MetaDorian.MTAPI_UPrimitiveComponent` | Last-render and on-screen-render time getters returning one float | Actual backing fields and timing/occlusion semantics; neither return type describes exposed pixels |
| `DFMGameplay.DFMPlayerController` | Line-of-sight queries returning a float plus one point, or a bool | Return-value meaning and execution context; neither signature supplies a silhouette |
| `Engine.Actor` | `WasRecentlyRendered` returning a bool | Render timing semantics; cannot describe which part is exposed |

The generated SDK function files are **ProcessEvent wrappers**, not recovered
native implementations. For example, the render-time getters call
`GetDefaultObj()->ProcessEvent`; the line-of-sight wrapper calls
`UObject::ProcessEvent`. A scanner in another process cannot call these by
adding the dumped native offset to its own address space. Dumped offsets are
research references, not deployment-ready addresses.

No verified per-pixel or per-bone visibility cache was identified in the
inspected declarations. This is not proof that none exists in unreflected
native renderer state. The existing strings and earlier unrelated code slices
do not establish a callable implementation or GPU readback path for these leads.

## Implementation choices after source validation

1. **Game-rendered material pass:** the strongest local lead for accurate
   silhouettes and wall clipping. Requires proof of a suitable material,
   depth behavior, supported execution path and cleanup when meshes change.
   Outline names and the `NeedDepth` flag are insufficient proof. No remote
   writes, function calls, hooks or material modifications have been added.
2. **External overlay mask:** requires a verified enemy silhouette with scene
   occlusion, or validated mesh and scene-depth inputs from the same view/frame.
   Neither source is implemented. Entity depth alone cannot identify wall pixels.
3. **Image segmentation:** would require a separate screen-capture/model
   pipeline and reliable enemy/teammate association. This investigation provides
   no trained model or evidence of acceptable accuracy and latency.

No speculative Fill switch or unused protocol fields were added. Once a real
source exists, fill should be independently switchable alongside boxes/skeleton,
respect enemy/team filters, and disappear on stale data, world changes or Hide.
An unknown mask must produce no fill. A whole-body visibility guess must not be
presented as a verified partial-cover fill.

## Focus for the coordinated 2026-11-05 session

The first session is for **source feasibility**, not acceptance of a ready fill
build. Confirm the user is ready before starting any live diagnostic. Preserve
the accepted APK/scanner pair and use a short, bounded, separately invoked
read-only capture first; do not automatically start/restart the game.

First resolve what can be learned from the existing mesh/material chain and
the native implementations of the render-time getters. Only use addresses and
objects validated against that running build. Record whether the effect pass,
second-pass materials and outline component are actually present. A raw pointer
or a true render flag is not success. If no usable path can be established,
report that result instead of turning guessed skeleton regions red.

When a viable pixel-accurate prototype exists, compare these visual cases:

| Scene | Required result |
| --- | --- |
| Enemy fully exposed | Entire rendered body surface red |
| Only arm/head/leg exposed | Only that exposed surface red; cover untouched |
| Enemy fully behind solid cover | No fill |
| Enemy moving across a cover edge | Fill follows the current edge without trailing onto cover |
| Two enemies overlapping | Rear enemy does not fill over the nearer visible surface |
| Teammate beside enemy | Enemy fill respects the team filter |
| Camera turn, Hide, disconnect, world change | No displaced or lingering fill |

Foliage, glass, smoke, gear and weapon coverage need explicit observations;
collision queries and render depth need not agree for those cases. Record the
observed behavior before choosing their fill policy.

## Offline verification

The index generator successfully extracted all 21 expected declarations with
unique owner/member pairs and verified the committed output against the local
SDK. App/scanner runtime sources and deployment pins were not changed. Android
and native runtime suites were not rerun because this work changes only the
offline indexing tool and research notes; no on-device behavior is claimed.
