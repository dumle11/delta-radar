# Position protection: observed behavior and open questions

Date: 2026-11-03. This report consolidates existing captures and SDK declarations.
It does not identify the complete cipher, recover a key, or provide a decoder.
No new device attachment, injection, protected-memory read, or game modification
was performed for this report.

## Main findings

1. Protection is selective in the recorded scene. The initial sample contained
   29 verified character actors: 4 had both bounds and component-transform
   encryption flags set; 25 had both flags clear.
2. The captures support interpreting these fields through the matching engine
   metadata rather than treating their first three floats as coordinates.
   Finite, plausible-looking numbers are not proof of usable world positions.
3. A clear encryption flag alone does not certify a value. Earlier captured
   getter instructions also tested whether the handler had a registered index.
4. Alternative cached fields were not independently validated. In one later
   sample, 7 distinct remote actors had only 3 distinct cached movement vectors,
   despite those cached fields carrying clear flags. This is insufficient
   evidence for using them as current world positions.
5. No captured evidence establishes a single XOR cipher or an XOR key. Pointer
   encoding, name encoding, coordinate protection and hash-table operations must
   not be treated as interchangeable algorithms.

The figures above describe bounded samples, not every actor, streamed level,
account, match type or game build.

## Evidence and provenance

The historical environment was Android 10/API 29 with game version
1.202.37117.5601, version code 2217. The recorded process identity remained the
same across the relevant range and live-match observations. This shows that a
reboot was unnecessary for that process to exhibit different field states; it
does not establish what caused the difference.

The initial actor sample examined the first 512 entries of a 552-entry
persistent-level actor list. The later probe independently rediscovered current
pawns through player-state relationships. The two samples must not be combined
as if they contained the same actor lifetimes or represented one atomic frame.

The report's [summary and capture hashes](evidence/encryption-observation-summary-2026-11-03.json)
record the exact inputs used for the counts. The original captures and their
28-entry manifest remain under `/tmp/delta-live-encryption-evidence`. All 28
stored artifact hashes matched that manifest when checked for this report.
Temporary files are not a durable archive; hashes alone cannot recreate them.

## What the SDK declares

The matching [CoreUObject declarations](../CppSDK/SDK/CoreUObject_structs.hpp)
define a handler containing an index, an encryption-state byte, and behavior
bits named dynamic, shared-key and bitwise-copyable. An encoded-vector type
combines three floating-point fields with that handler. The encoded-transform
type contains rotation, translation, scale and handler metadata.

These names describe a data representation. They do not disclose the cipher,
key schedule, key lifetime, or the meaning of every runtime mode.

The [encryption subsystem declaration](../CppSDK/SDK/GPEncryption_classes.hpp)
contains collections for actors, components and encryption information. It also
declares an encryption-settings function. The generated wrappers establish the
existence of reflected APIs, not their complete implementation or their callers.

## Which fields were affected

| Field or subsystem | Recorded observation | Supported conclusion |
|---|---|---|
| Character bounds origin | 4 of 29 actors flagged in the initial sample | Raw XYZ reads are unsafe for those records |
| Root world transform | Flagged for the same 4 actors | Bounds are not the only affected representation |
| Mesh world transform | Flagged records also occurred in the captured actors | Mesh transforms are not an established universal fallback |
| Relative position | Flagged for all 7 remote actors in the later sample | Changing to relative position does not establish a solution |
| Last-frame position | Clear flags occurred alongside duplicated cached values | Clear metadata does not prove freshness or geometric correctness |
| Movement last-update position | 7 actors shared 3 distinct cached vectors | Independent visible-position validation is still required |
| Local camera position | Sampled with an unmanaged index and clear flag | This specific camera sample was admissible; remote actors need separate validation |
| Actor lists and player-state links | Traversable in the recorded probes | The observed coordinate failure was not explained solely by an inaccessible actor list |
| Names and object classification | Separate resolution problems were recorded | Name decoding failures do not establish coordinate encryption |

Duplication alone is not mathematical proof that a coordinate is false: distinct
actors can occupy the same location. Here it is a reason to withhold validation,
especially without an independently correlated visible body and motion trace.

## Getter behavior: narrow static observation

Earlier readable ARM64 getter snapshots showed a conditional path that used
both a runtime mode and handler registration state before dispatching to a
runtime callback. Consequently, checking only the state byte omitted part of
the getter's decision.

This establishes conditional dispatch, not what the callback does internally.
The captured code spans were partial and do not establish complete function
coverage, all callers, or the key material. They were not executed as part of
this report.

The reader's validation was tightened to require an unmanaged handler and a
clear flag, with a separate transform-consistency check. That is a rejection
rule for unverified data, not coordinate recovery or proof of a cipher.

## The forum's match-join configuration lead

The supplied page 83 image, ordinal post #1649, visibly shows a Lua match-join
handler passing a notification field named `enc_flag` to
`SetEnableEncryptionBits`. A nearby branch separately references class-related
encryption settings.

This is evidence of the control flow shown in that image. It suggests that
match-join configuration can influence protection settings in the represented
build. The complete source, build provenance, actual notification value and
execution on this Android device have not been established. It does not prove
that reports, bans or a specific account state caused the local behavior.

In particular, a field named `enc_flag` passed to a settings API is not evidence
that the field is an encryption key.

The supplied screenshots also contain examples concerning names and pointers.
Their XOR operations do not identify the algorithm applied to position fields.
Several images are clipped or partly blank, so missing portions remain unknown.

## REA method and limitations

This report follows the
[reverse-engineer-anything skill](/Users/dme/.agents/skills/reverse-engineer-anything/SKILL.md)
by separating declarations, captured observations, static interpretation,
inferences and unknowns, and checking artifact identity before reusing results.

The earlier REA attempt admitted a small ELF wrapper around captured bytes, but
Hopper analysis was denied macOS Automation permission. It produced no successful
native dossier or pseudocode. Historical instruction observations came from the
local LLVM disassembly recorded in the earlier investigation.

A fresh REA evidence-bundle lookup for that partial ELF's exact SHA-256 returned
zero retained records. Therefore this report does not cite a nonexistent REA
Evidence ID or present the old disassembly as a new REA verification. No provider
configuration or host permissions were changed.

## What remains unknown

- The exact algorithm used for each protected field, including whether any
  stage uses XOR and whether different fields use different mechanisms.
- Any key, key derivation, rotation schedule or session-specific state.
- The precise scope and meaning of the match notification's settings bits.
- Whether the cached duplicate values are stale, substituted, or produced by
  some other update behavior.
- Whether the friend's installation uses identical game, APK and reader builds.
- Why rebooting reportedly changes behavior, and whether that observation is
  reproducible with all other variables held constant.
- Whether pointer/list encoding observed in other builds occurs on this device.

These gaps prevent an exact account of the complete encryption mechanism. They
must not be filled with guessed constants, transplanted PC algorithms, or a
claim that coordinate recovery has been solved.
