# Decoder dependency

Vendored from https://github.com/libtersafe/dfm_android_unicorn at commit
`646c770c67d1af9bb9a5e39eb8fd9cc60e6b6eb0` (GPL-2.0; see LICENSE).
The upstream `jni` directory supplied the source, Unicorn headers and ARM64
static libraries. Preserve the license and upstream attribution when distributing.
The complete upstream source is available at the pinned commit above.

Local changes: process_vm_readv only; removed ptrace/TLS acquisition and proc-mem
fallback; simulate exclusive stores, process/thread IDs and counter reads only
inside Unicorn; refresh every lazily mapped page including tagged aliases; fail
closed on missing remote pages. No target writes or target thread attachment.
`scanner/decode_bridge.cpp` invokes the matching SDK transform getter and validates
its returned transform, including an already-decoded remote-cache return.

The SDK getter offset is specific to the bundled game build. A successful build
is not evidence that it matches a future game update. The native build manifest
records hashes of all source files, headers and static libraries.
