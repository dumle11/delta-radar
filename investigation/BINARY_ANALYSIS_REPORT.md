# Binary analysis of discovery failure — 2026-11-02

Investigation of why the Delta-Radar scanner cannot discover an active UWorld
during loaded gameplay matches. Read-only with respect to application/scanner
code; no commits, no game/scanner launch.

## Tools and prerequisites

| Tool | Status | Notes |
|------|--------|-------|
| REA (morluto/rea) | **Not usable** | Requires Node.js ≥24.22.1; installed is 24.15.0. Also requires Ghidra/Hopper/IDA — none installed. |
| Ghidra | **Not installed** | No package, no local installation found anywhere on this machine. |
| radare2 | Available | `/usr/bin/r2` installed via Kali packages. |
| binutils | Available | readelf, objdump, strings, nm, file — all present. |
| libUE4.so static analysis | **Blocked by packing** | On-disk `.text` (330 MB) and `.rodata` (40 MB) are packed/encrypted. File-wide entropy 6.75. Zero hits for UNetDriver, UActorChannel, ServerMove, DeltaForce. Only JNI exports, libc imports, and ~1 million lines of non-printable fragments are recoverable. |
| Device access | Available | OnePlus 7T connected, game PID 3746 running, root shell functional. |

**REA could not be used.** The `rea-agents@6.8.0` npm package requires
Node.js `^22.23.4 || ^24.22.1 || >=26.12.1`. The installed version 24.15.0
is below the 24.x minimum. Additionally, REA's native binary analysis
requires Ghidra, Hopper, IDA, or Kuna — none are available. Cloned source
is at `/tmp/claude-1000/-home-openclaw-StudioProjects-Delta-Radar/d936748f-1a80-440a-aa0a-d3819ffd0886/scratchpad/rea-investigation/rea/` for reference.

## Artifacts examined

### On-device

| Artifact | Identity |
|----------|----------|
| Game package | `com.proxima.dfm` v1.202.37117.5601, versionCode 2217, minSdk 24, targetSdk 35 |
| Game PID | 3746 (confirmed running) |
| libUE4.so on-device | `/data/app/~~a4byb-QP_b17LJPvWdSg0g==/com.proxima.dfm-l7EYS_pY_cniZTZr3rHyHA==/lib/arm64/libUE4.so`, 571,309,672 bytes |
| split APK | `split_config.arm64_v8a.apk`, 217,311,597 bytes |
| ELF header (pulled) | First 65,536 bytes extracted for analysis |

### ELF structure of libUE4.so

```
Type: ELF64 DYN (shared object), AArch64, little-endian
Program headers: 13
Section headers: 23 (at file offset 571,308,200 — end of file)

LOAD segments:
  R-E  VirtAddr 0x000000000  FileSize 0x1BD0B990 (462 MB)  — code + rodata
  RW   VirtAddr 0x1BD0F990   FileSize 0x38D4A80  (59 MB)   MemSize 0x38D8670
  RW   VirtAddr 0x1F5E8410   FileSize 0x6701A8   (6.4 MB)  MemSize 0x152C2A8 (21 MB incl. BSS)
  RW   VirtAddr 0x20B28000   FileSize 0x247F8A1  (37 MB)   — late data/BSS
```

Runtime mapping (from /proc/3746/maps):
- UE4 base: `0x774a230000`
- UE4 end: `0x776d1d8000` (559 MB span)

### Existing project evidence

- `docs/BASELINE_REVIEW.md`, `docs/SCANNER_DEPLOYMENT.md`, `docs/SCANNER_RESEARCH.md`
- `docs/EVIDENCE.md` — prior REA-backed observations on packing/encryption
- `evidence/local/scanner-milestone/*.log` — 6 scanner session logs
- `evidence/local/mobile-dumper-source/DeltaForce.h` — name decryption profile

### Scanner source

- `scanner/memscan.c` — deployed binary SHA-256 `dfbe410b...`, source SHA `274f08d5...`
- SDK dump offsets from `github.com/eikarna/deltaforce-dump` (Garena v1.203)

## Confirmed findings

### 1. SDK dump offsets are wrong for this build (CONFIRMED)

**Evidence:** The scanner's Phase 1 GNames lookup at `ue4_base + 0x18BACD80`
failed. FNamePool was found only by Phase 2 fallback scanning at a completely
different address.

| | SDK dump (Garena v1.203) | Actual (Proxima v1.202) | Drift |
|--|--|--|--|
| GNames/FNamePool offset from UE4 base | `0x18BACD80` | `0x1BEE1F60` | **+51.2 MB** |
| Segment | Falls in R-E (code) segment | In RW (data) segment, 1.8 MB in | Wrong segment entirely |

The SDK dump's GNames offset points into the **read-execute code segment** of
this build. The actual FNamePool is in the **read-write data segment**. This is
not a small per-field drift — the two builds have fundamentally different binary
layouts. A 51.2 MB global-variable offset shift makes it virtually certain that
ALL fixed offsets from the Garena v1.203 dump are invalid for this Proxima
v1.202 binary, including:

- `OFF_GNAMES_FROM_UE4_BASE = 0x18BACD80` — **confirmed wrong**
- `OFF_GUOBJECTARRAY_FROM_BASE = 0x18BCA1A0` — **presumed wrong** (same region)

**UObject field offsets** (UWorld::PersistentLevel = 0xF8, ULevel::ActorCluster
= 0xC8, AActor::RootComponent = 0x180, etc.) are class-layout offsets
rather than binary-address offsets. They MAY still be correct if both builds
share the same C++ class definitions. However, Garena v1.203 and Proxima v1.202
are different regional builds (Garena Southeast Asia vs Proxima/Level Infinite
global), and version 1.203 vs 1.202 indicates at least one content update apart.
Game updates routinely add or reorder UObject properties, shifting these offsets.

**Status: These offsets are unverified for this build and cannot be validated
by static analysis of the packed binary.**

### 2. FName strings are encrypted (CONFIRMED)

**Evidence:** MobileDumper-7's `DeltaForceProfile` (`DeltaForce.h`) provides
explicit `DecryptUTF8` and `DecryptUTF16` overrides for `com.proxima.dfm`. The
algorithm:

```
Key = f(Len % 9)   // 9 cases, each a different arithmetic function of Len
For each byte:  Data[i] = (Key & 0x80) ^ (~Data[i])
```

This means every FName string (except possibly index 0 "None") is stored
bitwise-inverted with a conditional high-bit XOR. The scanner's `resolve_fname`
rejects any character outside printable ASCII (0x20–0x7E), so **all encrypted
name resolutions return empty strings**.

The scanner found "None" as plaintext at offset +8 in block0 of the FNamePool
(hdr=8). For "None" (Len=4), the encryption would produce non-ASCII bytes
(0xB1, 0x90, 0x91, 0x9A). The plaintext "None" means either:
- Index 0 is special-cased and stored unencrypted
- The FNamePool has a different entry layout than expected

The 8-byte header (vs standard 2-byte) and the encryption together indicate a
custom FNamePool format. The scanner's FNamePool fallback scan found it by
searching for plaintext "None" at various header sizes — this worked for
discovery but does not establish that the pool is generally readable.

**Impact on discovery:** FName resolution is NOT used in the world-discovery
validation chain. `try_validate_uworld` and `is_character_class` operate
entirely on pointer comparison and structural checks. Name encryption does not
directly cause the discovery failure. It DOES prevent `is_loot_class`,
`find_object_field`, and runtime team-field resolution from working once a world
is found.

### 3. The deployed binary lacks discovery diagnostics (CONFIRMED)

**Evidence:** All 6 scanner session logs contain zero `[discovery]` JSON output
lines. The current source code includes comprehensive `DiscoveryDiagnostics`
(~170 lines of counters, per-check pass/reject tallies, top-3 candidate
tracking, per-stage read metrics), but the deployment report explicitly
recommends adding diagnostic counters as the **next step**. The deployed binary
(SHA `dfbe410b...`) was built before the diagnostics were added to the source.

**Impact:** We cannot determine which specific check rejects candidates, how
many pointer slots are examined, what the maximum score reached is, or which
mappings are scanned. The source-level analysis must infer failure modes from
the algorithm structure.

### 4. Scanner infrastructure works correctly (CONFIRMED)

| Component | Status | Evidence |
|-----------|--------|----------|
| Game PID discovery | ✓ | PID 3746 found in all sessions |
| process_vm_readv | ✓ | 4 bounded 16-byte reads succeeded (ELF magic verified at base) |
| /proc/maps parsing | ✓ | 15,005 regions parsed, UE4 base/end identified |
| FNamePool discovery | ✓ (fallback) | Found by Phase 2 rw scan at 0x7766111f60 |
| RW region scanning | ✓ | 5 libUE4 rw regions scanned (~98 MB) |
| Session/lease system | ✓ | Proper start/stop/cancellation |
| Socket/feed transport | ✓ | Verified by 8 device tests |

### 5. Game heap pointer range is correct (CONFIRMED)

**Evidence:** `is_game_heap_ptr` checks `0x6e00000000 ≤ p < 0x7800000000`.
The device's /proc/3746/maps shows **9,163 rw regions totaling 2,678 MB** in
this range, including all major libc_malloc allocations (64 MB, 210 MB chunks)
and the libUE4.so data sections. The UE4 module itself maps at
0x774a230000–0x776d1d8000, within this range.

### 6. RW scan coverage is adequate for the BSS/data case (CONFIRMED)

The scanner scans all named `libUE4.so` rw regions (~98 MB) plus anonymous
regions within 256 MB of UE4's end. The ELF program headers show ~115 MB of
RW segments. The scanner's 98 MB coverage matches well (the difference is BSS
zero-fill that may map as separate anonymous regions). If GWorld is a BSS
global variable in libUE4.so, it SHOULD be in the scanned area.

## Hypothesis ranking

### H1 (HIGHEST): UObject structural offsets are wrong for this build

**Probability: Very high**

The 51.2 MB GNames offset drift proves the two binaries have different layouts.
UObject field offsets (`UWorld+0xF8 → PersistentLevel`, `ULevel+0xC8 →
ActorCluster`, etc.) are C++ class-layout offsets that depend on the engine
version, build configuration, and any custom class modifications. With version
1.203 vs 1.202 across different regional publishers, these offsets are very
likely shifted.

**Predicted failure mode:** Every pointer in the BSS that happens to be a valid
heap address gets `try_validate_uworld` called. Reading at the wrong
PersistentLevel offset (0xF8) returns arbitrary data from the middle of whatever
heap object that pointer actually references. This data almost always fails the
first structural check (`is_heap_ptr(level) && !is_in_ue4(level)`), causing
immediate rejection. No candidate ever reaches score 70.

**Why zero candidates appear:** With ~6.5 million pointer slots scanned and
wrong structural offsets, the probability that random data at `ptr + 0xF8`
passes `is_heap_ptr`, AND `ptr + 0xF8 + 0xC8` also passes, AND the array
fields are plausible, is astronomically low. The multi-stage validation is
specifically designed to reject random data, and it does so effectively — even
against real heap objects that aren't UWorld instances.

### H2 (MEDIUM): GWorld pointer is not in the scanned BSS regions

**Probability: Medium-low**

If the game uses a custom allocator or stores the GWorld reference in a
non-libUE4 memory region (e.g., a separately mapped anonymous page beyond the
256 MB scan radius), the scanner would never find the pointer to scan.

**Against this hypothesis:** Standard UE4 stores GWorld as a global variable in
the engine's BSS section. The scanner scans all libUE4.so rw regions.

**For this hypothesis:** Custom anti-cheat integrations (libanogs.so/libanort.so
= Tencent ACE) could relocate or obfuscate the GWorld pointer. The anti-cheat
could also intercept `process_vm_readv` or return decoy data for specific
address ranges.

### H3 (LOWER): Anti-cheat interference with memory reads

**Probability: Low-medium**

Tencent ACE (Anti-Cheat Expert) is present (`libanogs.so`, `libanort.so`). It
can potentially:
- Detect `process_vm_readv` access patterns
- Return modified data for specific addresses
- Clear or relocate sensitive global pointers

**Against:** Four bounded reads succeeded with correct data (ELF magic verified).
The scanner has been running for extended periods without access denial.

**For:** ACE could selectively interfere with reads at specific offsets (like the
GWorld pointer location) while allowing reads at other addresses.

### H4 (LOWEST): UWorld doesn't exist during observed gameplay

**Probability: Very low**

Two loaded matches were observed at timers 29:25 and 29:36. A UWorld with
actors should exist during active gameplay.

## Scanner assumptions: supported, contradicted, or unresolved

| Assumption | Status | Evidence |
|------------|--------|---------|
| libUE4.so can be read via process_vm_readv | **Supported** | 4 reads succeeded, ELF magic verified |
| UE4 base address from /proc/maps is correct | **Supported** | First mapping matches ELF load address |
| GNames at ue4_base + 0x18BACD80 | **Contradicted** | Actually at 0x1BEE1F60 (51.2 MB drift) |
| FNameEntry has 2-byte headers with plaintext ASCII | **Contradicted** | 8-byte headers; strings are encrypted |
| UWorld::PersistentLevel at +0xF8 | **Unresolved** | Cannot verify from packed binary |
| ULevel::ActorCluster at +0xC8 | **Unresolved** | Cannot verify from packed binary |
| ULevel direct actor arrays at +0x98/0xA0/0xA8/0xB0 | **Unresolved** | Cannot verify from packed binary |
| AActor::RootComponent at +0x180 | **Unresolved** | Cannot verify from packed binary |
| USceneComponent::RelativeLocation at +0x164 | **Unresolved** | Cannot verify from packed binary |
| GWorld is a BSS global in libUE4.so rw sections | **Unresolved** | Plausible but not confirmed |
| Game heap at 0x6e00000000–0x7800000000 | **Supported** | 2,678 MB of rw allocations in range |
| is_heap_ptr range (0x10000–0xFFFFFFFFFFFF) | **Supported** | All observed allocations in range |
| Anonymous regions near UE4 need scanning | **Supported** | BSS regions near libUE4 are in scan range |
| FName resolution can identify actor classes | **Contradicted** | Encrypted names; only "None" readable |
| Garena v1.203 offsets apply to Proxima v1.202 | **Contradicted** | GNames 51.2 MB off; builds differ |

## Smallest next diagnostic

**Deploy the diagnostic-enabled scanner build.**

The current source already includes comprehensive `DiscoveryDiagnostics` that
track:
- Per-check pass/reject tallies for all 32 validation checks
- Top-3 candidates by score with per-path failure masks
- Per-stage read metrics (calls, complete, partial, failed, bytes)
- Mapping selection reasons (selected, not-rw, executable, named-other, etc.)
- Read error errno distribution
- Maximum score reached across all candidates

This is the **single highest-value experiment**: one stationary loaded-match
capture with the diagnostic scanner would reveal:
1. How many pointer slots are examined
2. How many pass the initial heap-pointer check
3. At which stage candidates fail (level pointer? cluster? array? player sample?)
4. The maximum score any candidate achieves
5. Whether any read errors occur at specific stages

**No code changes to the discovery logic are needed** — only building and
deploying the current source, which already has the diagnostics. This requires:
```sh
python3 scanner/tools/build_scanner.py --ndk "$ANDROID_NDK_HOME" --pin-app
python3 scanner/tools/deploy_scanner.py --serial DEVICE_SERIAL \
  --manifest scanner/build/api31/manifest.json \
  --evidence evidence/local/investigation
```

If the diagnostics confirm H1 (wrong offsets), the next step is runtime offset
discovery. MobileDumper-7's `Layouts.cpp` demonstrates the correct approach:
runtime probing of memory to discover UObject field offsets, FNamePool layout,
and GWorld location — rather than hardcoding values from a different build's
SDK dump.

## Limitations

1. **Static analysis of libUE4.so is blocked.** The 462 MB code/rodata segment
   is packed/encrypted with entropy 6.75. No disassembler can recover meaningful
   code or data structures from the on-disk binary. Runtime analysis (memory
   dumps of the unpacked code at runtime) would be needed to verify offsets.

2. **REA is not usable.** Node.js version mismatch (24.15.0 < 24.22.1) and
   no Ghidra/Hopper/IDA installation. Even if installed, the packed binary
   would prevent meaningful analysis.

3. **No discovery diagnostics from deployed binary.** The exact rejection stage
   cannot be determined from existing logs. The diagnostics exist in source but
   weren't in the deployed build.

4. **UObject layout offsets cannot be statically verified.** The packed binary
   prevents extracting class layouts. Runtime memory probing during gameplay is
   the only viable approach.

5. **FName decryption is not integrated.** Even if discovery succeeds, name
   resolution requires implementing the `DeltaForce.h` decryption algorithm.
   The scanner's `resolve_fname` currently assumes plaintext ASCII.

## Summary

The scanner's world discovery fails because it uses hardcoded offsets from a
Garena v1.203 SDK dump that are demonstrably wrong for the installed Proxima
v1.202 build. The GNames offset alone is 51.2 MB off, falling in the wrong ELF
segment. While UObject field offsets (class-layout offsets) are independent of
binary-address offsets and could theoretically still be correct, the version and
publisher mismatch makes this unlikely. Additionally, FName strings are
encrypted, preventing all name-based validation and identification.

The scanner infrastructure (memory access, mapping discovery, transport) works
correctly. The failure is specifically in the **hardcoded offset assumptions**
used to interpret the memory layout of UE4 objects.

Deploy the diagnostic-enabled scanner build for one match capture. The
diagnostics will confirm which validation stage fails and quantify how far
candidates get before rejection. This evidence determines whether the fix is
adjusting known offsets or implementing runtime offset discovery.
