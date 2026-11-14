#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

/* Bounded, external forensic capture. No attach, code writes or signals.
 * The RVA is specific to the SHA-256-verified libanogs analyzed in handoff.md.
 * Captures may contain private SDK data: keep the output directory private. */
static int target;
static ssize_t copy_from(uint64_t address, void *out, size_t size) {
    struct iovec local = {out, size}, remote = {(void *)(uintptr_t)address, size};
    return syscall(__NR_process_vm_readv, target, &local, 1, &remote, 1, 0);
}
static long seconds(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec;
}
static uint64_t library_base(void) {
    char path[128], line[1024];
    snprintf(path, sizeof(path), "/proc/%d/maps", target);
    FILE *f = fopen(path, "r"); if (!f) return 0;
    uint64_t result = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long start, end, offset; char perm[8];
        if (strstr(line, "/libanogs.so") &&
            sscanf(line, "%llx-%llx %7s %llx", &start, &end, perm, &offset) == 4 &&
            offset == 0 && strchr(perm, 'r')) { result = start; break; }
    }
    fclose(f); return result;
}
static int writable_mapping(uint64_t address) {
    char path[128], line[1024];
    snprintf(path, sizeof(path), "/proc/%d/maps", target);
    FILE *f = fopen(path, "r"); if (!f) return 0;
    int result = 0;
    while (fgets(line, sizeof(line), f)) {
        unsigned long long start, end; char perm[8];
        if (sscanf(line, "%llx-%llx %7s", &start, &end, perm) == 3 &&
            address >= start && address < end && end - address >= 256) {
            result = perm[0] == 'r' && perm[1] == 'w'; break;
        }
    }
    fclose(f); return result;
}
static int write_capture(const char *directory, const char *name, const void *data, size_t size) {
    char path[1024];
    if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= (int)sizeof(path)) return -1;
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    size_t done = 0;
    while (done < size) {
        ssize_t n = write(fd, (const char *)data + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return -1; }
        done += (size_t)n;
    }
    return close(fd);
}
static int inspect_thread(int tid, uint64_t base, const char *directory) {
    char path[128], line[1024], name[80];
    snprintf(path, sizeof(path), "/proc/%d/task/%d/comm", target, tid);
    FILE *f = fopen(path, "r"); if (!f) return 0;
    if (!fgets(name, sizeof(name), f)) { fclose(f); return 0; } fclose(f);
    if (strncmp(name, "GameThread", 10)) return 0;
    snprintf(path, sizeof(path), "/proc/%d/task/%d/syscall", target, tid);
    f = fopen(path, "r"); if (!f) return 0;
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; } fclose(f);
    long nr; unsigned long long a[6], sp, pc;
    if (sscanf(line, "%ld %llx %llx %llx %llx %llx %llx %llx %llx",
        &nr, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &sp, &pc) != 9) return 0;
    uint64_t request;
    if (nr == __NR_nanosleep) request = a[0];
    else if (nr == __NR_clock_nanosleep) request = a[2];
    else return 0;
    int64_t duration[2];
    if (copy_from(request, duration, sizeof(duration)) != sizeof(duration) || duration[0] != 10) return 0;
    uint64_t stack[512];
    ssize_t bytes = copy_from(sp, stack, sizeof(stack));
    if (bytes < 8) return 0;
    int marker = -1;
    for (int i = 0; i < bytes / 8; i++) if (stack[i] == base + 0x47e6c8) { marker = i; break; }
    if (marker < 0) return 0;
    char meta[16384]; size_t used = (size_t)snprintf(meta, sizeof(meta),
        "pid=%d tid=%d monotonic=%ld base=0x%llx sp=0x%llx pc=0x%llx sleep=%lld marker_word=%d stack_bytes=%zd\n",
        target, tid, seconds(), (unsigned long long)base, sp, pc,
        (long long)duration[0], marker, bytes);
    if (write_capture(directory, "stack.bin", stack, (size_t)bytes)) return -1;
    uint64_t seen[32]; int count = 0;
    for (int i = 0; i < bytes / 8 && count < 32; i++) {
        uint64_t address = stack[i];
        if ((address & 7) || address < 0x100000000ULL || address >= 0x8000000000ULL ||
            (address >= sp && address - sp < 65536)) continue;
        int duplicate = 0;
        for (int j = 0; j < count; j++) if (seen[j] == address) duplicate = 1;
        if (duplicate || !writable_mapping(address)) continue;
        unsigned char buffer[256];
        if (copy_from(address, buffer, sizeof(buffer)) != sizeof(buffer)) continue;
        char filename[80]; snprintf(filename, sizeof(filename), "object-%02d.bin", count);
        if (write_capture(directory, filename, buffer, sizeof(buffer))) return -1;
        seen[count++] = address;
        if (used < sizeof(meta)) {
            int n = snprintf(meta + used, sizeof(meta) - used,
                "%s address=0x%llx stack_word=%d\n", filename, (unsigned long long)address, i);
            if (n > 0 && (size_t)n < sizeof(meta) - used) used += (size_t)n;
        }
    }
    if (write_capture(directory, "metadata.txt", meta, used)) return -1;
    snprintf(path, sizeof(path), "/proc/%d/status", target);
    f = fopen(path, "r");
    if (f) { while (fgets(line, sizeof(line), f)) if (!strncmp(line, "TracerPid:", 10)) fputs(line, stdout); fclose(f); }
    printf("Captured SDK termination-sleep context: pid=%d tid=%d objects=%d; target execution was not stopped\n", target, tid, count);
    return 1;
}
int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    if (argc != 3 || argv[2][0] != '/') { fprintf(stderr, "usage: %s PID NEW_PRIVATE_OUTPUT_DIRECTORY\n", argv[0]); return 2; }
    char *end; long value = strtol(argv[1], &end, 10);
    if (*end || value <= 0 || value > 2147483647L) return 2;
    target = (int)value;
    if (mkdir(argv[2], 0700)) { perror("private output directory"); return 3; }
    long deadline = seconds() + 160;
    printf("Observing pid=%d for <=160 seconds; external checked reads only\n", target);
    while (seconds() < deadline) {
        uint64_t base = library_base();
        if (base) {
            unsigned char header[4];
            if (copy_from(base, header, sizeof(header)) != sizeof(header)) {
                perror("checked cross-process read"); return 7;
            }
        }
        char path[128]; snprintf(path, sizeof(path), "/proc/%d/task", target);
        DIR *d = opendir(path); if (!d) { puts("Target exited before capture"); return 4; }
        struct dirent *entry;
        while ((entry = readdir(d))) {
            int tid = atoi(entry->d_name); if (tid <= 0 || !base) continue;
            int result = inspect_thread(tid, base, argv[2]);
            if (result) { closedir(d); return result > 0 ? 0 : 5; }
        }
        closedir(d); usleep(100000);
    }
    puts("No matching context captured within the observation window"); return 6;
}
