# Offline latency audit — 2026-11-02

Historical audit of an earlier candidate. Branch names, counts, installed state
and device-check status below describe that audit only. Current accepted build
and the next measurement plan are in [CURRENT_STATUS.md](CURRENT_STATUS.md).

This candidate is on `working-v1`, in [PR #2](https://github.com/dumle11/delta-radar/pull/2).
The phone was off and charging during this audit. These are source-level fixes
with local regression coverage; device responsiveness, player alignment and
crash stability have not been verified for this candidate.

| Finding | Change |
| --- | --- |
| Projection ownership ended before the UI published, allowing old UI callbacks to accumulate. | Keep one operation active through publication, with one coalesced request for the newest snapshot. Schedule against display frames and request updates when accepted feed data arrives. |
| Every timer tick copied and projected the same snapshot again. | Skip already published sequences; retain a low-rate fallback for lifecycle and settings changes. |
| A new feed service restarted its sequence at zero, below the old service's last published sequence. | Scope publication order to the feed instance and visible overlay generation. |
| Diagnostic atomic writes and fsync ran on the caller, including the draw thread. | Capture evidence at the existing bounded rate, then serialize and write on a background worker with a three-item queue. Debug capture stays opt-in. |
| A 50 ms screen-coordinate animation added camera lag. Drawing allocated paints, connection arrays, formatters and temporary rectangles repeatedly. | Draw the latest projected sample consistently, reuse drawing objects, and avoid rebuilding a map of already validated unique IDs. |
| Failed decoding was resubmitted each scan, sharing a FIFO with loot. Requests for departed actors remained queued. | Use bounded, coalesced work with character priority, fair loot turns, failure backoff, initialization backoff and expiry of untouched requests. Ignore old-process completions. |
| Decoder results were timestamped at completion, hiding their sampling age. Filling a 512-entry cache cleared every coordinate. | Preserve the sample timestamp; remove expired entries or the oldest entry when making room. |
| Newly registered characters waited for the next full level/loot cache rebuild. | Refresh additions from the small validated combat roster between full scans. Preserve cached loot and give combatants priority if capacity is exhausted. Full scans retain their existing rate. |
| Sending a frame could block the camera collector for a configured one second. Buffered packets were aged only from receipt. | Configure a 33 ms send timeout, reconnect after incomplete sends, and include a boot-clock packet timestamp. Android rejects already expired or future-dated packets and preserves source age. Older test feeds without the optional timestamp remain supported. |

## Validation

- ARM64/API-31 native scanner build passed.
- Decoder scheduling/cache eviction and roster-cache tests passed with AddressSanitizer and UndefinedBehaviorSanitizer.
- Native packet timestamp, enemy/loot source isolation, and SDK discovery/name/class regression checks passed.
- Android debug APK, device-test APK and all 35 JVM tests passed.
- Android lint passed with zero errors; 163 warnings remain, mainly existing UI, layout and style issues. This is not a warning-free audit.
- No device tests or live frame-time measurements were run while the phone was unavailable.

The latest APK, native binary, build manifest, JUnit results, lint reports and
validation summary are kept in ignored `evidence/local/session/audit-20261102/`.
The app pins the matching native identity through `scanner/deployment.properties`.
Build provenance records the pre-audit base commit plus hashes of all native
inputs, including the new headers. No new binary has been installed on the phone.

## Remaining live checks

Verify a visible living character at close range, camera turns, new character
arrival, nearby loot, Hide/Show, reconnect and stalled-feed expiry. Compare frame
timing with debug capture disabled and enabled. The last game exit at 19:51:24
was recorded as signal 7 (SIGBUS), without a new native stack trace; this audit
does not establish or fix its cause. Private crash logs remain local.
