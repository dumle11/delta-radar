# Decoder dependency

Vendored from https://github.com/libtersafe/dfm_android_unicorn at commit
`646c770c67d1af9bb9a5e39eb8fd9cc60e6b6eb0` (GPL-2.0; see LICENSE).
The upstream `jni` directory supplied the source, Unicorn headers and ARM64
static libraries. Preserve the license and upstream attribution when distributing.
The complete upstream source is available at the pinned commit above.

Local vendor changes: process_vm_readv only; removed ptrace/TLS acquisition and
unrestricted proc-mem fallback; simulate exclusive stores, process/thread IDs and counter reads only
inside Unicorn; refresh every lazily mapped page including tagged aliases; fail
closed on missing remote pages. No target writes or target thread attachment.
`scanner/decode_bridge.cpp` invokes the complete matching SDK native getter,
including nested FEncVector translation decoding. Its reader permits bounded
read-only proc-mem access exclusively inside the SDK decoder global slot's
write-only .bss VMA (at most 256 KiB per emulated call). Other reads use
process_vm_readv. The bridge never writes target memory or attaches a thread.

The SDK getter offset is specific to the bundled game build. A successful build
is not evidence that it matches a future game update. The native build manifest
records hashes of all source files, headers and static libraries.
