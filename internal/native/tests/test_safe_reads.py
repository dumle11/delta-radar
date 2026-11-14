"""Exercise production checked-read helpers without installing signal handlers."""
from pathlib import Path
import argparse
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / "external/scanner/memscan-legacy.c").read_text()


def function(signature):
    start = source.index(signature + " {")
    return source[start:source.index("\n}", start) + 2]


program = r'''
#define BUILD_AS_LIBRARY 1
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "self-memory.h"
static SelfMemoryReader checked_reader = {-1, -1};
static unsigned diagnostic_read_failures;
static uint64_t diagnostic_failure_address;
static int diagnostic_failure_errno;
'''
for signature in (
    "static ssize_t safe_read_mem(uint64_t addr, void *buf, size_t len)",
    "static uint64_t safe_read_ptr(uint64_t addr)",
    "static ssize_t read_mem(uint64_t addr, void *buf, size_t len)",
):
    program += function(signature) + "\n"
program += r'''
int main(void) {
    alarm(10);
    const int signals[] = {SIGSEGV, SIGBUS, SIGABRT};
    struct sigaction before[3], after[3];
    for (int i=0; i<3; i++) assert(sigaction(signals[i], 0, &before[i]) == 0);
    uint64_t good = 12345, out = 0;
    assert(self_memory_open(&checked_reader) == 0);
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    void *mapping = mmap(0, page*2, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert(mapping != MAP_FAILED);
    assert(mprotect((char *)mapping+page, page, PROT_NONE) == 0);
    uint64_t valid = (uintptr_t)&good;
    uint64_t invalid = (uintptr_t)mapping + page;
    uint64_t boundary = invalid - 4;
    for (int i=0; i<100; i++) {
        assert(safe_read_mem(invalid, &out, sizeof(out)) == -1);
        assert(safe_read_ptr(invalid) == 0);
        assert(read_mem(invalid, &out, sizeof(out)) == -1);
        assert(safe_read_ptr(valid) == good);
        assert(read_mem(valid, &out, sizeof(out)) == sizeof(out) && out == good);
    }
    assert(diagnostic_read_failures == 100);
    SelfMemoryReader closed_reader = {-1, -1};
    assert(self_memory_read(&checked_reader, boundary, &out, sizeof(out)) == -1 && errno == EFAULT);
    assert(self_memory_read(&closed_reader, valid, &out, sizeof(out)) == -1 && errno == EBADF);
    assert(self_memory_read(&checked_reader, UINT64_MAX, &out, 8) == -1 && errno == EOVERFLOW);
    assert(self_memory_read(&checked_reader, valid, 0, 8) == -1 && errno == EINVAL);
    assert(self_memory_read(&checked_reader, invalid, 0, 0) == 0);
    assert(read_mem(valid, &out, sizeof(out)) == sizeof(out) && out == good);
    unsigned char large[32768], copied[32768];
    for (size_t i=0; i<sizeof(large); i++) large[i]=(unsigned char)i;
    assert(self_memory_read(&checked_reader, (uintptr_t)large, copied, sizeof(copied)) == sizeof(copied));
    assert(!memcmp(large, copied, sizeof(copied)));
    assert(munmap((char *)mapping+page, page) == 0);
    assert(self_memory_read(&checked_reader, invalid, &out, sizeof(out)) == -1);
    assert(read_mem(valid, &out, sizeof(out)) == sizeof(out) && out == good);
    for (int i=0; i<3; i++) {
        assert(sigaction(signals[i], 0, &after[i]) == 0);
        assert(before[i].sa_sigaction == after[i].sa_sigaction);
        assert(before[i].sa_flags == after[i].sa_flags);
        for (int sig=1; sig<NSIG; sig++)
            assert(sigismember(&before[i].sa_mask,sig) == sigismember(&after[i].sa_mask,sig));
    }
    munmap(mapping, page);
    self_memory_close(&checked_reader);
    puts("PASS: 300 rejected reads, valid reads, partial/overflow rejection, unchanged host signals");
    return 0;
}
'''
parser = argparse.ArgumentParser()
parser.add_argument("--emit-c", type=Path)
args = parser.parse_args()
if args.emit_c:
    args.emit_c.write_text(program)
else:
    with tempfile.TemporaryDirectory() as directory:
        cfile = Path(directory) / "reads.c"
        executable = Path(directory) / "reads"
        cfile.write_text(program)
        subprocess.run(["cc", "-O2", "-I", str(root / "external/scanner"), str(cfile), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=15)
