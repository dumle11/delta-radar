#ifndef DELTA_SELF_MEMORY_H
#define DELTA_SELF_MEMORY_H

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct { int read_fd, write_fd; } SelfMemoryReader;

static void self_memory_close(SelfMemoryReader *reader) {
    if (reader->read_fd >= 0) close(reader->read_fd);
    if (reader->write_fd >= 0) close(reader->write_fd);
    reader->read_fd = reader->write_fd = -1;
}

/* Use normal kernel copy_from_user/copy_to_user via a private nonblocking pipe.
 * Unlike dereferencing a stale pointer, write() returns EFAULT without invoking
 * the application's signal handlers. No /proc or ptrace permission is required.
 * Each producer thread must own its reader; do not share a pipe between threads. */
static int self_memory_open(SelfMemoryReader *reader) {
    if (reader->read_fd >= 0 && reader->write_fd >= 0) return 0;
    int pair[2];
    if (pipe(pair) < 0) return -1;
    for (int i = 0; i < 2; i++) {
        int flags = fcntl(pair[i], F_GETFL);
        if (flags < 0 || fcntl(pair[i], F_SETFL, flags | O_NONBLOCK) < 0 ||
            fcntl(pair[i], F_SETFD, FD_CLOEXEC) < 0) {
            int saved = errno; close(pair[0]); close(pair[1]); errno = saved; return -1;
        }
    }
    reader->read_fd = pair[0]; reader->write_fd = pair[1];
    return 0;
}

static void self_memory_drain(SelfMemoryReader *reader) {
    char discarded[256];
    for (;;) {
        ssize_t count = read(reader->read_fd, discarded, sizeof(discarded));
        if (count > 0 || (count < 0 && errno == EINTR)) continue;
        break;
    }
}

static ssize_t self_memory_read(SelfMemoryReader *reader, uint64_t address, void *out, size_t size) {
    if (reader->read_fd < 0 || reader->write_fd < 0) { errno = EBADF; return -1; }
    if (size == 0) return 0;
    if (!out) { errno = EINVAL; return -1; }
    if (size > SSIZE_MAX || address > UINTPTR_MAX || size > UINTPTR_MAX - address) {
        errno = EOVERFLOW; return -1;
    }
    for (size_t offset = 0; offset < size;) {
        size_t chunk = size - offset;
        if (chunk > PIPE_BUF) chunk = PIPE_BUF;
        ssize_t copied;
        do { copied = write(reader->write_fd, (const void *)(uintptr_t)(address + offset), chunk); }
        while (copied < 0 && errno == EINTR);
        if (copied != (ssize_t)chunk) {
            int saved = copied < 0 ? errno : EFAULT;
            self_memory_drain(reader); errno = saved; return -1;
        }
        ssize_t received;
        do { received = read(reader->read_fd, (char *)out + offset, chunk); }
        while (received < 0 && errno == EINTR);
        if (received != (ssize_t)chunk) {
            int saved = received < 0 ? errno : EIO;
            self_memory_drain(reader); errno = saved; return -1;
        }
        offset += chunk;
    }
    return (ssize_t)size;
}

#endif
