# Reset-command history audit — 2026-11-14

The user supplied past shell history including repeated `reset_account.sh` runs,
manual clearing of `com.google.android.gsf` and `com.google.android.gms`, native
injection, module reinstall/removal, external game-config deletion, and a payload
copy with an old hardcoded application UID. None of these commands was rerun in
this audit. No Google account, app data, identity, or service settings were reset.

## Concrete source problems

- `reset_account.sh` clears the game's private and external data, changes global
  and per-app Android IDs, changes runtime device properties, and installs a boot
  override. It is not a narrow cache reset. The boot override was already backed
  up and disabled during the earlier investigation; its removal did not fix the
  crash.
- Its direct SSAID XML edit hardcodes key `10142`. That does not identify the
  current game UID `10556`. Editing a live settings file also bypasses Android's
  settings service. Do not attempt a second identity rewrite as a repair.
- It deletes a GSF registration row and journal, and deletes selected Play
  services database files, without first establishing that the services released
  those databases. These operations can disrupt service state. Some operations
  suppress failures, so the script's completion message does not establish that
  every requested operation succeeded. `sqlite3` was not found on the current
  device PATH; its availability during the historical runs is unknown.
- The historical payload-copy command hardcodes UID `u0_a552` and its SELinux
  categories. These are stale after reinstall. Current game-private payload was
  already absent in the earlier post-reinstall check; that old command is not
  evidence of an active wrong-owner payload in the current install.

Google documents that clearing Play services data can require signing back into
Google accounts: https://support.google.com/googleplay/answer/9037938 . This is a
credible explanation for the reported sign-outs. The user has now confirmed
signing back into one account to reinstall the game; the other accounts were not
all reauthenticated. Sign-out is not itself proof of the native crash trigger.

## Current read-only checks

- GSF, Play services and Play Store are installed and enabled for primary user 0.
  Google app data roots have the expected shared UID / privileged-app labels.
- Both downloaded GSF database snapshots (`gservices.db`, `googlesettings.db`)
  return `ok` from host SQLite read-only `PRAGMA quick_check`. Source SHA-256 was
  stable across each capture and matches the decoded copy. The GSF registration
  ID row exists; its value was not printed. These are snapshot checks, not a
  complete transaction-consistent backup or validation of every Google database.
- Current SSAID XML parses; exactly one game entry uses UID 10556 and has matching
  value/default fields. The old hardcoded key 10142 is not present. No IDs were
  changed or disclosed.
- Current Wi-Fi network reports INTERNET and VALIDATED. Automatic date/time and
  time zone are enabled; data partition has about 28 GiB free.
- An initial bounded log snapshot contains 185 `BadAuthentication` lines across
  Play services, Gmail and Calendar. These include recoverable sign-in errors.
  Their association with accounts not signed back in is plausible, not proven.
  No matching SQLite corruption/cannot-open/database-lock errors were found in
  that snapshot. Package enabled state and database readability do not establish
  that every Google account is authenticated.

Private raw logs, account-service snapshot, database snapshots and screenshots
are under `/tmp/delta-google-health-20261114/` (directory 0700, files 0600).
These may contain account/session data and must not be committed or shared.
Only this sanitized report belongs in the repository.

## Fresh game observation

The first standard launch created PID 7446 while the phone display was off and
Android was dozing. A roughly 90-second process-survival observation in that
state is not a valid gameplay stability test. After waking the phone normally,
the display showed the game splash and then resource loading. Foreground capture
was started separately. No injector, debugger, SDK patch or concealment change
was used. The same PID reached the main menu (screenshot at approximately 33 seconds of
foreground observation), then died at **18:08:12**, before Firing Range was
entered. It was alive at observation 40.4 seconds and absent at 45.6 seconds.
Android records `am_proc_died`; this capture has no Fatal signal, Java exception,
ANR, matching low-memory-killer event, or Google authentication/SSL exception.
DropBox did not show a corresponding app-crash entry in the inspected interval.
The kernel's SIGCHLD notification to zygote is a process-death notification, not
proof of the initiating failure. Do not identify this latest exit as SIGBUS or
attribute it to the previously captured SDK fault without additional evidence.
See `normal-startup-180812-exit.log`.
