#ifndef DELTA_CAMERA_STREAM_H
#define DELTA_CAMERA_STREAM_H
#ifndef CAMERA_STREAM_SOCKET_NAME
#define CAMERA_STREAM_SOCKET_NAME "delta_radar_camera"
#endif

/* This sampler owns its state and reads via process_vm_readv only. Publishing
 * a source under a mutex prevents mixed process/world/cache identities. */
typedef struct { int pid; uint64_t world, cache, generation; } CameraSource;
static pthread_mutex_t camera_source_lock = PTHREAD_MUTEX_INITIALIZER;
static CameraSource camera_source;
static pthread_t camera_thread;
static int camera_thread_started;
static void camera_stream_publish(int pid, uint64_t world, uint64_t cache) {
    pthread_mutex_lock(&camera_source_lock);
    if (camera_source.pid != pid || camera_source.world != world || camera_source.cache != cache) {
        camera_source.pid = pid; camera_source.world = world; camera_source.cache = cache;
        camera_source.generation++;
    }
    pthread_mutex_unlock(&camera_source_lock);
}
static CameraSource camera_stream_source(void) {
    pthread_mutex_lock(&camera_source_lock);
    CameraSource copy = camera_source;
    pthread_mutex_unlock(&camera_source_lock);
    return copy;
}
static int camera_stream_read(int pid, uint64_t address, void *out, size_t size) {
    struct iovec local = {.iov_base=out, .iov_len=size};
    struct iovec remote = {.iov_base=(void *)address, .iov_len=size};
    return syscall(__NR_process_vm_readv, pid, &local, 1, &remote, 1, 0) == (ssize_t)size;
}
static int camera_stream_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address = {.sun_family=AF_UNIX};
    memcpy(address.sun_path + 1, CAMERA_STREAM_SOCKET_NAME, strlen(CAMERA_STREAM_SOCKET_NAME));
    struct timeval timeout = {.tv_sec=0, .tv_usec=20000};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, (struct sockaddr *)&address,
                offsetof(struct sockaddr_un, sun_path) + 1 + strlen(CAMERA_STREAM_SOCKET_NAME)) < 0) {
        close(fd); return -1;
    }
    return fd;
}
static void *camera_stream_worker(void *unused) {
    (void)unused;
    int fd = -1;
    float previous = -1;
    uint64_t generation = 0;
    long long changed_at = 0, retry_at = 0, last_sent = -8;
    while (session_active()) {
        long long now = sample_time_ms();
        CameraSource source = camera_stream_source();
        if (source.generation != generation) {
            generation = source.generation; previous = -1; changed_at = 0;
        }
        if (fd < 0 && now >= retry_at) { fd = camera_stream_connect(); retry_at = now + 250; }
        unsigned char a[48], b[48];
        float stamp = 0, xyz[3] = {0}, angles[3] = {0}, fov = 0;
        int valid = 0;
        if (source.pid > 0 && source.world && source.cache &&
            camera_stream_read(source.pid, source.cache, a, sizeof(a)) &&
            camera_stream_read(source.pid, source.cache, b, sizeof(b)) && !memcmp(a,b,sizeof(a)) &&
            camera_stream_source().generation == generation) {
            memcpy(&stamp,a,4); memcpy(xyz,a+16,12); memcpy(angles,a+32,12); memcpy(&fov,a+44,4);
            valid = enc_handler_unmanaged(a+28) && isfinite(stamp) && stamp > 0 &&
                isfinite(fov) && fov >= 5 && fov <= 150;
            for (int i=0;i<3;i++)
                valid = valid && isfinite(xyz[i]) && fabsf(xyz[i]) < 10000000 && isfinite(angles[i]);
            if (valid && stamp != previous) { previous=stamp; changed_at=now; }
            valid = valid && changed_at > 0 && now-changed_at <= 100;
        }
        if (fd >= 0 && now-last_sent >= (valid ? 8 : 250)) {
            char packet[512];
            /* Invalid samples carry finite zeroes so the JSON record is valid. */
            if (!valid) { stamp=0; fov=0; memset(xyz,0,sizeof(xyz)); memset(angles,0,sizeof(angles)); }
            int n = snprintf(packet,sizeof(packet),
                "{\"gamePid\":%d,\"world\":\"0x%llx\",\"sampledElapsedMs\":%lld,\"timestamp\":%.9g,"
                "\"valid\":%s,\"x\":%.6f,\"y\":%.6f,\"z\":%.6f,\"pitch\":%.6f,\"yaw\":%.6f,\"roll\":%.6f,\"fov\":%.6f}\n",
                source.pid,(unsigned long long)source.world,valid?changed_at:now,(double)stamp,
                valid?"true":"false",xyz[0]/100,xyz[1]/100,xyz[2]/100,angles[0],angles[1],angles[2],fov);
            if (n <= 0 || n >= (int)sizeof(packet) || send(fd,packet,(size_t)n,MSG_NOSIGNAL) != n) {
                close(fd); fd=-1;
            }
            last_sent=now;
        }
        usleep(4000);
    }
    if (fd >= 0) close(fd);
    return NULL;
}
static void camera_stream_start(void) {
    if (!camera_thread_started && !pthread_create(&camera_thread,NULL,camera_stream_worker,NULL))
        camera_thread_started=1;
}
static void camera_stream_stop(void) {
    if (camera_thread_started) { pthread_join(camera_thread,NULL); camera_thread_started=0; }
}
#endif
