#include <cerrno>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <elf.h>
#include <android/dlext.h>
#include <android/log.h>
#include <sys/system_properties.h>
#include "zygisk.hpp"

#define LOGD(...) __android_log_print(ANDROID_LOG_INFO, "DeltaZygisk", __VA_ARGS__)
static JavaVM *g_vm;
static void *g_payload;
static size_t g_size;

/* Read before specialization: module files need not be accessible to the app
 * UID or its linker namespace. Anonymous memory survives specialization without
 * carrying any root/module-directory descriptor into the application. */
static bool read_payload(int directory) {
    int fd = openat(directory, "payload/arm64-v8a.so", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { LOGD("payload open: %s", strerror(errno)); return false; }
    struct stat st{};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < (off_t)sizeof(Elf64_Ehdr) || st.st_size > 64*1024*1024) {
        close(fd); LOGD("invalid payload size/type"); return false;
    }
    size_t size = (size_t)st.st_size;
    void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data == MAP_FAILED) { close(fd); return false; }
    size_t done = 0;
    while (done < size) {
        ssize_t n = read(fd, (char*)data + done, size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += (size_t)n;
    }
    close(fd);
    const auto *eh = (const Elf64_Ehdr*)data;
    if (done != size || memcmp(eh->e_ident, ELFMAG, SELFMAG) || eh->e_ident[EI_CLASS] != ELFCLASS64 ||
        eh->e_ident[EI_DATA] != ELFDATA2LSB || eh->e_machine != EM_AARCH64 || eh->e_type != ET_DYN) {
        munmap(data, size); LOGD("invalid ARM64 payload"); return false;
    }
    g_payload = data; g_size = size; return true;
}

static void *loader_thread(void *) {
    int fd = (int)syscall(__NR_memfd_create, "delta-payload", 1U /* MFD_CLOEXEC */);
    if (fd < 0) { LOGD("memfd: %s", strerror(errno)); munmap(g_payload,g_size); return nullptr; }
    size_t done = 0;
    while (done < g_size) {
        ssize_t n = write(fd, (char*)g_payload + done, g_size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += (size_t)n;
    }
    munmap(g_payload, g_size); g_payload = nullptr;
    if (done != g_size) { LOGD("payload write failed"); close(fd); return nullptr; }
    android_dlextinfo ext{};
    ext.flags = ANDROID_DLEXT_USE_LIBRARY_FD; ext.library_fd = fd;
    void *handle = android_dlopen_ext("delta-payload.so", RTLD_NOW | RTLD_LOCAL, &ext);
    close(fd);
    if (!handle) { LOGD("payload load: %s", dlerror()); return nullptr; }
    auto onload = (jint (*)(JavaVM*, void*))dlsym(handle, "JNI_OnLoad");
    if (!onload) { LOGD("missing JNI_OnLoad"); dlclose(handle); return nullptr; }
    jint result = onload(g_vm, nullptr);
    LOGD("payload initialized: JNI=%x bytes=%zu", result, g_size);
    /* Payload owns running threads/hooks, so retain its handle for process life. */
    return nullptr;
}

class SvcModule : public zygisk::ModuleBase {
    zygisk::Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool ready = false;
public:
    void onLoad(zygisk::Api *a, JNIEnv *e) override { api=a; env=e; }
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        // Use the unmodified official API 2 layout, valid on newer providers too.
        const char *name = args->nice_name ? env->GetStringUTFChars(args->nice_name, nullptr) : nullptr;
        bool target = name && strcmp(name,"com.proxima.dfm") == 0;
        if (name) env->ReleaseStringUTFChars(args->nice_name,name);
        if (target && env->GetJavaVM(&g_vm) == JNI_OK) {
            int dir = api->getModuleDir();
            if (dir >= 0) { ready=read_payload(dir); close(dir); }
            else LOGD("module directory unavailable");
        }
        if (!ready) api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!ready) return;
        char sdk[PROP_VALUE_MAX]{}; __system_property_get("ro.build.version.sdk",sdk);
        LOGD("target pid=%d SDK=%s API=%d",getpid(),sdk,ZYGISK_API_VERSION);
        pthread_t thread;
        int error=pthread_create(&thread,nullptr,loader_thread,nullptr);
        if (!error) pthread_detach(thread);
        else { LOGD("pthread_create: %s",strerror(error)); munmap(g_payload,g_size); }
    }
};
REGISTER_ZYGISK_MODULE(SvcModule)
