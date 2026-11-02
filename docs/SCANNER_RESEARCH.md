# MobileDumper-7 source assessment — 2026-11-02

Source-only review at upstream revision
`595655004a8e1d56fd0e708fe64649f1864d7066` (2026-10-28).
No build, deployment, injection, suspension, or execution of this project was
performed. Read-only reference copies are in ignored local evidence.

## Confirmed from source

- [DeltaForceProfile](https://github.com/MJx0/MobileDumper-7/blob/595655004a8e1d56fd0e708fe64649f1864d7066/Dumper/Profile/CustomProfiles/Shared/DeltaForce.h)
  lists `com.proxima.dfm`, Garena and Tencent package variants. The Android
  entry point actually registers that profile and selects it by package string.
- The profile overrides UTF-8 and UTF-16 **name-string decoding**. It does not
  supply a current game-version-specific GWorld, roster, camera, actor,
  transform, health, team, or FName pool layout table.
- [IProfile](https://github.com/MJx0/MobileDumper-7/blob/595655004a8e1d56fd0e708fe64649f1864d7066/Dumper/Profile/IProfile.h)
  uses generic symbol lookup and runtime layout detection. Package matching in
  the Android entry point does not gate this profile on version 1.202.37117.5601.
- [Name layout detection](https://github.com/MJx0/MobileDumper-7/blob/595655004a8e1d56fd0e708fe64649f1864d7066/Dumper/Engine/OffsetFinder/Layouts.cpp)
  can apply profile callbacks when checking readable names and the `None`
  sentinel. These are runtime detection procedures, not prevalidated constants
  that can simply be copied into our scanner.
- The [Android entry point](https://github.com/MJx0/MobileDumper-7/blob/595655004a8e1d56fd0e708fe64649f1864d7066/Dumper/main_android.cpp)
  supports standalone and injected modes. Its optional suspension path sends
  stop/continue signals; its library mode automatically schedules a dump.
  Those are not authorized actions for this investigation and were not used.

## Relevance to this scanner

Our `resolve_fname` assumes a fixed block-bit layout, stride/header options and
printable plaintext ASCII. Startup initially looks for plaintext `None` before
accepting a pool. Therefore the upstream profile gives a concrete reason to
question that **plaintext-name assumption**. It does not show that the profile's
decoding applies to our exact installed build, nor that our pool base, field
layout or player transforms are correct.

The current scanner's Garena-versus-Proxima offset comments, fixed object
headers, heap-address range, roster back-references and transform offsets still
need independent live validation. Unknown names and underground markers must
not be treated as the same proven bug.

## Decision

Keep this as a research lead. Do not run the dumper or import decoding routines
into the startup/lifecycle patch. First establish our own scanner's identity,
root/read access, fresh socket frames, actual overlay lifecycle, and correlated
source-versus-screen evidence. Any later engine/name work requires an explicit,
limited hypothesis and validation against this installed build.
