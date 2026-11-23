# Chams rendering follow-up — 2026-11-23

Changes remain uncommitted alongside earlier work. Do not reset unrelated changes.

## Findings and implementation

- Original order was original → green → red. Red fragments from hidden surfaces
  could overwrite green. First changed to red → original → green. User confirmed
  the red-over-green artifact was gone, but nearby boxes still hid covered bodies.
- Drawing the tint immediately with each original mesh let later scenery overwrite
  it. `chams_replay.h` captures graphics state in an ordered per-command-buffer
  journal and replays selected draws before the end of their original subpass.
  All red draws precede all green draws. Neither clone writes depth. Bindings,
  descriptor dynamic offsets, push constants, viewport/scissor and core dynamic
  state are copied. State is restored after replay. Recording resets on begin;
  explicit free releases buffers. Bounded overflow/secondary contents suppress
  replay. This targets core Vulkan 1.0 inline render passes, not RP2/dynamic rendering.
- Clone rasterization disables depth bias; depth bounds and stencil are disabled.
  Reverse-Z visible compare is GREATER_OR_EQUAL, occluded is LESS. Only attachment0
  receives color. Small indexed mesh sections are included; bone-format and
  attribute filtering and the existing near-depth first-person discard remain.
- Full intensity1.0 produced white/pink in the initial live test; reverted to0.5
  for replay validation. Latest build uses white fragment shaders multiplied by
  dynamic blend constants and adds a persisted10–100% brightness slider (default70).
  Palette/brightness now apply during drawing without rebuilding pipeline/shader
  objects. Host blend constants are restored after replay.

## Crash introduced and corrected

First replay build crashed at startup twice. Tombstones04/05 from17:32 are saved
in this folder. RenderThread jumped into NX anonymous/BSS memory. Driver binary
pulled read-only from `/vendor/lib64/hw/vulkan.msm8998.so`:
SHA256 `904ce44cd954fa20847d989317ff2163867ab7bc391360a82c468dd7b91ffc6e`.
REA pull Evidence: `ev_06490461051385c003e0f806841072b48abaf741a3d466f3119bf3fefb32579a`.

`vkCmdEndRenderPass` driver RVA0x9fb48 contains:
STP; MOV; BL0xfb228 at0x9fb50; LDP; B0xb0018.
The existing `_ih_patch` copied16 bytes without relocating that BL. The crash LR
was trampoline+0x1cc, matching that relocated slot's BL return site. Added
`chams_reloc.h`: preserve direct branch destinations with absolute veneers; reject
unsupported PC-relative forms/internal branches before mutation. Journal install
preflights every target. No anti-cheat/loader changes were made.

The crashing build was immediately rolled back, then the corrected replay build
was deliberately installed. PID3421 successfully loaded the range and replayed
1607 draws over194 flushes with0 drops in the captured sample. No newer tombstone
was observed at that check. `replay-fixed-range.png` shows red covered legs/bodies
through the nearby boxes and green exposed surfaces. Colors were still dark0.5.

## Validation and current deployment

- `test_chams_reloc.py`: actual crashing prologue, positive/negative branch targets,
  resume address, ordinary prologue and rejection of unsupported/internal branches.
- `test_chams_replay.py`: actual journal, red-before-green ordering, copied state,
  subpass isolation, independent buffers, recording reuse, overflow, secondary
  exclusion, live palette/intensity changes and clamping.
- Android overlay DEX build, NDK build and diff whitespace checks pass.

Latest installed library SHA256:
`eccaf12a7591072844384895f921f13e69f93126355c6002bf243b15a19073c2`
244920 bytes, local/device hashes match. This latest live-tint/default70% build
reached the range on PID4357. User confirmed: “looks good now”. Final capture
`final-range.png` shows brighter green exposed surfaces, solid red occluded bodies
through boxes, and no white/pink output in this view. `final-diagnostics.txt` records
replay activity and0 drops. No source/build changes followed that installed build;
only test assertions and documentation were updated. Host tests cover live tint
changes and restoration of host blend constants; manual slider/palette gestures
were not independently exercised during this final check. Do not claim all lighting,
map, culling, session-transition or long-session behavior is solved. Geometry not
submitted by the game (occlusion/frustum culling) cannot be recreated by replay.

Existing unrelated unresolved items include lobby model selection, exact actor
identity filtering, and near-plane-based FP filtering that can clip close enemies.
Journal CB slots are bounded128; command pools destroyed without explicit buffer
free can leave registry entries occupied until process exit. Unsupported capture
paths fail closed. Color output still passes through game post-processing.
