# Delta Radar — Handoff (2026-11-14)

## Post-reinstall investigation — current state

The user reinstalled from Play Store and downloaded assets; crashes continued.
The current package path is
`/data/app/com.proxima.dfm-1W1zNfsN8IuxPlwcvgGaaw==/`, UID **10556**, version
`1.202.37119.3601`, versionCode 2365, installed at 16:19:06. The prior package
UID was 10555. **The recovery archives below belong to the prior installation;
do not blindly restore their UID/context or account files over the new install.**
The proposed package-data reset is superseded by the user's completed reinstall;
no further reset was performed by the agent.

The reinstalled game has no private `libue4patch.so`, its overlay app-op is
default, and the loader module remains disabled. The genuine build fingerprint
is still active and `/data/adb/service.d` is empty. `/data/local/tmp/libue4patch.so`
has SHA-256 `f94aa27981427c1bc69c354e4b0c8ef45387e1305db0e21b57091b92a6d5e37f`
(the checked-read, clean-initialization build, not the old patching build).
KittyInjector exists, but the requested injection/hide command has **not** been
executed during this continuation; a crashing vanilla client is being tested first.

**Root-environment hypothesis:** disabling an overlay/module does not remove
Magisk. Local CLI inspection found Magisk 28.1, Zygisk=0 and DenyList=0, with no
game entry. Two unrelated existing DenyList entries were preserved. For a
controlled per-app-exclusion test, added `com.proxima.dfm|com.proxima.dfm`, enabled
DenyList enforcement, restored global Zygisk=1, and verified both settings before
requesting reboot. The custom loader stays disabled. Pre-change settings/list
are protected under `/tmp/delta-reinstalled-root-test/`. Post-reboot verification
confirmed enforcement and game membership. PID 12518's mountinfo had zero
matching Magisk-related mount records, versus five in the root shell namespace.
The untraced game nevertheless exited with SIGSEGV at 16:43:10. See
`denylist-reinstalled-exit.log`. The root-exclusion test therefore did not fix
the crash or establish its exact trigger.

**Configuration-history correction:** this agent removed calls to `_apply_cfg`,
identity/build spoofing and process-wide fault handlers/watchdog to isolate
allocator/signal-chain failures. That also removed legacy behavior which may
have affected root/tamper responses. A payload-disabled run is not equivalent
to removing root from the phone. Module disable markers for Shamiko/LSPosed and
the other non-project modules mostly date to Nov 13 around 16:32; they were
already disabled before this continuation. No such module was re-enabled here.

KittyInjector `-h` was executed only to inspect usage. Its strings contain a
JavaVM lookup and automatic JNI_OnLoad entry-call path; there is no evidence
that the supplied command needs a separate invented JNI flag. Its hiding flags
describe handling of the injected library, not removal of Magisk from Android.
The injection command itself remains unexecuted.

### Completed comparison: saved pre-Magisk boot image

Found `/data/magisk_backup_4f5b9a4c7e71c406b758e45f64b28e7c16dd0910/boot.img.gz`.
Read the current boot partition into an audit copy; **nothing was written to
any partition**. The current ramdisk's `.backup/.magisk` declares original SHA1
`4f5b9a4c7e71c406b758e45f64b28e7c16dd0910`, which exactly matches the saved
decompressed image. Both boot headers identify Android 10, patch 2020-04,
header version 0, page size 4096, and identical kernel command lines. The initial
compressed/decompressed kernel hashes differed; inspection established exactly
the documented Magisk `skip_initramfs` → `want_initramfs` change. Applying that
change to an **audit copy** of the original decompressed kernel produces an
exact match to the active decompressed kernel. Kernel-DTB hashes also match.
The original ramdisk's Magisk patch test returns 0 and its listing has no
checked Magisk/SuperSU marker paths.

Prepared images and verification manifest are protected under
`/Users/dme/Backups/delta-boot-comparison-2026-11-14/`. The bootloader reports
unlocked (`ro.boot.flash.locked=0`). The reviewed prospective action is a
**temporary** `fastboot boot stock-boot.img`, after verifying device identity in
fastboot. Fastboot help describes this as downloading/booting from RAM. Do not
flash partitions or relock the bootloader. Normal reboot returns to the installed
Magisk-patched boot image. **The user explicitly approved this temporary boot
test.** Image hash and device product were rechecked, the game was stopped, and
`adb reboot bootloader` requested. On the first transition the user selected
Start and returned to the installed boot before image loading. A second scripted
transition verified serial `20f9aa6c` and fastboot product `msm8998` (matching the
live Android platform), then ran `fastboot boot stock-boot.img`: 64 MiB transfer
and Booting both returned OKAY. No partition was flashed.

Android subsequently completed startup after a delay while the bootloader screen
had remained visible. An early host query saw neither transport; no fallback
reboot was actually issued. The user reported the phone started by itself and
unlocked it. Verification during that comparison showed `sys.boot_completed=1`, boot reason
`bootloader`, no `su` command on PATH and no Magisk-related process. This verified
the temporary pre-Magisk runtime. **The phone has since returned to its installed
rooted boot; the temporary image is no longer running.**
`verified-boot-manifest.json` records command acceptance and Android verification.
The game was launched untraced as PID **15607** and exited with **SIGBUS at
17:09:58**, despite the verified inactive Magisk runtime. See
`temporary-pre-magisk-game-exit.log`. This establishes that active Magisk is not
necessary to reproduce an exit; installed tools, cached SDK/server state and the
unlocked bootloader remain, so it does not rule out all root-related detections.
Serial-property consistency was also checked: runtime serial equals boot serial,
and persistent radio serial equals boot serial rather than the saved old spoof
value. No speculative serial/property reset was performed.

The temporary comparison is complete. A normal reboot returned to the installed
rooted boot, verified by boot completion and `/sbin/su`. Current installed-boot
settings remain Zygisk=1 / DenyList enforcement enabled, with the custom loader
disabled. No partition was flashed or bootloader relocked.

An external read-only capture using
`internal/native/tests/termination_context.c` succeeded on PID 10504, TID 16773,
with TracerPid=0; the game subsequently exited with SIGBUS at 17:23:57.
The helper did not attach, pause, signal, or write target memory. It saved a
bounded stack and 27 candidate context blocks. Captures are private under
`/tmp/delta-private-termination-context/`, outside the repository: they may contain
credentials or session data and must not be committed/shared. The captured SDK
request label was `gp3_fri` in a forced-dispatch path. **The label does not prove
Frida was detected, identify the initiating check, or establish a remedy.**
Observed empty handler slots were sampled after cleanup and do not establish
that initialization failed. No SDK checks or branches were patched.

Further bounded checks found both `libanogs.so` and `libanort.so` loaded in
PID 24881. The sampled maps did not match Frida/Xposed/Riru/Gadget module names;
checked logs showed no matching linker/missing-symbol error. No configured
HTTP proxy, Private DNS override, always-on VPN, relevant hosts-file block,
or Unreal command-line override was found in the inspected paths. No matching
listeners appeared in the bounded default-port/Unix-socket checks. These negative
checks are not proof of a factory-clean environment. Existing instrumentation
binaries are present in `/data/local/tmp`; their presence is not evidence that
they were loaded or caused this exit. They have not been deleted or hidden.

A fresh Android user **12 / DeltaCrashTest** was created, started, and given
an install-existing reference to the Play Store game. Its setup-complete flag
was set. **No gameplay, asset copying, account sign-in, or clean-profile stability
test was completed.** The profile was subsequently stopped and retained; it is
not a successful comparison. Primary user 0 remains current. Existing Work
profile 10 and Guest 11 were preserved. Protected state is recorded in
`/Users/dme/Backups/delta-boot-comparison-2026-11-14/clean-profile-test.json`.
The user has not tested this same account on another unmodified phone, leaving
account/server versus device state unresolved.

REA/Hopper became unavailable during additional analysis (provider launch error,
then `cleanup_incomplete`). No additional successful function analysis should be
claimed. An empty evidence export is under `/tmp/delta-reinstalled-root-test/`;
the documented close retry also failed. Direct LLVM disassembly remained usable.
No Hopper installation, provider repair, or manual runtime-directory deletion
was performed.

**Scope for further work:** normal crash diagnostics and restoration of the
unmodified client are appropriate. Do not inject the payload, hide injected
code, patch anti-cheat decisions, or alter identity to evade enforcement. The
exact initiating condition and a stable remedy remain unverified.

The user supplied [ff-ace-anticheat-analysis](https://github.com/Lixense/ff-ace-anticheat-analysis).
Its author analyzes Free Fire OB54 **ARMv7**, whereas this target is Delta Force
**ARM64**. Its descriptions of root/debug/integrity checks and multiple
termination paths are relevant research leads; its offsets/patch claims must
not be applied to this target. Read the exact current binary evidence below.

## Historical reset commands — new evidence

The user supplied prior shell history confirming repeated `reset_account.sh`,
manual `pm clear com.google.android.gsf` / `com.google.android.gms`, module changes
and payload copies with an obsolete UID. The current script has broad destructive
side effects, a hardcoded SSAID key, and live Google database deletions; see
`docs/overlay-2026-11-14/reset-history-audit.md`. It was inspected, not executed.
The user signed back into **one** Google account to reinstall the game. Existing
recoverable authentication errors in Play services/Gmail/Calendar can therefore
reflect other accounts still needing sign-in. Do not equate those errors with
the game's deliberate native exit without causal evidence.

Current read-only checks: GSF database snapshots pass SQLite quick_check; a GSF
registration ID exists; game SSAID XML parses and current UID/value/default are
consistent; Google packages are enabled; Wi-Fi is validated; about 28 GiB is free.
No account/identity/database repair was attempted, and no random old identifier
was restored. Private captures are outside the repo under
`/tmp/delta-google-health-20261114/`. Further resets would destroy useful evidence
and are not an established remedy. A fresh ordinary launch (PID 7446) reached the main menu, then exited at
**18:08:12**, before entering Firing Range. It was present at foreground-monitor
40.4 seconds and absent at 45.6 seconds. The private payload is absent and loader
disabled. This capture has `am_proc_died` but no Fatal signal, Java exception,
ANR, matching low-memory kill, or Google authentication error. The kernel's
SIGCHLD-to-zygote record does not identify the initiating cause. Do not label
this latest exit SIGBUS or infer a specific SDK condition from earlier captures.
Sanitized evidence: `normal-startup-180812-exit.log`. The initial screen-off
process-survival interval was not a valid stability comparison.

## Freeze/crash investigation — current changes

This section supersedes earlier descriptions of initialization and fault recovery.

### Latest finding: deliberate termination mechanism identified

The sampled PID 26726 / fault TID 27438 was observed sleeping with a return
address exactly `libanogs.so + 0x47e6c8`, before its first SIGBUS at `0x10b5`.
The matching installed/APK library contains `sub_47e678`: its termination branch
sleeps ten seconds, computes a low address from `random()` masked to 12 bits plus
`0x1000`, copies it into x0/FP/LR/SP/x17, and branches to that address. This is a
deliberate register-destruction/fault path, not a normal failed allocation.
The prior PID 25662's first-fault registers x0/x8/x17/x29/x30/SP/PC all equalled
`0x13b4`, matching that mechanism. This explains both the low invalid execution
addresses and the loss of usable unwind state in these captures.

The random low target also explains differing initial signals: the observed
aligned unmapped target `0x13b4` produced SIGSEGV, while unaligned `0x10b5`
produced SIGBUS/BUS_ADRALN. **Different exit signals alone do not establish
different causes.** Earlier cold-baseline SIGSEGV exits without registers could
still be this path; the AuxiliaryRHI fault at `0xf0` with a mapped PC is a
separate observation and must not be conflated with this register-poisoning.

Evidence: `libanogs-termination-disassembly.txt`,
`fault-thread-before-termination.log`, and `clean-initial-invalid-registers.log`.
The original library SHA-256 is
`a5ec3e9abcc9a4b96baf87342e39142cf51818a200e7ff15e8b185a376884d97`.
REA/Hopper independently analyzed `sub_47e678`, evidence
`ev_8ae445e0d41ef2fbbc162d08300033db5f1646206d07048402a228cb599b37e9`.
Full exported evidence is `/tmp/delta-apk-audit/libanogs-evidence.json`; the REA
session was closed after export. Pseudocode has provider limitations; assembly
and the runtime return address are the useful correlation here.

**Limits:** this establishes the mechanism for the traced capture, not the exact
check/reason that selected it. Tracing itself can influence security checks.
Earlier untraced failures establish that instrumentation is not necessary for a
crash, but do not independently prove every failure used this identical path.
The separately observed null-pointer SIGSEGV on an AuxiliaryRHI-named thread
must be treated separately; the thread name alone does not establish which
module or graphics component faulted. Likewise, the sampled GameThread name
does not prove this is UE's principal game thread: its pre-fault frames map
into `libanogs.so`.
No security code, signal handlers, checks or binary branches were patched.

**Global Zygisk comparison (did not fix the crash):** Magisk's global `zygisk` setting was still 1
when individual modules were disabled. It was backed up to
`/data/local/tmp/delta-rollback-20261114/zygisk-setting-before.txt`, changed to 0
and verified, then rebooted. This tests rollback of the injection runtime itself;
the loader disable flag remains present, the boot-property override remains
disabled, and genuine firmware properties should remain. The user was asked to
unlock after reboot and leave the game closed for verification. After boot,
`sys.boot_completed=1`, keyguard was not showing, the firmware fingerprint was
genuine and `zygisk=0` was verified. Launched PID 9622 with no debugger/signal
observer; the user reported it closed/crashed and a later `pidof` was empty.
Its exact exit signal was not retained in the inspected log buffer. Turning
off the injection runtime therefore did not fix the failure.

**Game overlay-permission comparison (did not fix the crash):** the installed APK does not
request SYSTEM_ALERT_WINDOW, but its app-op was explicitly `allow`, matching the
earlier overlay setup command. Backed up that observation in
`/tmp/delta-apk-audit/overlay-appop-before.txt` and set only this app-op back to
`default`; verified it. Launched PID 26945 untraced, still with Zygisk/loader and
property boot override disabled. The live log reader captured SIGSEGV exit at
15:36:56. See `default-overlay-permission-exit.log`. Internal overlay startup
will require a deliberate permission grant again if later re-enabled.

**User-confirmed baseline:** the same installed game version was reliably stable
before the internal-overlay changes, not merely an older game version. Continue
auditing persistent effects instead of attributing this to a version change.

**Private-directory rollback now running:** disabled-loader comparisons still
left the custom payload and other owned files inside the game's private data.
The earlier "no payload" test only renamed its library in that same directory,
which is not equivalent to removing these artifacts. Five files were copied to
`/data/local/tmp/delta-rollback-20261114/app-artifacts/`, each copy SHA-256-verified,
then removed from their original paths: `libue4patch.so`, `cache/.fids`, `cache/d`,
`cache/d.previous-overlay`, and `shared_prefs/overlay_visuals.xml`. A cross-policy
rename was initially rejected (EPERM), so verified copy-then-remove was used.
No original app files or account data were cleared. PID 28435 was the subsequent
untraced comparison; the live log reader recorded SIGBUS at 15:41:55. Removing
these files alone therefore did not fix the failure. See
`owned-artifacts-removed-exit.log`.

An additional persistent generated artifact was identified:
`oat/arm64/Anonymous-DexFile@353099944.vdex` contains the current
overlay DEX checksum `a8e00b15`. There are eight small Anonymous-DexFile VDEX
files; the others were not yet mapped to a particular overlay build. Copies
are under `/tmp/delta-apk-audit/vdex/`. All eight generated VDEX files were then
copied to `app-artifacts/oat/arm64/` in the on-device rollback directory, hash-
verified, and removed from their original paths while the game was stopped.
PID 29817 was the untraced comparison afterward; it still exited with SIGBUS at
15:44:56. See `verification-cache-reset-exit.log`. Cache cleanup did not fix it.

**Recovery backup completed and verified:** the persistent host recovery folder
is `/Users/dme/Backups/delta-client-recovery-2026-11-14/` (directory mode 700,
files mode 600). It contains private session/account data and must not be
copied into the repo or printed. The first direct root-stream archives failed
validation and are named `*.unverified-stream`; **do not use them**. On this
device, `adb exec-out su -c ...` has terminal newline processing: one known
469-byte VDEX became 470 bytes with a different hash. `stty raw -echo` restored
its exact device hash in a controlled test. This also means earlier unverified
binary captures through that route must be treated cautiously; APKs obtained by
`adb pull`, device-computed ASCII hashes, and textual signal logs are unaffected.

Replacement archives were created as files on the device with tar diagnostics
redirected separately, then copied via `adb pull` (binary-safe). The temporary
device backup directory is mode 700, owned by shell; archives are mode 600.
The game was force-stopped. Post-stop SHA-256 lists cover private and external
regular files. Verification passed for **415 private + 164 external files**,
with every archived regular file matching its device hash and archive paths
validated. The private archive is 3,852,564,480 bytes and external archive is
110,411,776 bytes (3,962,976,256 bytes total). `verified-manifest.json` records
their SHA-256 hashes; both were checked again after moving to the persistent
folder. `verify_backup.py` and `RECOVERY.txt` are retained alongside them.
One Unix socket was omitted by tar; it is a runtime endpoint, not a regular
data file. File backups do not guarantee recovery of a live/guest session;
confirm that the user can sign in before a destructive reset.

No package-data reset or original game-settings reset has been performed.
Device `pm clear` deletes all package data; this Android version does not
advertise a cache-only flag. A broader data reset requires explicit user approval
because it clears local sessions/settings even with rollback archives prepared.

**Persistent-state audit (latest continuation):** a separate Magisk boot script,
`/data/adb/service.d/spoof_props.sh`, remained active independently of all module
disable flags. Its contents match the boot-persistence section of this repo's
`reset_account.sh`. It reapplies serial/build property overrides after every
boot. The observed `ro.build.fingerprint` ended in `18CAE261`, while the vendor
firmware fingerprint ended in `2006012143`. Thus earlier module-disabled reboots
still had this persistent override and were not firmware-default comparisons.

The script and pre-change properties were backed up under
`/data/local/tmp/delta-rollback-20261114/` (properties file mode 600). The active
script was moved outside `service.d` to `spoof_props.disabled`; the loader module
disable flag was created, then a reboot requested. The phone took several
minutes to boot and the user unlocked it. ADB returned; `sys.boot_completed=1`
and build/vendor fingerprints now both end in `2006012143`. The boot-script
directory is empty and loader disable flag is present. The user launched the
game without our loader and reported another crash before Firing Range. PID
9978 exited with SIGSEGV at 15:18:16. This comparison had no signal observer.
Thus rolling back the boot property override alone did not fix the game.

**Installed-file integrity:** pulled the installed base and ARM64 split APKs;
`apksigner verify` succeeds for both (base signer CN PROXIMA BETA). SHA-256
comparison of every ARM64 library in those APKs against extracted installed
files gives **79/79 matches**, no extra installed libraries. This rules out
leftover patches to those extracted libraries; it does not verify downloaded
resources or all user data. See `installed-library-audit.txt`.

**Shader-cache comparison (failed to fix the crash):** with the game stopped, moved
`/sdcard/Android/data/com.proxima.dfm/files/VulkanProgramBinaryCache` to sibling
`VulkanProgramBinaryCache.before-20261114`. It contained a 281,051-byte pipeline
cache and 252-byte LRU from Nov 13; it has not been deleted. Launched PID 24673
with loader still disabled, no tracer, and original boot properties. The game
created a new cache directory and reached Firing Range. The user confirmed
movement responded, then reported a crash there. Zygote confirms SIGBUS at
15:21:20. The regenerated pipeline cache is 206,536 bytes with a 204-byte LRU.
Cache regeneration therefore did not fix the failure. Both old and regenerated
caches remain available. User settings and login data were not changed. Current
device state intentionally has the loader disabled for testing.

**Further bounded environment checks:** no `wrap.*`, allocator-debug overrides,
Vulkan/HWUI debug overrides, GPU-debug layers, or ANGLE selections appeared in
the inspected property/global-setting keys; debug_app is null and
wait_for_debugger is 0. Process-name filtering found no Frida, dumper, scanner,
GDB, or radar process. These are bounded observations, not proof that every
system modification is absent. Kernel output has Adreno allocation-alignment
warnings, but no inspected GPU page-fault evidence tying them to this exit.

PID 25662 is being sampled with `signal-observer-sampled.c`: only GameThread-
named threads are briefly interrupted every five seconds for bounded frame-
pointer samples; original crash signals remain forwarded. The aim is to connect
the actual fault TID to an earlier mapped caller, since the existing small
invalid-PC faults do not preserve a usable caller. This diagnostic perturbs
timing; compare with the already-established untraced failures.

The historical reset script also changes persistent radio properties and Android
IDs and deletes app/services data. It was **not executed** during this audit.
Original values for those earlier destructive changes have not been established;
do not invent replacement IDs or clear app data as a purported rollback. The next
read-only audit should compare installed native libraries against APK entries for
possible on-disk patches, then test a launch without tracing or our loader.

**Latest range repro:** the user reached Firing Range on PID 23534 and it stopped
responding to input at 15:05:28, then exited with SIGBUS at 15:05:32. The checked
reader had reached 17 entities and a valid camera, then kept logging with an
invalid camera until exit. Android's ANR collection was too late to include the
game's stacks. A subsequent run, PID 24707, also reached the range (18 entities,
valid camera and visible skeletons) and exited with SIGBUS at 15:07:45. An earlier
live stack snapshot is saved, but does not identify the actual faulting thread.
Evidence: `range-exit-23534.log`, `range-exit-24707.log`, and
`native-stack-range-before-exit.txt`. These reproduce the failure on the deployed
clean-initialization build; the original issue remains open.

**Scanner/overlay-disabled comparison:** `/data/local/tmp/.noscan` was created
and PID 25891 logged `s0: skip`, confirming neither started. The user reports it
crashed before any interaction or entry to Firing Range. A signal observer caught
SIGSEGV on thread 26167, named `AuxiliaryRHI`, with `si_code=1`, address `0xf0`,
then a second SIGSEGV at `0x3b`; Zygote confirms exit with signal 11 at 15:09:27.
This establishes a startup graphics-thread crash independently of the scanner
and overlay; it does **not** establish that the range SIGBUS has the same cause.
No corresponding new game tombstone exists. See `noscan-graphics-signals.log`
and `noscan-graphics-exit.log`.

A further disabled run, PID 26792, also logged `s0: skip`. The bounded observer
caught **SIGBUS on GameThread at invalid `PC=LR=0x1302`**, `BUS_ADRALN`, then
additional faults through host crash handlers. No executable mapping contains
the first PC, and the first frame pointer could not produce a usable caller.
See `noscan-gamethread-signals.log`; the observer forwards original signals and
detached/exited without killing the game. This establishes that the SIGBUS form
also occurs with no scanner or overlay running. It does not prove which game
component or device condition corrupted control flow. Do not infer causation
from the thread name or hide the fault with security/signal patches.

**Comparison cleanup completed:** `.noscan` was removed and its absence verified.
The payload hash still matches the deployed clean-initialization build. The
next launch will start the overlay normally. No unrelated modules, app data,
graphics caches or game settings were changed during these comparisons.

The latest code **does not call** the legacy identity spoofing, memory masking,
code/GOT patching or signal watchdog from `JNI_OnLoad`. The scanner no longer
installs/reinstalls fault handlers or uses `sigsetjmp` recovery. Historical helper
functions remain in the C source but are not invoked by the initialization path.
The unused external camera-stream worker is no longer started internally.

`external/scanner/self-memory.h` provides checked copies through a private,
thread-owned, nonblocking pipe. Kernel `write`/`read` returns `EFAULT` for bad
source memory without entering host crash handlers. Transfers are bounded by
`PIPE_BUF`, partial copies are rejected, the channel is drained after errors,
and thread-owned descriptors are closed on scanner cleanup. No extra device
permission or change to host signals is required. This replaces direct pointer
`memcpy` plus process-wide signal recovery in all library read helpers. The
external standalone reader retains its existing process-memory access path.

An initial `/proc/self/mem`/pread version was rejected by the game's app context
with `Permission denied` and stopped cleanly. It was replaced by the pipe-copy
implementation rather than changing SELinux or permissions. The pipe version
ran in the app context and logged 17 entities and a valid plaintext camera.

**Passing checks:** host and Android checked-read fixtures test 300 rejected
reads, subsequent valid reads, unreadable/unmapped pages, a page-boundary copy,
large multi-chunk copies, overflow/invalid arguments and unchanged
SIGSEGV/SIGBUS/SIGABRT handlers. Projection and full synthetic skeleton-reader
tests also pass. Clang static analysis found eight dead stores (mostly diagnostic
expressions compiled out), no reported memory error; this does not prove the
whole payload is bug-free.

**Remaining exit, independently measured:** the pipe-copy build's PID 32457
exited with SIGBUS, confirmed by Zygote (`Process 32457 exited due to signal 7`).
No new native tombstone was produced. There was ample available RAM and no
corresponding low-memory-killer record in the inspected kernel output.

A bounded ptrace observer forwarded original signals unchanged. On PID 5266,
thread 5874 (named GameThread) first stopped with SIGBUS, `si_code=1`
(BUS_ADRALN), `PC=LR=0x197a`, then additional fault signals before termination.
This is not evidence of an externally sent SI_TKILL signal. The printed sender
fields share the siginfo union and must not be interpreted for positive hardware
fault codes. Tracing changes timing; these records alone do not establish
whether a game bug, its protection code or another environmental issue caused
that invalid address. Observer cleanup completed without killing the target.

A stronger comparison moved the payload file aside before launching PID 6243;
its maps did not contain `libue4patch.so`, and no scanner/overlay initialized.
That process also exited with SIGBUS, confirmed by Zygote. The payload file was
restored afterward. The Zygisk loader was still present in this comparison, so
it is not yet equivalent to a stock, unmodified-device baseline. A cold comparison disabled only `delta_zygisk`, rebooted, and launched PID 10663.
That baseline also crashed: Zygote reports signal 11 (segmentation fault), recorded
in `cold-disabled-loader-exit.log`. The exit signal differs, but this does not
establish a different root cause without first-fault registers. The module's disable flag was
then removed and the device rebooted to restore its original module state.
Restoration was verified after reboot: the module disable flag is absent and
the deployed payload still has SHA-256
`f94aa27981427c1bc69c354e4b0c8ef45387e1305db0e21b57091b92a6d5e37f`.
The new tombstone (`tombstone_08`) belongs to `com.qti.diagservices`, PID 8717,
not the game; it cannot explain the game's cold-baseline crash. The overall
game-crash problem is not resolved by the reader/initialization fixes alone.

The restored build launched as PID 23534 and confirmed checked-copy initialization
and successful overlay creation. An early stack snapshot shows Android main
polling, the reader sleeping, and UE GameThread sleeping; it does not show the
earlier allocator deadlock. This single snapshot does not establish stability.
Capture: `native-stack-clean-init.txt`. Host checked-read and projection fixtures
were rerun successfully after restoration.

Evidence: `signal-origin-read-only-build.log`, `signal-observer.c`,
`read-only-build-exit.log`, `no-payload-exit.log`, and
`cold-disabled-loader-exit.log` under
`docs/overlay-2026-11-14/`. The previous allocator/crash-handler stack captures
remain relevant to the original freeze. Do not report all crashes fixed merely
because unsafe recovery and startup mutations have been removed.

## Read this first: current status

The user reports three current issues: the menu could not be dragged, the overlay
flickers, and the game freezes after a few seconds. When asked, the user confirmed
**both the game and the overlay menu freeze**. Stability is not established.

The latest build includes header dragging, completed-frame presentation and
checked kernel memory copies without process-wide signal interception. Its
regressions pass; the previously deployed payload is now archived outside the
game directory for client recovery. Legacy process mutations no longer
run. Device comparisons still expose game exits without our payload or module;
see the investigation above. Do not report overall stability as fixed.

A previous successful range run showed boxes following the camera and enclosing
mannequins, with 17 entities and a valid plaintext camera. That establishes one
working tracking run, not reliable launches, menu-model alignment, live skeleton
rendering, or long-term stability.

The complete earlier handoff, including previous-session history and claims, is
preserved in [the pre-update archive](docs/overlay-2026-11-14/handoff-before-drag-flicker.md).
This document supersedes conflicting claims there. In particular, “fully
neutralized,” “confirmed stable,” and “fork causes reconnecting” were not proven
by this investigation.

## Latest build and device state

- **Current recovery state:** the loader module is disabled and Magisk's global
  `zygisk` setting is now 1 with DenyList enforcement enabled for the game,
  verified after reboot. This test still crashed. The user reinstalled the app;
  no custom payload is present in its private directory, and its overlay app-op
  is `default`. The overlay is intentionally unavailable. The separate property
  boot script remains disabled. No alternative boot image has been booted.
- The old pipeline-cache copies are preserved in the prior-installation recovery
  archive. Original external paths may have been removed by reinstall; recheck
  before using them. Cache regeneration did not fix crashes.
- Last built library SHA-256 (no longer installed in game data during recovery):
  `f94aa27981427c1bc69c354e4b0c8ef45387e1305db0e21b57091b92a6d5e37f`.
  The local and device hashes matched after deployment.
- Device: OnePlus 5 / ONEPLUS A5000, Android 10, serial `20f9aa6c`.
- Package: `com.proxima.dfm`; launch component:
  `com.proxima.dfm/com.epicgames.ue4.SplashActivity`.
- Recorded installed version: `1.202.37119.3601`; screenshots display
  `AppVer:1.202.37119.0 ResVer:1.202.37119.23`.
- Current app UID/owner: `10556` / `u0_a556`, private context
  `u:object_r:app_data_file:s0:c44,c258,c512,c768`. Prior archive UID was 10555.
- Previous payload location: `/data/data/com.proxima.dfm/libue4patch.so`, mode 755,
  SELinux context `u:object_r:app_data_file:s0:c43,c258,c512,c768`.
  It is now backed up under `delta-rollback-20261114/app-artifacts/` outside the
  game directory. Recheck owner/context before future deployments; the build script's obsolete
  `u0_a142` deployment example was removed.
- Overlay permission was allowed on the prior install and its menu was observed.
  The freshly installed game's app-op is now default; no menu is active.
- `.noscan` was removed after the isolation test; it must not remain accidentally.
- The old stop file is archived as
  `/data/local/tmp/.s7xk.previous-overlay-stop`; `.s7xk` itself was removed.
- Rollback copies on the device include:
  `/data/local/tmp/delta-overlay-previous.so`,
  `/data/local/tmp/delta-overlay-before-tabs.so`, and
  `/data/local/tmp/delta-overlay-before-drag-flicker.so`.
- The tree contains uncommitted source edits, generated DEX/header/library changes,
  tests and evidence. No commit, PR, publication or unrelated module enablement
  was performed. Preserve other existing changes.

## Architecture and files

The internal library is loaded into the game by the existing Zygisk loader.
Current `JNI_OnLoad` stores the VM and starts the checked reader/overlay, except
when `.noscan` exists. It does not invoke the legacy identity, memory, code/GOT
or signal modifications. In earlier builds those changes ran before `.noscan`,
so historical scanner-disabled tests were not clean baselines. Even the newer
no-payload comparison still had the loader; the cold disabled-module comparison
isolated that variable as well. Magisk/the rooted device environment remained.

`internal/native/delta_radar_lib.c` includes
`external/scanner/memscan-legacy.c`; the two implementations are not independent.
The scanner hands entity/camera data directly to `esp_overlay.h`, which invokes
static Java methods in `O.java` through JNI. Java is compiled into a DEX embedded
in `overlay_dex.h`, then into `libue4patch.so`. The Java overlay uses Android
`TYPE_APPLICATION_OVERLAY` windows: a non-touchable drawing surface, a draggable
bubble and a touchable menu.

Relevant paths:

| Path | Purpose |
| --- | --- |
| `internal/native/overlay/O.java` | Frame presentation, menu, input, preferences |
| `internal/native/esp_overlay.h` | JNI API, projection, boxes, skeleton lines |
| `internal/native/delta_radar_lib.c` | Initialization and scanner loop |
| `external/scanner/memscan-legacy.c` | Camera/entity reading and discovery |
| `external/scanner/skeleton-reader.h` | Validated animated pose and bone selection |
| `external/scanner/tests/skeleton_reader_regression.c` | Synthetic bone/wire fixture |
| `internal/native/tests/` | Android UI and host regressions |
| `internal/native/overlay/build_overlay.sh` | Reproducible DEX/header generation |
| `internal/native/build_lib.sh` | Native arm64 build |
| `external/android/.../SkeletonProjection.kt` | External skeleton projection reference |
| `external/android/.../EnemyLinesView.kt` | External box/skeleton rendering reference |

The old `esp_render.h` GL hook was still installed, despite the former handoff
calling it unused. Its include and startup installation call were removed. The
Java overlay is now the only active renderer in this library's startup path.

## Confirmed findings and changes

### Updates stopped at frame 300

A root-owned `/data/local/tmp/.s7xk`, dated November 7, was left on the device.
The scanner checks it every 300 iterations, then stops. The app UID could read
but could not unlink the file from `/data/local/tmp`, so it persisted across
launches. Instrumented logs repeatedly ended at frame 299. Archiving this file
allowed runs to continue beyond frames 1400, 3500 and 4400.

Stop detection, failed unlink and cleanup now write diagnostics. This explains
one frozen-overlay cause; it does not explain every game freeze.

### Missing overlay during the comparison

We created `/data/local/tmp/.noscan` for an isolation run. That intentionally
skips scanner startup and overlay creation, while earlier initialization still
runs. The user later launched the game, could enter the range, and saw no overlay.
The marker was the reason for that missing overlay. It was removed and a new
library was deployed and launched. Do not leave this marker after an experiment.

### Camera lifetime and projection

- Camera validity is reset for each feed frame rather than retained indefinitely.
- Self matching uses the complete `self_entity_id`, including replacement-pawn
  IDs, rather than a literal `se` prefix.
- The renderer requires a reader-valid camera with finite position/angles/FOV.
  A pawn-origin fallback is not treated as an accurate rendered camera.
- The selected cache is reset when the camera manager changes, and discovery
  retries after 45 invalid frames, including transitions within the same UWorld.
- Projection uses horizontal FOV and uniform pixel scale after accounting for
  aspect ratio. Head and feet are projected separately.
- The intended total width is 0.45 times character height; the previous code used
  0.45 as half-width, doubling the width. Half-width is now `height * .45 * .5`.
- Individual offscreen head/feet endpoints no longer invalidate a visible box.
  Final bounds are culled. The arbitrary 800px height clamp and distance-based
  fallback box were removed.
- Skeleton segments can be considered even when the capsule box is offscreen;
  invalid/rear endpoints are skipped and Android clips visible segments.

Menu preview worlds still produced oversized/off-model actors in earlier
screenshots. A good range screenshot does not validate preview-actor selection.

### Menu features and dragging

The menu now has Visuals, Colors and Status tabs. It offers master enable,
boxes, skeleton, names/types, health, distance and snap lines; HSV category colors
for players, bots, teammates and loot; and line weight. Settings use app-private
SharedPreferences `overlay_visuals`. Saved user preferences override defaults.

Previously **only the bubble had drag handling**. The menu had none. The latest
change adds a title/header drag handle, an 8px movement threshold, clamping to the
measured viewport, and stored `menuX`/`menuY` coordinates. Reopening the menu uses
the stored coordinates. The close button, tabs, switches, sliders and scrolling
are outside the drag handle. The subtitle tells the user to drag the header.

The panel is nominally 760×700 physical pixels, reduced for smaller viewports.
It is still a native Android overlay, not an ImGui rendering-pipeline integration.

### Flicker: concrete code paths and latest fix

The former UI `onDraw()` used `tryLock()` and returned without drawing if the
producer held the frame lock. A redraw at that moment could therefore show an
empty transparent frame. A single invalid camera sample also immediately
published an empty bitmap, even when the next sample was valid.

The latest presentation path uses three preallocated `Frame` objects, each with
one bitmap, one reusable Canvas and an atomic reader count. The UI pins the latest
completed frame, draws it, then unpins it. The single producer selects a frame
that is neither published nor pinned. If no spare is available, it drops that
production attempt instead of blanking the UI. No shared frame lock is used.

A half-finished frame cannot replace the last completed frame. Publication uses
`postInvalidateOnAnimation()`. Canvas objects are reused; bitmaps are not explicitly
recycled while a reader might still hold them. Resizing allocates a new pool and
lets old frames be garbage-collected after their readers release them.

Native rendering now preserves the last completed frame through a transient
invalid camera sample. A continuous invalid-camera interval still expires after
500ms on the Java side. Isolated empty/failed entity reads are debounced: explicit
clearing occurs on the third consecutive empty/failed read. Disabling visuals
still hides/clears immediately; rescan and viewport-change invalidation remain.

The expiry timer checks every 250ms, so an unrefreshed frame can remain for about
500–750ms while the UI is responsive. Expiry cannot run while the whole process's
UI thread is frozen. Three 1920×1080 ARGB buffers use about 23.7 MiB, plus temporary
old buffers during resize. Real-device sustained flicker behavior still needs
verification; passing synthetic tests is not sufficient.

### Freeze and signal-chain findings

The user confirms the game and menu both freeze. Earlier native captures showed:

- A reader waiting in jemalloc during `CanvasJNI::initRaster`, with UI waiting
  in `O.onDraw` on the old frame lock.
- A GameThread in `je_extent_heap_first` / `extent_recycle` through Vulkan
  `vkCreateImage`, while several other threads waited for allocation locks.

This suggests process-wide allocator/driver or initialization/lifetime problems,
but does not prove which earlier hook or scanner operation caused them.

Canvas allocation was first removed from each frame. The latest buffer path also
removes UI dependence on a producer-held frame lock. These changes do not repair
an arbitrary process-wide allocator failure.

Scanner startup installed the global fault handlers a second time after
`JNI_OnLoad`, overwriting saved previous handlers with our own handler. The
redundant scanner-thread installation was removed in an earlier mitigation.
The current redesign removes the library's global handler installation/recovery
entirely and uses checked kernel copies. Historical saved-mask tests described
in the archived handoff no longer describe the active read path.

The latest post-frame-publication capture for PID 28997 reproduced a broader
failure: Android MainThread was blocked in `je_malloc_mutex_lock_slow` through
`extent_record` / `je_free` while the Looper shrank a Vector. Many other threads
also waited in jemalloc. Thread 29408 showed nested CrashSight / ART signal-chain
frames interrupting `je_tcache_bin_flush_small`, with the original allocation
called from `libanogs.so`. This is strong evidence of allocator/crash-handling
reentrancy or corruption, not proof that the UI frame-buffer code caused it.
The reader was still logging frames above 9000 with `camera=0`. Full capture:
`docs/overlay-2026-11-14/native-stack-post-frame-fix.txt`.

The device drag input/screenshot check stalled during this process-wide failure,
so header dragging is currently verified by Android synthetic events, not a
successful live drag in that frozen process. Do not claim flicker or game-wide
freezing is solved from the buffer tests.

A subsequent capture for PID 27714 showed the Android main thread polling,
reader threads sleeping and the main UE GameThread in a timed condition wait;
it did not reproduce the earlier allocator stack. Captures attempted for PIDs
24923/25879 failed because the target was gone or could not be dumped. Do not
label those failed captures as evidence of a particular deadlock.

The user observed one crash during a scanner-disabled range attempt, then a
later scanner-disabled attempt entered the range. Later enabled runs also
included successful tracking and intermittent exits. No consistent minimal
reproduction or unmodified-payload baseline has been established.

### Skeleton compatibility and selected bones

The internal renderer consumes the existing external reader's validated pose,
using the same 17 selected body joints. No fallback/artificial skeleton is drawn.

Selected mesh indices:
`1,4,6,7,10,30,31,32,34,35,38,58,59,60,62,63,64`.

Selected parent indices:
`-1,0,1,2,3,1,5,6,1,8,9,0,11,12,0,14,15`.

Parent indices must precede their child. The reader checks active pose buffer,
asset topology, attachment transforms, finite positions, geometry and supported
profile before publishing a pose.

The older bone-count getter was at RVA `0x1acda0fc`; its bytes no longer matched
this build. A read-only executable-segment search found exactly one expected
getter at `0x1b040308`. Loaded memory confirmed:
`f9438c08 b4000068 b941d100 d65f03c0`.
These instructions retain the mesh+0x718 / asset+0x1d0 layout check. The reader
accepts either address only if that exact signature matches; it does not accept
an unknown signature. Both profiles pass the synthetic reader fixture.

Live skeleton lines on training mannequins have not yet been verified. A toggle
being present or a valid getter signature does not prove the actor's pose passed
the remaining checks. The archived SDK is from November 2, version
`1.202.37117.5601`, not a full verified SDK for this installed release.

## Validation and evidence

Passing checks so far:

- Android arm64 library build and generated-DEX embedding.
- Host projection: center, scale, aspect/orientation, rotation, offscreen
  endpoints and invalid values.
- Host/Android checked reads: 300 rejected bad reads, subsequent valid reads,
  unreadable/unmapped and partial transfers, and unchanged host signal handlers.
- Skeleton transform math.
- Full Android synthetic skeleton reader: old/new getter profile, active buffer,
  17-joint topology, transforms, wire payload and rejection of invalid state.
- Android overlay: measured dimensions, resize invalidation, publication, expiry,
  disabled/empty frames, 2000 reused frames, pinned-reader exclusion, preservation
  of a completed frame while production is incomplete or all spares are busy.
- Android menu: header drag, edge clamping, remembered coordinates, tabs,
  skeleton switch and palette getter.
- `git diff --check`.

The skeleton reader test originally expected long JSON names. Its expectations
were corrected to the existing protocol keys `bv`, `bc`, `b`, `bp`; the actual
bone checks already passed before that wire assertion failed.

The separate legacy reader test still fails `test_character_identity` on both
baseline HEAD and the modified reader. That is a pre-existing failure. The full
Linux reader tests do not compile directly on macOS; use the NDK/device. Do not
silently count that suite as passing.

Evidence in `docs/overlay-2026-11-14/` includes menu previews and range screenshots
before and after a camera turn. Stack/log excerpts are retained separately when
available. Screenshots establish the visible instant, not continuous smoothness.

## Build, tests and deployment

From repository root:

```sh
bash internal/native/overlay/build_overlay.sh
NDK_HOME="$HOME/Library/Android/sdk/ndk/28.2.13676358" bash internal/native/build_lib.sh
python3 internal/native/tests/test_projection.py
python3 internal/native/tests/test_safe_reads.py
```

`build_overlay.sh` uses Android SDK platform 31 and build-tools 35.0.0 by default.
It compiles in a fresh temporary directory, embeds only production Java classes,
and generates `_overlay_dex` / `_overlay_dex_len`. Override `ANDROID_SDK_ROOT`,
`ANDROID_HOME`, `ANDROID_JAR` or `D8` if needed. Avoid accidentally globbing
`OverlayRegression.class` into the production DEX.

Android UI test commands are in `internal/native/tests/README.md`. The test writes
three PNG menu previews under `/data/local/tmp/delta-menu-{0,1,2}.png`.

For the reader fixture, cross-compile
`external/scanner/tests/skeleton_reader_regression.c` with
`aarch64-linux-android26-clang -O2 -pthread ... -lm`, push it under
`/data/local/tmp`, and run it on the device. This fixture uses its own synthetic
memory and does not need the game.

Deployment requires rebuilding DEX after Java edits, then rebuilding native.
Back up the device library, force-stop the game, push/copy the new library,
restore the verified app owner/mode/SELinux context, compare SHA-256, and relaunch.
Do not use a hard-coded UID from an old session. The existing overlay app-op was
already allowed; only investigate it if logs show an initialization failure.

## Diagnostics and device control files

| Path / record | Meaning |
| --- | --- |
| `/data/data/com.proxima.dfm/cache/d` | Monotonic timestamped diagnostic log |
| `frame N: read begin/end/render end` | Separates reader progress from drawing |
| `camera=0` | Invalid reader camera; can also occur when backgrounded |
| `cam:` | Selected self/camera position, rotation, FOV and plaintext marker |
| `stop sentinel detected` / `scanner stopped` | Explicit scanner shutdown |
| `.noscan` | Skips scanner and overlay at next library initialization |
| `.s7xk` | Stops scanner on its next 300-iteration sentinel check |

Read privileged logs with `adb shell su -c 'cat ...'`; a normal `adb pull` of the
private app cache was permission-denied. First determine the current PID and
foreground activity; no PID, a failed dump, a paused camera and a frozen game are
different observations. Reader progress does not establish that GameThread or
Android UI is progressing.

## Remaining work, in priority order

0. **No stability fix established.** Temporary pre-Magisk boot, reinstall,
   payload-disabled startup, and per-app-exclusion comparisons all failed to
   establish stability. The phone is back on its installed rooted boot. Test
   profile 12 is stopped and untested. Preserve the current UID-10556 install
   and keep prior UID-10555 backups separate. A normal-client test on an
   unmodified supported phone, using the same account if available, would help
   distinguish account/server state from this modified device; the user has
   not performed it. Use ordinary crash evidence for game support rather than
   treating an internal SDK label as a confirmed detection reason.
1. **Reproduce and fix the game-wide freeze.** Confirm the foreground phase,
   current PID and exact onset. Capture relevant thread stacks while it is
   actually frozen, distinguishing UI, main UE GameThread, render/RHI and reader.
   Correlate with monotonic frame/camera logs. Do not call a short successful run
   a stability fix. Investigate allocator/driver/global initialization effects
   separately from UI frame presentation.
2. **Verify the latest drag/flicker build on the physical device.** Drag the title
   away from its initial position, reopen it, operate tabs/sliders/switches and
   rotate if practical. Check continuous camera movement with menu open/closed.
   Verify short bad samples hold a completed frame while sustained loss expires.
3. **Verify live skeleton poses.** Count valid poses, identify rejection reasons
   without disabling validation, and compare the selected joints against the
   working external renderer. Do not invent bones when data is unavailable.
4. **Fix preview-world actor selection / menu alignment.** Earlier menu boxes
   included oversized/off-model preview actors. Keep this separate from the
   verified range box projection.
5. **Resolve the baseline character-identity regression** without weakening class
   validation merely to make the fixture pass; check whether its layout is stale.
6. **Complete session transitions and teardown checks:** menu → Loadout → weapon
   → Modify → Firing Range; exit/re-enter; background/foreground; stop marker;
   orientation and stale-frame expiration. User calls Loadout “Prepare.”
7. Treat reconnecting and all earlier device-ban/root/anti-cheat effectiveness
   claims as unverified. No new spoof-module enablement, offset-based security
   changes or claimed ban bypass was performed here.

## Reference project

The user's older source is at
`/Volumes/SharedData/Backups/MacBook Pro 16 2019/dev/delta-internal`.
Its `jni/copy` contains a small ImGui diagnostics window (frame count, boxes,
lines), plus Vulkan integration experiments. It is not the complete external
settings UI and has not been ported into this implementation. Source browsing
needed no binary decompilation. REA guidance was consulted for runtime work;
its persistent GDB workflow is Linux-only, so Android stacks were collected with
available `adb`/`debuggerd` instead. No debugger-provider installation was done.
