# Controlled loader comparison

The user requested a comparison of Zygisk loading and KittyInjector after
repeated exits and incorrect visibility colors. A missing/frozen overlay or
process exit alone does not establish detection.

## Controls prepared

`internal/native/tests/build_loader_comparison.sh` builds two separate artifacts:

- `libloader_probe.so`: constructor/JNI log markers only. No scanner, UI, game
  reads, identity changes, hooks, or signal handlers. Normal ART loading was
  smoke-tested in a separate Android `app_process`; it passed.
- `liboverlay_plain.so`: current reader/menu/overlay. Since November 15 the
  default production initialization also skips legacy build spoofing, `_apply_cfg` and
  `patch_report_functions`. The compiled symbol inventory contains none of
  these initialization routines. It retains the reader's game-memory reads
  and ordinary Android overlay windows, so it is not a load-only control.

The existing `noop_lib.c` is not an inert control: it installs process-wide
SIGSEGV/SIGBUS/SIGABRT handlers. Do not use it for this comparison.

Artifacts, the verified original device library, initial configuration, hashes,
and selected game events are private under `/tmp/delta-loader-comparison/`.
The original library hash is
`ef9254305a44a89eeeff8043ec63be83342ea89c10f4c673ec4af4d128b9f139`.
Original state: Zygisk=1, custom module enabled, DenyList enforcement=1, SELinux
Enforcing. UID/GID 10556:10556, mode 0755, app-data context categories
`c44,c258,c512,c768`. Initial boot ID is in the private state file.

## Procedure

1. Preserve current evidence and configuration. Keep the same installed client,
   account, assets, device/root environment and ten-minute range observation
   window. The existing private payload file remains present in every stage.
2. Set Zygisk=0 and reboot. Verify the new boot ID, boot completion, unlocked
   user, saved setting, and absence of libzygisk in both zygote maps. This is a
   no-Zygisk/no-payload-loading baseline, not a factory-clean or unrooted phone.
3. Launch normally and record startup/range phase, PID, start/end times and any
   exit. Range observation time begins at actual range entry, not app launch.
4. In a fresh process under the same no-Zygisk boot, use the installed
   `/data/local/.stash/KittyInjector` with its actual `-pkg`, `-pid`, `-lib`
   interface to load the inert probe. Verify both its map and constructor/JNI
   marker. Do not infer success from the injector exit code alone.
5. Repeat in another fresh process with the plain overlay. Verify its
   `jni: plain-overlay-init v1` marker, mapped artifact, UI creation and progress.
   The probe and overlay must not be loaded together in this phase.
6. To compare the same plain payload with Zygisk, install that exact artifact
   at the loader's fixed library path, restore Zygisk=1, and reboot. Verify
   actual framework/module/payload activation, not just the database setting.
   The current loader starts after about 200ms; later manual injection differs
   in timing, which must remain an explicit limitation of the comparison.
7. Restore the saved original library, owner/mode/context, original setting and
   module state. Restore any game activity only after verifying the expected
   binary is mapped. Keep diagnostic logs private.

Use plain injection for the comparison, without concealment/path-bypass flags
or SELinux mode changes. The launcher wrapper currently changes SELinux mode
and speaks a different CLI from this installed injector; do not use that
wrapper as the test driver. If enforcing policy blocks injection, record the
block instead of silently changing the environment.

If the no-payload baseline exits, payload loading is not necessary for that
exit. That does not identify its cause or establish that every later exit has
the same cause. If only a payload-bearing stage exits, investigate payload and
initialization/timing effects before attributing it to detection.

## Rendering and visibility scope

The current Android window composites separately from the game's surface; it
cannot interleave boxes/model effects below individual game HUD controls.
A game-side canvas/render pass before HUD composition is needed for primitive
overlays, and model materials or a depth/stencil-aware scene pass for actual
model fills. A late EGL/Vulkan presentation hook by itself does not establish
the desired ordering. The legacy GLES line renderer is not a model renderer.

Unreal 4.27 documents custom-depth/stencil-based post-process effects and
placement within its post-processing graph:
[Epic: Post Process Materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-materials?application_version=4.27).
That supports the architectural direction, not a claim that this shipped game
exposes a usable integration. A verified game-thread/render-thread bridge,
material/pass availability and mobile-renderer support remain to be established.
Do not add an inert Chams toggle or present a filled box as model chams.

Visibility remains a bounds estimate. Target/owner exclusions exist in the
latest source but were not loaded by the long-lived PID 6613, whose maps still
referenced the earlier deleted library inode. Its exposed-bot red result is not
verification of the latest exclusion build.

## Results

Artifact preparation and the separate-process probe smoke test are complete.
Zygisk was set to zero and the phone rebooted; new boot ID
`0c203f6d-c02c-4820-914a-5095391f9f4e`, with no libzygisk in either zygote map.
The user unlocked it. The no-payload game launch at 22:40:34 UTC became PID
12138, and a game-map check found none of the comparison/payload libraries.
The user confirmed range entry; its observation timer is in private
`stage1-state.json`. PID 12138 completed the ten-minute range window with no
observed exit. Near the end, the user confirmed movement, aiming and shooting
remained responsive. This is one successful bounded baseline, not a long-term
stability guarantee or proof that the rooted environment cannot be detected.

The old installed KittyInjector then tested the inert probe in fresh PID 30482.
The constructor marker appeared in that PID, but the injector reported a zero
library base, a symbol-resolution error naming
`Java_com_intlgame_foundation_NDKHelper_runOnUiThreadHandler__JI`, and explicitly
logged that it was killing the target after failure. The probe does not import
that symbol (its dynamic symbol inventory was checked). No JNI_OnLoad marker
appeared. Classify this as an injector post-load/entry-discovery failure and
injector-initiated termination, not an observed game detection/crash.

The launcher-packaged injector reports version 6.0.0 and supports the documented
`--pid ... --libs ...` interface. It was staged separately for an inert-probe
retry. The existing launcher's long-option interface therefore matches its
packaged asset, but not the old installed injector tested first.
The [upstream project](https://github.com/MJx0/AndKittyInjector) documents that
injection failures can force-stop the target, consistent with the old log.
No successful Kitty-to-game entry call or restoring Zygisk phase is yet claimed.

### Canonical-path and foreground results

Using `/data/data/...` resolved the zero-base failure in v6.0.0. Constructor,
JNI_OnLoad, mapped ELF and detached state were verified. PID 31755 was an
automatic background JobInfoSchedulerService restart, so it supplies no game
stability observation. A repeat after a forced fresh foreground launch, PID
32032, also loaded and entered successfully, with TracerPid=0 and SELinux
Enforcing. It exited approximately 26 seconds after injection, before range
entry was recorded. No matching new native/Java fatal record appeared in the
bounded crash buffer. Private `stage2d-result.json` retains samples and timestamps.

The user supplied a historical old-tool command combining memfd loading and
map/linker concealment. Those options were not exercised in this comparison;
the plain canonical-path entry itself worked. No concealment behavior or
claimed detection avoidance was tested.

The next control was revised in response to this failure: use the same inert
probe through Zygisk before adding a scanner/UI payload. The fixed production
library path now holds the probe, Zygisk was set back to 1, and the restoring
framework reboot was requested. The original library remains backed up for
restoration. Different load timing and library paths between the Kitty and
Zygisk controls remain limitations; this is not a definitive detection test.

## November 15 result and current deployment

The matched Zygisk inert-probe process PID 12711 exited after 473.9 seconds of
user-confirmed range observation (94 samples; private `stage4-result.json`).
Both loading paths therefore had exits with the inert payload. The no-payload
baseline survived 603.9 seconds. These bounded observations do not identify the
cause or establish detection; timing and route remain different variables.

Original Zygisk/module/DenyList/SELinux settings are restored. The user asked to
install and test the actual updated overlay, so the probe has been replaced by
production, **not** by the older backed-up build. Current production initialization
omits legacy identity/config/code/report hooks by default; the comparison macro
is no longer needed. Temporary comparison artifacts on the phone were removed.
See the top of `handoff.md` for the current deployed hash and runtime results.

Full build PID 7124 actually rendered the new menu, lines and boxes in range but
still exited after several minutes. The live capture showed false-red exposed
targets and misclassified NPCs. Component Outer inspection identified target-owned
collision components traversed under another actor. A subsequent installed
revision adds that exact ownership exclusion and native ancestry fallback for
failed class-name reads. Its host tests pass. PID 9711 then showed green exposed
targets labelled NPC; the user confirmed some cover works but glass/other walls
can remain green. It exited at 11:09:43 UTC, and Zygote reported SIGBUS (signal7),
without a new tombstone/fatal backtrace. No cause is established yet.
