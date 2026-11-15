#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <android/dlext.h>
#include <android/log.h>

#include "zygisk.hpp"

using zygisk::Api;

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "zl", __VA_ARGS__)

#ifndef __NR_memfd_create
#define __NR_memfd_create 279
#endif
#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

static char _target_pkg[32];
static void _init_pkg() {
    if (_target_pkg[0]) return;
    static const unsigned char e[] = {
        0x50,0x5c,0x5e,0x1d,0x43,0x41,0x5c,0x4b,0x5a,0x5e,0x52,0x1d,0x57,0x55,0x5e
    };
    volatile unsigned char k = 0x33;
    for (int i = 0; i < 15; i++) _target_pkg[i] = e[i] ^ k;
    _target_pkg[15] = 0;
}

static JavaVM *g_vm = nullptr;

static void *_loader_thread(void *) {
    usleep(200000);

    /* Decode lib path at runtime */
    static const unsigned char lp[] = {
        0x1c,0x57,0x52,0x47,0x52,0x1c,0x57,0x52,0x47,0x52,0x1c,0x50,
        0x5c,0x5e,0x1d,0x43,0x41,0x5c,0x4b,0x5a,0x5e,0x52,0x1d,0x57,
        0x55,0x5e,0x1c,0x5f,0x5a,0x51,0x46,0x56,0x07,0x43,0x52,0x47,
        0x50,0x5b,0x1d,0x40,0x5c
    };
    char lib_path[48];
    volatile unsigned char lk = 0x33;
    for (int i = 0; i < 41; i++) lib_path[i] = lp[i] ^ lk;
    lib_path[41] = 0;

    /* Read library file into memory */
    int fd = open(lib_path, O_RDONLY);
    if (fd < 0) { LOGD("open fail"); return nullptr; }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 64) { close(fd); LOGD("stat fail"); return nullptr; }
    void *buf = mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (buf == MAP_FAILED) { LOGD("mmap fail"); return nullptr; }

    /* Copy into a memfd; retain the source file for subsequent launches. */
    static const unsigned char mn[] = {0x59,0x5a,0x47,0x1e,0x50,0x52,0x50,0x5b,0x56}; /* XOR 0x33 */
    char mfd_name[16];
    volatile unsigned char mk = 0x33;
    for (int i = 0; i < 9; i++) mfd_name[i] = mn[i] ^ mk;
    mfd_name[9] = 0;

    void *h = nullptr;
    int mfd = (int)syscall(__NR_memfd_create, mfd_name, MFD_CLOEXEC);
    if (mfd < 0) {
        munmap(buf, st.st_size);
        LOGD("memfd fail, fallback");
        h = dlopen(lib_path, RTLD_NOW);
        if (!h) { LOGD("dlopen: %s", dlerror()); return nullptr; }
    } else {
        ssize_t written = 0;
        while (written < st.st_size) {
            ssize_t r = write(mfd, (char *)buf + written, st.st_size - written);
            if (r <= 0) break;
            written += r;
        }
        munmap(buf, st.st_size);
        if (written != st.st_size) { close(mfd); LOGD("write fail"); return nullptr; }

        android_dlextinfo ext_info = {};
        ext_info.flags = ANDROID_DLEXT_USE_LIBRARY_FD;
        ext_info.library_fd = mfd;
        h = android_dlopen_ext(mfd_name, RTLD_NOW, &ext_info);
        close(mfd);
        if (!h) { LOGD("memfd dlopen: %s", dlerror()); return nullptr; }
    }

    LOGD("lib loaded");
    typedef jint (*fn_t)(JavaVM *, void *);
    fn_t onload = (fn_t)dlsym(h, "JNI_OnLoad");
    if (onload && g_vm) {
        LOGD("JNI_OnLoad");
        onload(g_vm, (void *)1337);
    }
    return nullptr;
}

class SvcModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        _init_pkg();
        // Magisk 28.1 struct differs from header: nice_name is at pointer offset 6
        auto *raw = reinterpret_cast<uintptr_t *>(args);
        auto name_ref = reinterpret_cast<jstring *>(raw[6]);
        if (name_ref) {
            jstring name_str = *name_ref;
            if (name_str) {
                const char *name = env->GetStringUTFChars(name_str, nullptr);
                if (name) {
                    if (strcmp(name, _target_pkg) == 0) is_target = true;
                    env->ReleaseStringUTFChars(name_str, name);
                }
            }
        }
        if (!is_target) {
            api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
        }
    }

    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!is_target) return;

        env->GetJavaVM(&g_vm);

        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_create(&t, &attr, _loader_thread, nullptr);
        pthread_attr_destroy(&attr);

        LOGD("loader scheduled");
    }

private:
    Api *api = nullptr;
    JNIEnv *env = nullptr;
    bool is_target = false;
};

REGISTER_ZYGISK_MODULE(SvcModule)
