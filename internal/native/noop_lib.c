#include <jni.h>
#include <signal.h>

static void _noop_handler(int sig, siginfo_t *info, void *ctx) {
    (void)ctx;
    if (info && (info->si_code <= 0))
        return;
}

__attribute__((visibility("default")))
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)vm; (void)reserved;
    struct sigaction sa;
    __builtin_memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = _noop_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGBUS, &sa, 0);
    sigaction(SIGABRT, &sa, 0);
    signal(SIGPIPE, SIG_IGN);
    return JNI_VERSION_1_6;
}
