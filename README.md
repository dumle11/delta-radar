# Delta Radar workspace

The workspace separates the external Android project, internal experiments, and
versioned SDK archives.

| Path | Contents |
| --- | --- |
| `external/android/` | Existing Delta Radar Android app |
| `external/scanner/` | External reader, touch bridge, headers, and existing tests |
| `internal/launcher/` | dMe Launch3r Android project |
| `internal/native/` | Existing internal library sources, build script, and binaries |
| `dumps/<version>/<capture>/` | SDK output and original archive for a recorded game version |
| `docs/` | Historical investigation notes and evidence |

The existing external project notes are in [external/README.md](external/README.md).
The internal architecture notes are in [internal/README.md](internal/README.md).
Archive provenance is recorded in [dumps/README.md](dumps/README.md).

## Reorganization status

On 2026-11-12 the existing files were moved into this layout, preserving local
edits and build artifacts. The native library's relative source include was
adjusted to its new location; it still shares the existing external reader
implementation. This is a folder separation, not an independent implementation.

No new device dump was captured, no offsets were refreshed, and no crash or
entity-detection fix was made. The archived SDK is version 1.202.37117.5601
from 2026-11-02, not a verified SDK for the current game release. Existing native
binaries were moved without rebuilding and do not certify source compatibility
or runtime stability. Historical notes retain paths from before the move.
