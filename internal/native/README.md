# Existing internal library artifacts

This folder contains the existing library sources, build script, and shared
objects previously stored under the root `scanner/` directory. Launch3r now
lives at `../launcher/`.

`delta_radar_lib.c` includes `../../external/scanner/memscan-legacy.c` and
therefore still depends on the external reader and its adjacent headers.
The include path was adjusted only to preserve the relationship after moving
the files. No initialization, memory access, or rendering behavior was changed.

The existing binaries have not been rebuilt or tested against the current game.
Their filenames alone do not establish which source revision produced them.
