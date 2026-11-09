# Feature decisions

Current branch, accepted build, and remaining work are recorded in
[CURRENT_STATUS.md](CURRENT_STATUS.md).

## Requested — visible enemy fill, 2026-11-04

Red fill should cover only exposed enemy surfaces and work alongside boxes or
skeleton. Offline SDK investigation found an effect/second-material-pass lead,
but no verified visible-pixel source or working fill implementation. See
[the investigation and coordinated test plan](ENEMY_VISIBLE_FILL.md).
The user offered match assistance for 2026-11-05. No live diagnostics or
deployment were performed for this request.

## Implemented candidate — container navigation, 2026-11-05

Independent yellow lines, boxes, names and distance to detected containers,
including unopened containers with unknown contents. Works with ordinary loot
OFF and value filtering ON. Android-only; uses the existing scanner feed.
See [container guide behavior and verification limits](CONTAINER_GUIDE.md).
Built locally; installation and unfamiliar-map coverage remain unverified.

## Dismissed — 2026-11-04

- **Auto-loot / Take best loot**, including automatic backpack replacement:
  cancelled by the user. Remove pickup-only research/planning code from active
  source. Do not resume the diagnostic, button, input or replacement work.
- **Enhanced aim assist:** cancelled by the user. No implementation had started.

The user reported a suspension notice containing only “account suspended” and
their UID, with no reason, duration or error code. They suspect the auto-loot
work; the cause remains unconfirmed. No automatic looting or game input ran in
the diagnostic check. Radar lines/boxes and ordinary loot markers remain the
requested scope. Radar and scanner were stopped at that event. The user later
resumed manual testing and accepted the installed APK. Automated game-memory
diagnostics have not resumed; cancelled features remain cancelled.

Historical pickup findings and suspension evidence are retained, not an active
backlog. Do not infer that removing these unused features establishes account
safety for the remaining memory reader/overlay.
