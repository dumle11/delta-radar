# Feature decisions

Current branch, accepted build, and remaining work are recorded in
[CURRENT_STATUS.md](CURRENT_STATUS.md).

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
