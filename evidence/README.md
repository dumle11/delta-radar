# Local evidence

`local/` is ignored by Git. It contains screenshots/player identifiers, raw
frame captures, logs and device metadata; do not force-add it to commits.
Generated binaries stay under ignored `scanner/build/` or temporary app build
trees. Public-safe findings and reviewed identities belong in `docs/`.

The preserved baseline evidence is under `local/2026-11-02-baseline/`; the
pre-milestone uncommitted source checkpoint is under
`local/pre-scanner-milestone/`. Current deployment and device evidence belongs
under `local/scanner-milestone/`.
