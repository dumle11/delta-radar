# Evidence — server communication & entity management in base.apk (`com.proxima.dfm`)

All Evidence IDs reference REA records; raw JSON is in this folder. String-level
observations on native binaries were produced with `strings`/`nm`/`objdump`
(marked *raw observation*) and via REA `inspect_with_objdump` /
`inspect_byte_entropy` (Evidence-backed).

## 1. Identity

| Fact | Value | Evidence |
|---|---|---|
| Package | `com.proxima.dfm` (Delta Force Mobile, Proxima Beta / Level Infinite) | `ev_686ce430e41c51781c15ed5e2d450aaf067188399054962694b856baa3ae750d` |
| Version | 1.202.37117.5601 (versionCode 2217), minSdk 24, targetSdk 35 | same |
| Engine | Unreal Engine 4 (`com.epicgames.ue4.GameActivity` launcher activity; `lib/arm64-v8a/libUE4.so`, 571 MB) | same + `ev_6cdbb4d9dd4ebce15c760646f6bb155ebb9feee098fbf85f42a489232559c777` (split extraction) |
| base.apk inventory | 613 file occurrences, complete coverage | `ev_8e41d7ec24bf1a20a2e2ae0ebbde8014cea2df98e3a15d8565e5d9d8f684c89e` |

## 2. Server communication (Java/SDK layer — `classes*.dex`, 15,657 classes)

The DEX layer is a thin shell (only 5 `com.proxima.dfm.*` classes: downloader
+ receiver, `ev_8b25caef7a7a24394b8192c737e314eb356028222c99ef35601823ecc413d2a4`).
Networking is SDK-driven:

* **INTL SDK** (Level Infinite login/lobby/social): 136 `com.intlgame.*`
  classes — `ev_017e5aa52407ca9963b4c4e8e0456a1970e5d926072aaf0e760c70266709109b`
* **GCloud**: voice engine + Apollo **WSSocket** persistent socket pool
  (`com.gcloudsdk.apollo.WSSocket/P`…) — `ev_456ea75bf4e72b223a397a283bbdbac2e0cbf8c43c44454cc49301d2e84d93b8`,
  `ev_c8fbb3156a403fb438bfe2bf739d5234d6032b48e332d9413e4a81e06eb10457`
* **HTTP(S)**: okhttp3 incl. WebSocket — `ev_64d191cba540972699f8d2863678eee6ff3382fc9bbb9a58d84b6a0c35d30703`
* **HTTPDNS**: `com.onesdk.oversea.httpdns.*` — `ev_e1c9b11018746db89f46806899c5921d7f6fe84766c072a5d31c56a4b3ef4c6e`
* **Telemetry**: TDM/TDataMaster — `ev_deb63054f2fbe4b757738938de3e10f1a196477f82a89b3d874fc469c9c2ac59`
* Manifest declares `android:networkSecurityConfig` and 31 permissions incl.
  `INTERNET`, `CHANGE_NETWORK_STATE`, `ACCESS_WIFI_STATE` —
  `ev_686ce430e41c51781c15ed5e2d450aaf067188399054962694b856baa3ae750d`

**Native bridge (`libINTLGameNative.so`)**: exports `GameNative_Init`,
`GameNative_SetPostCallback`, `GameNative_SetDownloadCallback`, and embeds a
Lua VM (`intlslua` — `luaopen_socket_core`, `luaopen_mime_core`), i.e. a
Lua-scriptable HTTP/download layer into the game — raw `nm -D` observation on
`docs/extracted/libINTLGameNative.so` (sha256
`963caf2ad10d09539a16c34305011fb8069d7c7a4e098e3e8fb7364b3157334f`; stripped
symbol table per `ev_d9a3438419e42803d2fda1d15c61b1bb227e5726edfdaade320e5ed5941804b0`).
`libgcloud.so` carries TGCP client strings (`tgcpapi`, HTTP-upgrade handling).

## 3. Entity management (game layer — `libUE4.so`)

Entity state is **not** in Java; it lives in UE4's replication system inside
`libUE4.so` (actors → channels → property replication, with net-relevancy
culling). Direct static confirmation is blocked by protection:

* **libUE4.so is packed/encrypted.** `.rodata` (40 MB) and `.text` (330 MB)
  contain no readable plaintext (≈4.5k strings ≥6 chars in .rodata; zero hits
  for `UNetDriver`, `UActorChannel`, `ServerMove`, `DeltaForce`) — raw
  observation. File-wide entropy 6.75 (29 runs classified) —
  `ev_d866a8d49d12a8adf522e6c4321c7cfd9e588f06bd2fbcc796754aca3469710e`.
* **Anti-cheat present**: `libanogs.so` / `libanort.so` (Tencent ACE runtime,
  exports `AnoSDKInitEx`) — raw observation of extracted split.
* **Asset paks are AES-encrypted** (pak version 11, non-zero encryption-index
  GUID) — raw observation of `pakchunk19` inside `split_obbassets.apk`
  (`assets/main.obb.png` is a ZIP container of `DeltaForce/Content/...`).
* Hopper-based native analysis was attempted and is blocked by a macOS
  Automation permission (Apple Events to Hopper denied) — grantable in System
  Settings → Privacy & Security → Automation, but the packing above is the
  binding constraint regardless.

### What this means architecturally

For a radar-style read of this game, the meaningful entity feed is UE4's
replicated-actor stream: each entity = an actor with id/class, transform
(x/y/z/yaw), team, health, entering/leaving relevancy. delta-radar's
ingestion schema mirrors exactly that model, and its 5-second drop-out mirrors
relevancy culling.

## 4. delta-radar implementation (this project)

* `server.js` — zero-dep Node server: `POST /api/state` ingestion,
  `GET /api/stream` (SSE), `GET /api/state`, mod pipeline, 5 s relevancy prune.
* `public/` — canvas radar (pan/zoom, touch), entity list, mod panels.
* `mods/server/` — feed-transform mods (hot-reloaded).
* `mods/client/` — drop-in render mods.
* Verified locally: 21 simulated entities streamed over SSE, mod toggle
  `hostiles-only` filtered feed 21 → 17 entities, all endpoints 200.
