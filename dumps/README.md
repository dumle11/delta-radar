# SDK archives

Store captures at `<game-version>/<capture-timestamp>/` so captures from different
releases or sessions do not overwrite one another. Keep the SDK output, dumper
log, and original archive together.

## Existing capture

`1.202.37117.5601/20261102-152210/` contains the files previously stored at the
workspace root:

- `CppSDK/`, `Dumpspace/`, `IDAMappings/`, and `Mappings/`
- `GObjects-Dump.txt` and `GObjects-Dump-WithProperties.txt`
- `MobileDumper-7.log`
- `com.proxima.dfm_v1.202.37117.5601_20261102-152210.zip`

The version and capture timestamp come from the existing archive filename.
The timestamp's timezone has not been established. The loose output was moved
alongside that archive without regenerating or certifying its contents.
This archive has not been verified against the current installed game version.

The dumper checkout remains at `/Users/dme/dev/MobileDumper-7`; it was not
modified or executed during the workspace reorganization.
