# ARM64 internal module: Android 10–14

Build from the repository root:

```sh
NDK_HOME=/opt/homebrew/share/android-ndk bash internal/zygisk/build.sh
```

Output: `internal/zygisk/out/delta_zygisk.zip`. The archive contains both the
Zygisk entry library and the current native payload. After changing Java, rebuild
`internal/native/overlay/build_overlay.sh` first to refresh the embedded dex.
The native and loader binaries target API 26, below the requested Android 10
minimum. This is an ARM64 module, not a 32-bit game build.

Install with Magisk's module installer, using the intended device's explicit serial:

```sh
adb -s SERIAL push internal/zygisk/out/delta_zygisk.zip /data/local/tmp/delta_zygisk.zip
adb -s SERIAL shell 'su -c "magisk --install-module /data/local/tmp/delta_zygisk.zip"'
adb -s SERIAL reboot
```

Unlock the phone and allow USB debugging after reboot. A working Zygisk provider
is required: either Magisk Zygisk or a compatible provider such as Zygisk Next.
Do not enable multiple providers. No SELinux policy or root-provider settings are
changed by this module. The testing phone already had permissive SELinux; enforcing
SELinux coverage remains unverified.

## Loader ABI and files

`zygisk.hpp` is the **unmodified official API 2 header**, from
[99468e1](https://github.com/topjohnwu/zygisk-module-sample/commit/99468e1).
The previous file was an API 5 layout with the version changed to 2. Its raw
`nice_name` pointer-offset workaround has been removed. The official API documents
[backwards compatibility and API 2's Magisk 24000 minimum](https://github.com/topjohnwu/zygisk-module-sample#api).

The exact main process name `com.proxima.dfm` is selected through `args->nice_name`.
Other applications, system-server and package subprocesses receive no payload.
Before specialization, the loader reads the bundled payload through `getModuleDir`
into bounded anonymous memory and closes its descriptors. After specialization,
a worker loads a memfd using `android_dlopen_ext` and calls `JNI_OnLoad` with the
process JavaVM. This avoids hard-coded app data paths, UIDs, multi-user paths,
private-file ownership and file-descriptor exemption requirements from newer APIs.
ELF class/machine/type and file size are checked before loading.

The installed payload is now
`/data/adb/modules/delta_zygisk/payload/arm64-v8a.so`. Changing the old
`/data/data/com.proxima.dfm/libue4patch.so` does **not** update this loader. For
routine development, atomically replace the module payload, preserving root
ownership, mode 0644 and its existing SELinux label, refresh `SHA256SUMS`, then
restart the game. Replacing the loader itself requires reinstall/reboot.
The ZIP and `SHA256SUMS` identify both packaged binaries.

## Android version versus GPU version

One module can cover the requested Android versions, but graphics hooks depend
on the GPU driver, rather than the Android API number. Do not select instruction
signatures solely by `SDK_INT` or a marketing GPU name.

| Device | OS | GPU/driver | Implementation |
| --- | --- | --- | --- |
| Previous OnePlus 5 | Android 10 | Adreno 540, `vulkan.msm8998.so` | Existing exact private map/unmap wrapper/callee recognition retained |
| New OnePlus 7T HD1903 | Android 12/API 31 | Adreno 640, `vulkan.adreno.so` | Validated public map ABI and single-RET unmap pair |

The older map wrapper shuffles registers into a private four-argument function;
its unmap wrapper branches to a private function. The new driver's map uses the
standard six-argument Vulkan ABI, while unmap is only `RET`: its backing mapping
persists until free. The new path tracks public map calls and retires backing
pointers on allocation destruction; it never overwrites the four-byte unmap
function or its neighbour. Reads remain bounds checked and use the existing
checked kernel-copy reader. Recognition is exact and fails closed for unknown
map/unmap pairs; arbitrary Adreno or Mali drivers are not supported by this claim.

The newer viewport and stencil functions also contain early B.cond/TBZ branches.
The trampoline relocator now handles B.cond, CBZ/CBNZ and TBZ/TBNZ, including
branches into the displaced block. Unsupported literal loads, internal direct
B/BL, and trampoline-capacity overflow still reject before patching.

The new display uses a 2400×1080 UI and observed swapchains of 720×1600 and
896×2000 with rotation 90°. The existing swapchain transform/extent handling
adapts to these dimensions; the old 1920×1080 UI is not baked into this port.

## Verification, 2026-11-25

Captured Adreno640 Vulkan driver SHA256:
`4ba8a296f71dd944ca4a9afa13eece713db827726fe1a7e1a16ebc854a1553ec`.
Driver bytes were inspected with local NDK llvm-readelf/llvm-objdump; this is
local static analysis, not a retained REA result.

New device serial `7492618f`, Android 12/API31, Magisk 30600, Zygisk Next 1.3.2,
game `1.202.37119.3601`. Module installation, reboot, target-process payload
mapping, native reader initialization and in-game Vulkan UI composition verified.
Diagnostics after the corrected payload restart: journal hooks=18 ready=1,
present hooks ready, public map/persistent backing ready, di/cgp/bp/sv all 1.

Host tests: chams relocation/replay, GPU resource tracking and both driver-profile
selection/rejection, ESP GPU, Vulkan input/presentation. Device ARM64 execution
of `tests/chams_reloc_runtime.c` verifies the captured stencil's four branch
combinations and viewport's conditional branch including zero/overflow cases.
This test runs independently of the game. Compile with the Android ARM64 compiler,
include `internal/native`, push to `/data/local/tmp`, chmod 755 and execute.

The Android 10 device was not connected for this run. Its old instruction path
passes the expanded fixtures, but this exact new module has not yet been tested
on that phone. Android 11/13/14 have no device coverage yet. Firing-range ESP and red/green chams were subsequently observed in the actual
game; more than 2,000 camera frames were captured with missing=0/read_fail=0 and
replay drops=0. Screenshot: `docs/android-compat-2026-11-25/firing-range.png`.
Dynamic alignment quality, long sessions and live matches still need testing. No OS-specific ZIP split is currently required.
