#include <jni.h>
#include <unistd.h>
#include <android/log.h>

/* A load-only control. No fault handlers, threads, game-memory access or hooks. */
__attribute__((constructor))
static void probe_loaded(void) {
    __android_log_print(ANDROID_LOG_INFO, "DeltaLoaderProbe",
                       "probe-v1 loaded pid=%d", getpid());
}

__attribute__((visibility("default")))
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    __android_log_print(ANDROID_LOG_INFO, "DeltaLoaderProbe",
                       "probe-v1 JNI_OnLoad pid=%d vm_present=%d", getpid(), vm != NULL);
    return JNI_VERSION_1_6;
}
