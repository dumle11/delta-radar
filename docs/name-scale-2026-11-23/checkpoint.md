# Actor label scaling and font readability — 2026-11-23

The user requested labels that shrink with distant actors, then reported that the
first8px minimum was difficult to read on the device. Current actor names use
`clamp(projected_actor_height * 0.20,12,24)` in UI pixels. Distance annotations use
`clamp(projected_actor_height * 0.17,12,20)`. Labels are horizontally centered on
the actor and keep their visibility colors. Loot/debug/menu text is independent.

`O.ta` uses dedicated bold sans-serif paints, antialiasing/hinting, pixel-aligned
baselines and a1.5–2.5px black outline, then the colored fill. The12px minimum
avoids reducing characters to about5px in the720p swapchain, as the initial8px
minimum did. Shared text/menu paint is not mutated. The JNI method signature is
`(Ljava/lang/String;FFIF)V`, with fallback to the existing text method if absent.

Built overlay DEX and arm64 native library; whitespace check passed. Latest
installed SHA256, matching local/device:
`3e39e9547783ba66d3fac5e8fda047d9d27a39b9a63a96b01474ff1840919829`
296152 bytes. PID23792 reached the range successfully. The final
`outlined-font-range.png` capture shows bold colored labels at multiple actor
sizes, including the smaller distant B7 label and larger nearby labels. User
readability feedback remains pending. Before captures are in this folder. The earlier fixed24px labels
cluttered distant target rows; the subsequent8px labels were too thin/small.

This is a narrow label change atop the current rendering implementation. It does
not establish completion of the earlier exact-matrix projection work or change
its validation status. Other agents have edited/built this shared workspace;
preserve their rendering changes. Announce intentional stops/restarts first.
