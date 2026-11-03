#define _GNU_SOURCE
#define __ANDROID_API__ 31
#define main scanner_program_main
#include "../memscan.c"
#undef main
#include <assert.h>

static long long boot_ms(void) {
    struct timespec ts; assert(clock_gettime(CLOCK_BOOTTIME, &ts) == 0);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(void) {
    int pair[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    persistent_sock = pair[0];
    num_entities = 1;
    strcpy(self_entity_id, "TEST-camera");
    strcpy(entities[0].id, self_entity_id);
    strcpy(entities[0].kind, "player");
    entities[0].fov = 80;
    long long before = boot_ms();
    assert(post_to_radar() == 0);
    char buffer[8192] = {0}; size_t length = 0;
    do {
        ssize_t n = read(pair[1], buffer + length, sizeof(buffer) - length - 1);
        assert(n > 0); length += (size_t)n;
        assert(length < sizeof(buffer) - 1);
    } while (buffer[length - 1] != '\n');
    const char *field = strstr(buffer, "\"capturedElapsedMs\":"); assert(field);
    long long captured = strtoll(field + strlen("\"capturedElapsedMs\":"), NULL, 10);
    assert(captured >= before && captured <= boot_ms());
    assert(strstr(buffer, "\"self\":\"TEST-camera\""));
    assert(strstr(buffer, "\"entities\":[{\"id\":\"TEST-camera\""));
    assert(length >= 3 && !strcmp(buffer + length - 3, "]}\n"));
    close_radar_sock(); close(pair[1]);
    puts("Native feed includes a boot-clock capture timestamp and complete camera frame");
}
