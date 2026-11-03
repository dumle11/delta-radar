# Forum screenshots: position evidence

## Coverage

All 90 supplied screenshots were processed locally with OCR. Screenshots 1–89
are exactly 16,384 pixels high and may be truncated; screenshots 14, 70 and 76
visibly contain no readable forum posts. With the user's permission, the forum
was then opened in the browser: all 90 pages returned successfully, with
1,792 post bodies (20 per page except 12 on page 90). Their text was searched
for position, actor-list, encryption, Android and account-flagging terms, and
selected relevant posts were read in full. This is a targeted text review, not
a manual analysis of every post or of linked images and downloads. Search
excerpts were limited to eight matches per post; other relevant wording could
be missed. Forum statements and code are unverified. No third-party code or
forum offsets have been adopted.

## Relevant claims and limits

- **Page 16:** A poster reports ActorList and LocalPlayers pointers changing
  after a match. Replies attribute runtime encryption to account flagging and
  describe a dynamically generated PC routine. These are unverified forum
  claims, not evidence that this mobile account was flagged.
- **Page 22:** A separate poster describes a PC page-table obfuscation change
  allegedly affecting everyone and asks whether it resembles account-specific
  encryption. Page-table obfuscation, encrypted pointers and encrypted vectors
  are different problems; none proves the cause of the Android symptoms.
- **Page 11:** A participant identifies an older China Android AArch64 SDK.
  Its presence is evidence of a related mobile build, not a match to the
  installed regional game or a validated decoder.
- **Page 58:** A PC camera-location example checks an encrypted-vector
  handler before calling a decoder. A mobile participant reports apparently
  normal camera coordinates despite the encrypted-vector type; another
  participant speculates on when encryption is enabled. These are useful
  reasons to examine handler state and camera freshness separately, but do
  not establish that every valid-looking vector is plaintext or that
  encryption is tied to flagging.
- **[Pages 59](https://www.unknowncheats.me/forum/other-fps-games/653290-delta-force-hawk-ops-reversal-structs-offsets-59.html),
  [64](https://www.unknowncheats.me/forum/other-fps-games/653290-delta-force-hawk-ops-reversal-structs-offsets-64.html):**
  Posters disagree about Android encryption. A Garena
  Android participant reports that external RelativeLocation or
  ComponentToWorld values stop tracking movement. Another reports that a
  runtime-modified component-transform accessor redirects to a different
  address. This is a build-specific claim, not proof of the same mechanism on
  this device. The previously captured mobile accessor does check a handler
  and call a dispatcher, but those observations do not establish equivalence
  to the forum's claimed redirection.
- **[Pages 69](https://www.unknowncheats.me/forum/other-fps-games/653290-delta-force-hawk-ops-reversal-structs-offsets-69.html)–[70](https://www.unknowncheats.me/forum/other-fps-games/653290-delta-force-hawk-ops-reversal-structs-offsets-70.html):**
  One Garena Android post claims a runtime transform lookup
  backed by an array indexed using a scene-component-related value; a later
  post says a changed build used a binary-search lookup. The quoted example
  mixes internal access with external-reader pseudocode and is not a verified
  drop-in implementation. Its version, identity mapping, lifetime, memory
  permissions and coverage of character bounds are unknown. Do not copy its
  offsets or execute its functions against the game.
- **Page 66:** A transform example combines a decoded mesh transform with bone
  data and discusses external decoding. It contains PC-specific assumptions
  and unresolved questions about calls/offsets. It does not validate the
  mobile root-bounds path.
- **Page 89:** A poster claims a Windows-build test resolved several enemy
  positions through a dynamic provider and describes a separate code-patching
  approach. The claim is not independently verified here; the x86-64 build,
  offsets, provider layout and target writes are not portable to Android.
  The report's distinctions between pointer chains, vector crypto, camera
  state and health freshness are useful diagnostic hypotheses.
- **Page 90:** A PC projection example and a discussion of missing boxes
  distinguish projection/team-classification errors from selective
  encryption. Another poster claims plaintext coordinates can be found by
  scanning Garena memory, but has not established reliable actor identity or
  bot filtering. Neither establishes which failure applies to this device;
  uncorrelated coordinate scans can produce ghost actors.

## Android follow-up

The existing mobile scanner has observed encrypted or managed handler states
for some character positions, but no safe decoder has been verified against a
visible living character. Raw external reads can succeed while the values
remain unusable. Before changing the decoder or discovery scan, correlate the
scanner's world/actor/health and position-verification diagnostics with a
known visible player and build identity. The diagnostic table exposes only
already-read, filtered data and cannot establish account status, distinguish
all encryption causes, enumerate omitted actors, or prove that coordinates
are correct. Any new live memory investigation requires its own bounded
safety plan and approval.
