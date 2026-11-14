#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <android/log.h>

#include "zygisk.hpp"

using zygisk::Api;

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "zl", __VA_ARGS__)

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
    /* Brief delay for app framework init, but not so long that ACE reads identity first */
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

    void *h = dlopen(lib_path, RTLD_NOW);
    if (!h) { LOGD("dlopen: %s", dlerror()); return nullptr; }
    LOGD("lib loaded");

    typedef jint (*fn_t)(JavaVM *, void *);
    fn_t onload = (fn_t)dlsym(h, "JNI_OnLoad");
    if (onload && g_vm) {
        LOGD("JNI_OnLoad");
        onload(g_vm, (void *)1337);
    }
    return nullptr;
}

class DeltaModule : public zygisk::ModuleBase {
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

REGISTER_ZYGISK_MODULE(DeltaModule)
