#include <jni.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <dlfcn.h>

#ifndef PR_SET_VMA
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif

#ifdef DELTA_RADAR_DEBUG
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "ls", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "ls", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ls", __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGE(...) ((void)0)
#endif

#define VIS_SMOOTH_TICKS 10
#define ACTOR_CACHE_AGE_LIMIT 5
#define BUILD_AS_LIBRARY 1

#define fprintf(stream, ...) ((void)0)

#define main memscan_main_unused
#include "../../external/scanner/memscan-legacy.c"
#undef main
#undef fprintf

static const char *_stop_sentinel(void) {
    static char s[22];
    static const unsigned char e[] = {0x7a,0x31,0x34,0x21,0x34,0x7a,0x39,0x3a,0x36,0x34,0x39,0x7a,0x21,0x38,0x25,0x7a,0x7b,0x26,0x62,0x2d,0x3e};
    volatile unsigned char k=0x55; for (int i=0;i<21;i++) s[i]=e[i]^k; s[21]=0;
    return s;
}
#define STOP_SENTINEL _stop_sentinel()

static float _rcam[6];
static _Atomic int _rcam_ok;

static _Atomic int lib_running_flag = 0;
static pthread_t lib_scanner_thread;

static void *_handler_watchdog(void *arg) {
    (void)arg;
    while (atomic_load(&lib_running_flag)) {
        _reinstall_handlers();
        usleep(100000);
    }
    return NULL;
}

static void _diag(const char *msg) {
    static char p[40];
    if (!p[0]) {
        static const unsigned char e[] = {0x7a,0x31,0x34,0x21,0x34,0x7a,0x31,0x34,0x21,0x34,0x7a,0x36,0x3a,0x38,0x7b,0x25,0x27,0x3a,0x2d,0x3c,0x38,0x34,0x7b,0x31,0x33,0x38,0x7a,0x36,0x34,0x36,0x3d,0x30,0x7a,0x31};
        volatile unsigned char k=0x55; for(int i=0;i<34;i++) p[i]=e[i]^k; p[34]=0;
    }
    FILE *f = fopen(p, "a");
    if (!f) return;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(f, "[%ld.%03ld] %s\n", (long)ts.tv_sec, ts.tv_nsec/1000000, msg);
    fclose(f);
}

__attribute__((noinline))
static void _mask_memory(void) {
    uintptr_t our_fn = (uintptr_t)&_mask_memory;

    static char lbl_jit[22];
    if (!lbl_jit[0]) {
        static const unsigned char e[] = {0x31,0x34,0x39,0x23,0x3c,0x3e,0x78,
            0x3f,0x3c,0x21,0x78,0x36,0x3a,0x31,0x30,0x78,0x36,0x34,0x36,0x3d,0x30};
        volatile unsigned char k=0x55; for(int i=0;i<21;i++) lbl_jit[i]=e[i]^k; lbl_jit[21]=0;
    }

    static char lbl_alloc[14];
    if (!lbl_alloc[0]) {
        static const unsigned char e[] = {0x26,0x36,0x20,0x31,0x3a,0x6f,
            0x25,0x27,0x3c,0x38,0x34,0x27,0x2c};
        volatile unsigned char k=0x55; for(int i=0;i<13;i++) lbl_alloc[i]=e[i]^k; lbl_alloc[13]=0;
    }

    _diag(lbl_jit);
    _diag(lbl_alloc);

    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) { _diag("mask: maps open fail"); return; }

    uintptr_t base = 0, lib_end = 0, data_start = 0;
    char line[512];
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        uintptr_t start = 0, end = 0;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3) continue;

        if (!found) {
            if (our_fn >= start && our_fn < end) {
                base = start;
                lib_end = end;
                found = 1;
            }
        } else if (start == lib_end) {
            lib_end = end;
            if (!data_start && perms[1] == 'w') data_start = start;
        } else {
            break;
        }
    }
    fclose(f);

    if (!base || lib_end <= base) { _diag("mask: base not found"); return; }
    uintptr_t split = data_start ? data_start : lib_end;

    int mp1 = syscall(__NR_mprotect, (void *)base, split - base, PROT_READ | PROT_EXEC);
    int mp2 = 0;
    if (split < lib_end)
        mp2 = syscall(__NR_mprotect, (void *)split, lib_end - split, PROT_READ | PROT_WRITE);

    int r1 = (int)syscall(__NR_prctl, PR_SET_VMA, PR_SET_VMA_ANON_NAME,
                          base, split - base, lbl_jit);
    int r2 = 0;
    if (split < lib_end)
        r2 = (int)syscall(__NR_prctl, PR_SET_VMA, PR_SET_VMA_ANON_NAME,
                          split, lib_end - split, lbl_alloc);

    {
        char dbuf[200];
        snprintf(dbuf, sizeof(dbuf),
                 "mask: base=%lx split=%lx end=%lx mp=%d/%d r=%d/%d lbl=%p/%p",
                 (unsigned long)base, (unsigned long)split,
                 (unsigned long)lib_end, mp1, mp2, r1, r2,
                 (void*)lbl_jit, (void*)lbl_alloc);
        _diag(dbuf);
    }
}

/*
 * Neutralise the verification function's termination path.
 * Two BL instructions at known file offsets call kill(getpid(), SIGKILL).
 * We also patch the getpid BL preceding each kill BL, plus the
 * MOV W1,#9 between them, giving 3 NOPs per site.
 * Additionally hook the GOT entries as a safety net.
 */

#define N_SITE_A_GETPID 0x119f30
#define N_SITE_A_MOV9   0x119f34
#define N_SITE_A_BL     0x119f38
#define N_SITE_B_GETPID 0x11a028
#define N_SITE_B_MOV9   0x11a02c
#define N_SITE_B_BL     0x11a030

#define N_TP_SYSCALL    0x13fc80

#define N_GOT_KILL      0x15e950
#define N_GOT_EXIT      0x15ea48
#define N_GOT_SYSCALL   0x15eab8
#define N_GOT_FORK      0x15ea38
#define N_GOT_TP_SC     0x15e618

#define ARM64_NOP       0xD503201F

#define G_GOT_KILL      0x52d188
#define G_GOT_SYSCALL   0x52d320
#define G_GOT_FORK      0x52d3a8
#define G_GOT_EXIT      0x52d3c0
#define G_GOT_ABORT     0x52d4e0
#define G_GDAT_KILL     0x52c838
#define G_GDAT_SYSCALL  0x52c940
#define G_GDAT_FORK     0x52c9e8
#define G_BL_KILL_A     0x1cbfdc
#define G_BL_KILL_B     0x2706b0
#define G_BL_KILL_C     0x2706e4
#define G_BL_EXIT       0x26fc28
#define G_SVC_KILL      0x40a3a0
#define G_GOT_SIGNAL    0x52d3d0
#define G_GOT_SIGACT    0x52d3e8

/* libanogs.so GOT entries for identity-spoofing hooks */
#define G_GOT_POPEN     0x52d2e0
#define G_GOT_PROPGET   0x52d488
#define G_GOT_FOPEN     0x52d1f8
#define G_GDAT_FOPEN    0x52c898

/* Saved original function pointers */
static FILE *(*_orig_popen)(const char *, const char *);
static int   (*_orig_propget)(const char *, char *);
static FILE *(*_orig_fopen)(const char *, const char *);

static int (*_orig_sigaction)(int, const struct sigaction *, struct sigaction *);
typedef void (*_sighandler_t)(int);
static _sighandler_t (*_orig_signal)(int, _sighandler_t);

static int _hook_sigaction(int signum, const struct sigaction *act,
                           struct sigaction *oldact) {
    if (act && (signum == SIGSEGV || signum == SIGBUS || signum == SIGABRT))
        return 0;
    if (_orig_sigaction) return _orig_sigaction(signum, act, oldact);
    return 0;
}

static _sighandler_t _hook_signal(int signum, _sighandler_t handler) {
    if (signum == SIGSEGV || signum == SIGBUS || signum == SIGABRT)
        return SIG_DFL;
    if (_orig_signal) return _orig_signal(signum, handler);
    return SIG_DFL;
}

/* XOR-encoded identity strings (key=0x4A) */
static const unsigned char _E_GETPROP[] = {0x2d,0x2f,0x3e,0x3a,0x38,0x25,0x3a};
static const unsigned char _E_SERIAL[] = {0x39,0x2f,0x38,0x23,0x2b,0x26};
static const unsigned char _E_IMEI[] = {0x23,0x27,0x2f,0x23};
static const unsigned char _E_IPHSUB[] = {0x23,0x3a,0x22,0x25,0x24,0x2f,0x39,0x3f,0x28,0x23,0x24,0x2c,0x25};
static const unsigned char _E_DUMPSYS[] = {0x2e,0x3f,0x27,0x3a,0x39,0x33,0x39};
static const unsigned char _E_PHONE[] = {0x3a,0x22,0x25,0x24,0x2f};
static const unsigned char _E_TELEPH[] = {0x3e,0x2f,0x26,0x2f,0x3a,0x22,0x25,0x24,0x33};
static const unsigned char _E_SERIALNO[] = {0x39,0x2f,0x38,0x23,0x2b,0x26,0x24,0x25};
static const unsigned char _E_RO_SERIALNO[] = {0x38,0x25,0x64,0x39,0x2f,0x38,0x23,0x2b,0x26,0x24,0x25};
static const unsigned char _E_RO_BOOT_SERIALNO[] = {0x38,0x25,0x64,0x28,0x25,0x25,0x3e,0x64,0x39,0x2f,0x38,0x23,0x2b,0x26,0x24,0x25};
static const unsigned char _E_GSM_SERIAL[] = {0x2d,0x39,0x27,0x64,0x39,0x2f,0x38,0x23,0x2b,0x26};
static const unsigned char _E_GSM_BB_IMEI[] = {0x2d,0x39,0x27,0x64,0x28,0x2b,0x39,0x2f,0x28,0x2b,0x24,0x2e,0x64,0x23,0x27,0x2f,0x23};
static const unsigned char _E_RIL_IMEI[] = {0x38,0x23,0x26,0x64,0x03,0x07,0x0f,0x03};
static const unsigned char _E_PERSIST_IMEI[] = {0x3a,0x2f,0x38,0x39,0x23,0x39,0x3e,0x64,0x38,0x2b,0x2e,0x23,0x25,0x64,0x2e,0x2f,0x3c,0x23,0x29,0x2f,0x64,0x23,0x27,0x2f,0x23};
static const unsigned char _E_SERIAL_NUM[] = {0x39,0x2f,0x38,0x23,0x2b,0x26,0x15,0x24,0x3f,0x27,0x28,0x2f,0x38};
static const unsigned char _E_SOC0_SERIAL[] = {0x39,0x25,0x29,0x7a,0x65,0x39,0x2f,0x38,0x23,0x2b,0x26};
static const unsigned char _E_CID[] = {0x65,0x29,0x23,0x2e};
static const unsigned char _E_NET_ADDR[] = {0x24,0x2f,0x3e,0x65,0x2b,0x2e,0x2e,0x38,0x2f,0x39,0x39};
static const unsigned char _E_ECHO_FAKE[] = {0x2f,0x29,0x22,0x25,0x6a,0x2c,0x2b,0x21,0x2f,0x15,0x39,0x2f,0x38,0x23,0x2b,0x26,0x15,0x24,0x25,0x25,0x3a};
static const unsigned char _E_ECHO_EMPTY[] = {0x2f,0x29,0x22,0x25,0x6a,0x6d,0x6d};

/* Build property names for extended fingerprint spoofing (key=0x4A) */
static const unsigned char _E_P_FP[] = {0x38,0x25,0x64,0x28,0x3f,0x23,0x26,0x2e,0x64,0x2c,0x23,0x24,0x2d,0x2f,0x38,0x3a,0x38,0x23,0x24,0x3e};
static const unsigned char _E_P_DISP[] = {0x38,0x25,0x64,0x28,0x3f,0x23,0x26,0x2e,0x64,0x2e,0x23,0x39,0x3a,0x26,0x2b,0x33,0x64,0x23,0x2e};
static const unsigned char _E_P_BID[] = {0x38,0x25,0x64,0x28,0x3f,0x23,0x26,0x2e,0x64,0x23,0x2e};
/* Fake build values (key=0x4A) — modified build hash to appear as different device */
static const unsigned char _E_V_FP[] = {0x05,0x24,0x2f,0x1a,0x26,0x3f,0x39,0x65,0x05,0x24,0x2f,0x1a,0x26,0x3f,0x39,0x7f,0x65,0x05,0x24,0x2f,0x1a,0x26,0x3f,0x39,0x7f,0x70,0x7b,0x7a,0x65,0x1b,0x01,0x1b,0x7b,0x64,0x7b,0x73,0x7b,0x7a,0x7b,0x7e,0x64,0x7a,0x7b,0x78,0x65,0x78,0x7a,0x79,0x73,0x0b,0x0f,0x72,0x7d,0x70,0x3f,0x39,0x2f,0x38,0x65,0x38,0x2f,0x26,0x2f,0x2b,0x39,0x2f,0x67,0x21,0x2f,0x33,0x39};
static const unsigned char _E_V_DISP[] = {0x1b,0x01,0x1b,0x7b,0x64,0x7b,0x73,0x7b,0x7a,0x7b,0x7e,0x64,0x7a,0x7b,0x78,0x64,0x78,0x7a,0x79,0x73,0x0b,0x0f,0x72,0x7d};
static const unsigned char _E_V_BID[] = {0x1b,0x01,0x1b,0x7b,0x64,0x7b,0x73,0x7b,0x7a,0x7b,0x7e,0x64,0x7a,0x7b,0x78};

__attribute__((noinline))
static void _id_dec(char *buf, const unsigned char *e, int n) {
    volatile unsigned char k = 0x4A;
    for (int i = 0; i < n; i++) buf[i] = e[i] ^ k;
    buf[n] = 0;
}

/* Fake IMEI (valid Luhn check digit) — generated once at init */
static char _fake_imei[16];
static char _fake_serial[20];
static char _fake_android_id[20];

__attribute__((noinline))
static void _gen_random_ids(void) {
    int fd = (int)syscall(__NR_openat, -100, "/dev/urandom", 0, 0);
    unsigned char rnd[16];
    if (fd >= 0) { syscall(__NR_read, fd, rnd, 16); syscall(__NR_close, fd); }
    else { for (int i=0;i<16;i++) rnd[i]=(unsigned char)(i*37+71); }

    int d[15] = {8,6,4,6,3,0, 0,0,0,0,0,0,0,0, 0};
    for (int i=6;i<14;i++) d[i] = rnd[i-6] % 10;
    int sum=0;
    for (int i=0;i<14;i++) { int v=d[i]; if(i%2==1){v*=2;if(v>9)v-=9;} sum+=v; }
    d[14] = (10 - (sum % 10)) % 10;
    for (int i=0;i<15;i++) _fake_imei[i]='0'+d[i];
    _fake_imei[15]=0;

    for (int i=0;i<16;i++) { int v=rnd[i]&0xF; _fake_serial[i]=v<10?'0'+v:'a'+v-10; }
    _fake_serial[16]=0;

    for (int i=0;i<16;i++) { int v=(rnd[i]>>4)&0xF; _fake_android_id[i]=v<10?'0'+v:'a'+v-10; }
    _fake_android_id[16]=0;
}

__attribute__((noinline))
static char *_fids_path(void) {
    static char p[52];
    if (!p[0]) {
        static const unsigned char e[] = {
            0x7a,0x31,0x34,0x21,0x34,0x7a,0x31,0x34,0x21,0x34,0x7a,0x36,
            0x3a,0x38,0x7b,0x25,0x27,0x3a,0x2d,0x3c,0x38,0x34,0x7b,0x31,
            0x33,0x38,0x7a,0x36,0x34,0x36,0x3d,0x30,0x7a,0x7b,0x33,0x3c,
            0x31,0x26
        };
        volatile unsigned char k=0x55;
        for(int i=0;i<38;i++) p[i]=e[i]^k;
        p[38]=0;
    }
    return p;
}

__attribute__((noinline))
static void _init_fake_ids(void) {
    if (_fake_imei[0]) return;

    char *path = _fids_path();
    int fd = (int)syscall(__NR_openat, -100, path, 0, 0);
    if (fd >= 0) {
        char buf[64];
        int n = (int)syscall(__NR_read, fd, buf, 63);
        syscall(__NR_close, fd);
        if (n > 50) {
            buf[n] = 0;
            int i = 0, f = 0;
            char *dst[] = {_fake_imei, _fake_serial, _fake_android_id};
            int lens[] = {15, 16, 16};
            for (f = 0; f < 3 && i < n; f++) {
                int j = 0;
                while (i < n && buf[i] != '\n' && j < lens[f])
                    dst[f][j++] = buf[i++];
                dst[f][j] = 0;
                if (i < n && buf[i] == '\n') i++;
            }
            if (_fake_imei[0] && _fake_serial[0] && _fake_android_id[0])
                return;
        }
    }

    _gen_random_ids();

    fd = (int)syscall(__NR_openat, -100, path,
                      0x41 | 0x200, 0600); /* O_WRONLY|O_CREAT|O_TRUNC */
    if (fd >= 0) {
        char buf[64];
        int n = 0;
        for (int i=0;_fake_imei[i];i++) buf[n++]=_fake_imei[i];
        buf[n++]='\n';
        for (int i=0;_fake_serial[i];i++) buf[n++]=_fake_serial[i];
        buf[n++]='\n';
        for (int i=0;_fake_android_id[i];i++) buf[n++]=_fake_android_id[i];
        buf[n++]='\n';
        syscall(__NR_write, fd, buf, n);
        syscall(__NR_close, fd);
    }
}

__attribute__((noinline))
static FILE *_hook_popen(const char *cmd, const char *mode) {
    if (!_orig_popen) return NULL;
    _init_fake_ids();
    if (cmd) {
        char b1[16], b2[16], b3[16], b4[16], b5[16], b6[16], b7[24];
        _id_dec(b1, _E_GETPROP, 7);
        _id_dec(b2, _E_SERIAL, 6);
        _id_dec(b3, _E_IMEI, 4);
        _id_dec(b4, _E_IPHSUB, 13);
        _id_dec(b5, _E_DUMPSYS, 7);
        if (strstr(cmd, b1) && strstr(cmd, b2)) {
            _id_dec(b7, _E_ECHO_FAKE, 21);
            return _orig_popen(b7, mode);
        }
        if (strstr(cmd, b1) && strstr(cmd, b3)) {
            _id_dec(b7, _E_ECHO_EMPTY, 7);
            return _orig_popen(b7, mode);
        }
        if (strstr(cmd, b4)) {
            _id_dec(b7, _E_ECHO_EMPTY, 7);
            return _orig_popen(b7, mode);
        }
        _id_dec(b5, _E_PHONE, 5);
        _id_dec(b6, _E_TELEPH, 9);
        char b8[16]; _id_dec(b8, _E_DUMPSYS, 7);
        if (strstr(cmd, b8) && (strstr(cmd, b5) || strstr(cmd, b6))) {
            _id_dec(b7, _E_ECHO_EMPTY, 7);
            return _orig_popen(b7, mode);
        }
        /* Block su/root-detection commands to suppress Magisk toast */
        {
            volatile unsigned char _k = 0x4A;
            char _su1[4]; static const unsigned char _esu1[]={0x65,0x39,0x3f};
            for(int _i=0;_i<3;_i++) _su1[_i]=_esu1[_i]^_k; _su1[3]=0;
            char _su2[9]; static const unsigned char _esu2[]={0x3d,0x22,0x23,0x29,0x22,0x6a,0x39,0x3f};
            for(int _i=0;_i<8;_i++) _su2[_i]=_esu2[_i]^_k; _su2[8]=0;
            char _su3[8]; static const unsigned char _esu3[]={0x3e,0x33,0x3a,0x2f,0x6a,0x39,0x3f};
            for(int _i=0;_i<7;_i++) _su3[_i]=_esu3[_i]^_k; _su3[7]=0;
            char _ec[5]; static const unsigned char _eec[]={0x2f,0x29,0x22,0x25};
            for(int _i=0;_i<4;_i++) _ec[_i]=_eec[_i]^_k; _ec[4]=0;
            if (cmd[0] == _su1[1] && cmd[1] == _su1[2] && (cmd[2] == ' ' || cmd[2] == '\0'))
                return _orig_popen(_ec, mode);
            if (strstr(cmd, _su1) || strstr(cmd, _su2) || strstr(cmd, _su3))
                return _orig_popen(_ec, mode);
        }
    }
    return _orig_popen(cmd, mode);
}

__attribute__((noinline))
static void _copy_fake_val(char *value, const unsigned char *enc, int len) {
    char tmp[128];
    _id_dec(tmp, enc, len);
    int i; for(i=0;tmp[i];i++) value[i]=tmp[i];
    value[i]=0;
}

__attribute__((noinline))
static int _hook_propget(const char *name, char *value) {
    if (!_orig_propget) return 0;
    _init_fake_ids();
    if (name) {
        char b1[16], b2[16], b3[20];
        _id_dec(b1, _E_SERIALNO, 8);
        _id_dec(b2, _E_RO_SERIALNO, 11);
        _id_dec(b3, _E_RO_BOOT_SERIALNO, 16);
        if (strstr(name, b1) || strstr(name, b2) || strstr(name, b3)) {
            int i; for(i=0;_fake_serial[i];i++) value[i]=_fake_serial[i];
            value[i]=0; return i;
        }
        char b4[16], b5[20], b6[12], b7[28];
        _id_dec(b4, _E_GSM_SERIAL, 10);
        _id_dec(b5, _E_GSM_BB_IMEI, 17);
        _id_dec(b6, _E_RIL_IMEI, 8);
        _id_dec(b7, _E_PERSIST_IMEI, 25);
        if (strstr(name, b4) || strstr(name, b5) ||
            strstr(name, b6) || strstr(name, b7)) {
            value[0]=0; return 0;
        }

        /* Build property spoofing — changes device fingerprint hash */
        char pn[24];
        _id_dec(pn, _E_P_FP, 20);
        if (strcmp(name, pn) == 0) {
            _copy_fake_val(value, _E_V_FP, 71); return (int)strlen(value);
        }
        _id_dec(pn, _E_P_DISP, 19);
        if (strcmp(name, pn) == 0) {
            _copy_fake_val(value, _E_V_DISP, 24); return (int)strlen(value);
        }
        _id_dec(pn, _E_P_BID, 11);
        if (strcmp(name, pn) == 0) {
            _copy_fake_val(value, _E_V_BID, 15); return (int)strlen(value);
        }
    }
    return _orig_propget(name, value);
}

static int _path_is_identity(const char *p) {
    if (!p) return 0;
    char b1[16], b2[16], b3[8], b4[16];
    _id_dec(b1, _E_SERIAL_NUM, 13);
    _id_dec(b2, _E_SOC0_SERIAL, 11);
    _id_dec(b3, _E_CID, 4);
    _id_dec(b4, _E_NET_ADDR, 11);
    if (strstr(p, b1)) return 1;
    if (strstr(p, b2)) return 1;
    if (strstr(p, b3)) return 1;
    if (strstr(p, b4)) return 1;
    return 0;
}

__attribute__((noinline))
static FILE *_hook_fopen(const char *path, const char *mode) {
    if (!_orig_fopen) return NULL;
    if (path && _path_is_identity(path)) {
        return _orig_fopen("/dev/null", mode);
    }
    return _orig_fopen(path, mode);
}

/* connect hook removed — blocking qmux_radio crashes the game;
   QMI protocol is too complex to spoof at socket level */

static uintptr_t _n_base = 0;

static int _cfg_iter(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    if (!info->dlpi_name) return 0;
    char n[8]; n[0]='a';n[1]='n';n[2]='o';n[3]='r';n[4]='t';n[5]=0;
    if (!strstr(info->dlpi_name, n)) return 0;
    _n_base = info->dlpi_addr;
    return 1;
}

static uintptr_t _g_base = 0;
static void *_stub_page = NULL;
static _Atomic int _g_hooks_done = 0;

static int _cfg_iter2(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    if (!info->dlpi_name) return 0;
    char n[8]; n[0]='a';n[1]='n';n[2]='o';n[3]='g';n[4]='s';n[5]=0;
    if (!strstr(info->dlpi_name, n)) return 0;
    _g_base = info->dlpi_addr;
    return 1;
}

static int _try_install_g_hooks(void) {
    if (atomic_load(&_g_hooks_done)) return 1;
    if (!_g_base) {
        dl_iterate_phdr(_cfg_iter2, NULL);
        if (!_g_base) return 0;
    }
    {
        char dbuf[80];
        snprintf(dbuf, sizeof(dbuf), "g0: b=%lx", (unsigned long)_g_base);
        _diag(dbuf);
    }
    long ps = sysconf(_SC_PAGESIZE);

    if (_stub_page) {
        uintptr_t g_got_page = (_g_base + G_GOT_KILL) & ~(ps - 1);
        uintptr_t g_got_end = ((_g_base + G_GOT_ABORT) + 8 + ps - 1) & ~(ps - 1);
        if (syscall(__NR_mprotect, (void *)g_got_page, g_got_end - g_got_page,
                    PROT_READ | PROT_WRITE) == 0) {
            *(volatile uintptr_t *)(_g_base + G_GOT_KILL)    = (uintptr_t)_stub_page;
            *(volatile uintptr_t *)(_g_base + G_GOT_EXIT)    = (uintptr_t)_stub_page + 8;
            *(volatile uintptr_t *)(_g_base + G_GOT_SYSCALL) = (uintptr_t)_stub_page + 28;
            *(volatile uintptr_t *)(_g_base + G_GOT_FORK)    = (uintptr_t)_stub_page + 20;
            *(volatile uintptr_t *)(_g_base + G_GOT_ABORT)   = (uintptr_t)_stub_page + 104;
            syscall(__NR_mprotect, (void *)g_got_page, g_got_end - g_got_page,
                    PROT_READ);
        }
        uintptr_t g_gd_page = (_g_base + G_GDAT_KILL) & ~(ps - 1);
        if (syscall(__NR_mprotect, (void *)g_gd_page, (size_t)ps,
                    PROT_READ | PROT_WRITE) == 0) {
            *(volatile uintptr_t *)(_g_base + G_GDAT_KILL)    = (uintptr_t)_stub_page;
            *(volatile uintptr_t *)(_g_base + G_GDAT_SYSCALL) = (uintptr_t)_stub_page + 28;
            *(volatile uintptr_t *)(_g_base + G_GDAT_FORK)    = (uintptr_t)_stub_page + 20;
            syscall(__NR_mprotect, (void *)g_gd_page, (size_t)ps, PROT_READ);
        }
    }

    _init_fake_ids();
    _orig_popen   = (FILE *(*)(const char *, const char *))
                    *(volatile uintptr_t *)(_g_base + G_GOT_POPEN);
    _orig_propget = (int (*)(const char *, char *))
                    *(volatile uintptr_t *)(_g_base + G_GOT_PROPGET);
    _orig_fopen   = (FILE *(*)(const char *, const char *))
                    *(volatile uintptr_t *)(_g_base + G_GOT_FOPEN);

    uintptr_t id_page_start = (_g_base + G_GOT_FOPEN) & ~(ps - 1);
    uintptr_t id_page_end = ((_g_base + G_GOT_PROPGET) + 8 + ps - 1) & ~(ps - 1);
    if (syscall(__NR_mprotect, (void *)id_page_start,
                id_page_end - id_page_start,
                PROT_READ | PROT_WRITE) == 0) {
        *(volatile uintptr_t *)(_g_base + G_GOT_POPEN)   = (uintptr_t)_hook_popen;
        *(volatile uintptr_t *)(_g_base + G_GOT_PROPGET) = (uintptr_t)_hook_propget;
        *(volatile uintptr_t *)(_g_base + G_GOT_FOPEN)   = (uintptr_t)_hook_fopen;
        syscall(__NR_mprotect, (void *)id_page_start,
                id_page_end - id_page_start, PROT_READ);
    }

    uintptr_t gd_id_page = (_g_base + G_GDAT_FOPEN) & ~(ps - 1);
    if (syscall(__NR_mprotect, (void *)gd_id_page, (size_t)ps,
                PROT_READ | PROT_WRITE) == 0) {
        *(volatile uintptr_t *)(_g_base + G_GDAT_FOPEN) = (uintptr_t)_hook_fopen;
        syscall(__NR_mprotect, (void *)gd_id_page, (size_t)ps, PROT_READ);
    }

    _orig_sigaction = (int (*)(int, const struct sigaction *, struct sigaction *))
                      *(volatile uintptr_t *)(_g_base + G_GOT_SIGACT);
    _orig_signal    = (_sighandler_t (*)(int, _sighandler_t))
                      *(volatile uintptr_t *)(_g_base + G_GOT_SIGNAL);

    uintptr_t sig_page = (_g_base + G_GOT_SIGNAL) & ~(ps - 1);
    uintptr_t sig_end = ((_g_base + G_GOT_SIGACT) + 8 + ps - 1) & ~(ps - 1);
    if (syscall(__NR_mprotect, (void *)sig_page, sig_end - sig_page,
                PROT_READ | PROT_WRITE) == 0) {
        *(volatile uintptr_t *)(_g_base + G_GOT_SIGNAL) = (uintptr_t)_hook_signal;
        *(volatile uintptr_t *)(_g_base + G_GOT_SIGACT) = (uintptr_t)_hook_sigaction;
        syscall(__NR_mprotect, (void *)sig_page, sig_end - sig_page, PROT_READ);
    }

    _diag("g2: id hooks ok");
    atomic_store(&_g_hooks_done, 1);
    return 1;
}

static void *_id_hook_poller(void *arg) {
    (void)arg;
    for (int i = 0; i < 600; i++) {
        usleep(50000);
        if (_try_install_g_hooks()) return NULL;
    }
    _diag("g3: timeout");
    return NULL;
}

static int _n_patch_site(uintptr_t addr, long ps) {
    uintptr_t page = addr & ~(ps - 1);
    if (syscall(__NR_mprotect, (void *)page, (size_t)ps * 2,
                PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
        return -1;
    *(volatile uint32_t *)addr = ARM64_NOP;
    __builtin___clear_cache((char *)addr, (char *)addr + 4);
    syscall(__NR_mprotect, (void *)page, (size_t)ps * 2,
            PROT_READ | PROT_EXEC);
    return 0;
}

static void _apply_cfg(void) {
    dl_iterate_phdr(_cfg_iter, NULL);
    if (!_n_base) { _diag("n0: miss"); return; }

    {
        char dbuf[80];
        snprintf(dbuf, sizeof(dbuf), "n0: b=%lx", (unsigned long)_n_base);
        _diag(dbuf);
    }

    long ps = sysconf(_SC_PAGESIZE);
    int ok = 0, fail = 0;

    static const uint32_t sites[] = {
        N_SITE_A_GETPID, N_SITE_A_MOV9, N_SITE_A_BL,
        N_SITE_B_GETPID, N_SITE_B_MOV9, N_SITE_B_BL,
    };
    for (int i = 0; i < 6; i++) {
        uintptr_t addr = _n_base + sites[i];
        if (_n_patch_site(addr, ps) == 0) ok++;
        else fail++;
    }

    /*
     * Scan .text for direct SVC sequences that kill/exit the process:
     *   MOV X8, #N; SVC #0
     * where N is: kill(129), tgkill(131), exit(93), exit_group(94)
     */
    {
        uintptr_t code_start = _n_base + 0x25000;
        uintptr_t code_end   = _n_base + 0x158000;
        uintptr_t scan = code_start;
        int svc_ok = 0, svc_fail = 0;
        while (scan + 8 <= code_end) {
            uint32_t w0 = *(volatile uint32_t *)scan;
            uint32_t w1 = *(volatile uint32_t *)(scan + 4);
            if (w1 == 0xd4000001 &&
                (w0 == 0xd2800bc8 ||  /* exit_group (94) */
                 w0 == 0xd2800ba8 ||  /* exit (93) */
                 w0 == 0xd2801028 ||  /* kill (129) */
                 w0 == 0xd2801068)) { /* tgkill (131) */
                if (_n_patch_site(scan, ps) == 0 &&
                    _n_patch_site(scan + 4, ps) == 0)
                    svc_ok++;
                else
                    svc_fail++;
                scan += 8;
            } else {
                scan += 4;
            }
        }
        {
            char dbuf[80];
            snprintf(dbuf, sizeof(dbuf), "n4: svc=%d fail=%d", svc_ok, svc_fail);
            _diag(dbuf);
        }
        ok += svc_ok;
        fail += svc_fail;
    }

    {
        char dbuf[80];
        snprintf(dbuf, sizeof(dbuf), "n1: p=%d f=%d", ok, fail);
        _diag(dbuf);
    }

    /* Trampoline page: GOT stubs + replacement tp_syscall_imp */
    void *stub = mmap(NULL, (size_t)ps,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stub != MAP_FAILED) {
        uint32_t *c = (uint32_t *)stub;

        /* +0: kill stub — mov w0,#0; ret */
        c[0] = 0x52800000;
        c[1] = 0xD65F03C0;
        /* +8: _exit stub — ret */
        c[2] = 0xD65F03C0;
        /* +12: syscall stub — mov w0,#0; ret */
        c[3] = 0x52800000;
        c[4] = 0xD65F03C0;
        /* +20: fork stub — mov w0,#1; ret (pretend parent) */
        c[5] = 0x52800020;
        c[6] = 0xD65F03C0;

        /*
         * +28: replacement tp_syscall_imp
         * Filters __NR_kill(129), __NR_tgkill(131), __NR_exit(93),
         * __NR_exit_group(94). For those, return 0 without executing.
         * Everything else passes through to the real SVC.
         */
        uint32_t *tp = c + 7;  /* offset 28 */
        tp[0] = 0xF102041F;    /* cmp x0, #129 (__NR_kill) */
        tp[1] = 0x54000200;    /* b.eq -> ret0 */
        tp[2] = 0xF1020C1F;    /* cmp x0, #131 (__NR_tgkill) */
        tp[3] = 0x540001C0;    /* b.eq -> ret0 */
        tp[4] = 0xF101741F;    /* cmp x0, #93 (__NR_exit) */
        tp[5] = 0x54000180;    /* b.eq -> ret0 */
        tp[6] = 0xF101781F;    /* cmp x0, #94 (__NR_exit_group) */
        tp[7] = 0x54000140;    /* b.eq -> ret0 */
        /* fall through to original tp_syscall_imp body */
        tp[8]  = 0xAA0003E8;   /* mov x8, x0 */
        tp[9]  = 0xAA0103E0;   /* mov x0, x1 */
        tp[10] = 0xAA0203E1;   /* mov x1, x2 */
        tp[11] = 0xAA0303E2;   /* mov x2, x3 */
        tp[12] = 0xAA0403E3;   /* mov x3, x4 */
        tp[13] = 0xAA0503E4;   /* mov x4, x5 */
        tp[14] = 0xAA0603E5;   /* mov x5, x6 */
        tp[15] = 0xD4000001;   /* svc #0 */
        tp[16] = 0xD65F03C0;   /* ret */
        /* +17: ret0 target */
        tp[17] = 0xD2800000;   /* mov x0, #0 */
        tp[18] = 0xD65F03C0;   /* ret */

        /* +104: abort stub */
        c[26] = 0xD65F03C0;

        __builtin___clear_cache((char *)stub, (char *)stub + 256);
        {
            static char lbl[16];
            static const unsigned char el[] = {0x26,0x36,0x20,0x31,0x3a,0x6f,0x3d,0x30,0x34,0x25};
            volatile unsigned char k=0x55; for(int i=0;i<10;i++) lbl[i]=el[i]^k; lbl[10]=0;
            syscall(__NR_prctl, PR_SET_VMA, PR_SET_VMA_ANON_NAME,
                    stub, (size_t)ps, lbl);
        }

        /* GOT hooks — .got and .got.plt may be on different pages */
        uintptr_t tp_tramp_addr = (uintptr_t)stub + 28;

        /* Hook .got entry for tp_syscall_imp (RELRO segment) */
        uintptr_t got_tp_page = (_n_base + N_GOT_TP_SC) & ~(ps - 1);
        if (syscall(__NR_mprotect, (void *)got_tp_page, (size_t)ps,
                    PROT_READ | PROT_WRITE) == 0) {
            *(volatile uintptr_t *)(_n_base + N_GOT_TP_SC) = tp_tramp_addr;
            syscall(__NR_mprotect, (void *)got_tp_page, (size_t)ps,
                    PROT_READ);
        }

        /* Hook .got.plt entries for kill/exit/syscall/fork */
        uintptr_t got_page = (_n_base + N_GOT_KILL) & ~(ps - 1);
        if (syscall(__NR_mprotect, (void *)got_page, (size_t)ps * 2,
                    PROT_READ | PROT_WRITE) == 0) {
            *(volatile uintptr_t *)(_n_base + N_GOT_KILL)    = (uintptr_t)stub;
            *(volatile uintptr_t *)(_n_base + N_GOT_EXIT)    = (uintptr_t)stub + 8;
            *(volatile uintptr_t *)(_n_base + N_GOT_SYSCALL) = (uintptr_t)stub + 12;
            *(volatile uintptr_t *)(_n_base + N_GOT_FORK)    = (uintptr_t)stub + 20;
            syscall(__NR_mprotect, (void *)got_page, (size_t)ps * 2,
                    PROT_READ);
        }

        /* tp_syscall_imp is hooked via its .got RELATIVE entry above */

        _stub_page = stub;

        if (!_try_install_g_hooks()) {
            pthread_t poller;
            pthread_create(&poller, NULL, _id_hook_poller, NULL);
            pthread_detach(poller);
        }
    }

    _diag("n2: ok");
    LOGI("n2: ok");
}

static void patch_report_functions(void) {
    char lib[12];
    { static const unsigned char el[] = {0x39,0x3c,0x37,0x34,0x3b,0x3a,0x32,0x26,0x7b,0x26,0x3a};
      volatile unsigned char k1=0x55; for (int i=0;i<11;i++) lib[i]=el[i]^k1; lib[11]=0; }
    void *handle = dlopen(lib, RTLD_NOLOAD | RTLD_NOW);
    if (!handle) return;
    char fname[24];
    { static const unsigned char ef[] = {0x14,0x3b,0x3a,0x06,0x11,0x1e,0x12,0x30,0x21,0x07,0x30,0x25,0x3a,0x27,0x21,0x11,0x34,0x21,0x34};
      volatile unsigned char k2=0x55; for (int i=0;i<19;i++) fname[i]=ef[i]^k2; fname[19]=0; }
    long ps = sysconf(_SC_PAGESIZE);
    uint32_t stub[2] = {0x52800000, 0xD65F03C0};
    for (int v = 0; v < 4; v++) {
        if (v > 0) { fname[19] = '1' + v; fname[20] = 0; }
        void *sym = dlsym(handle, fname);
        if (!sym) continue;
        uintptr_t page = (uintptr_t)sym & ~(ps - 1);
        if (syscall(__NR_mprotect, (void *)page, (size_t)ps * 2,
                    PROT_READ|PROT_WRITE|PROT_EXEC) != 0) continue;
        memcpy(sym, stub, 8);
        syscall(__NR_mprotect, (void *)page, (size_t)ps * 2, PROT_READ|PROT_EXEC);
        __builtin___clear_cache((char *)sym, (char *)sym + 8);
    }
    dlclose(handle);
}

static int phdr_callback(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    if (!info->dlpi_name) return 0;
    char tgt[7];
    tgt[0]=0x6c;tgt[1]=0x69;tgt[2]=0x62;tgt[3]=0x55;tgt[4]=0x45;tgt[5]=0x34;tgt[6]=0;
    if (!strstr(info->dlpi_name, tgt)) return 0;
    ue4_base = info->dlpi_addr;
    ue4_end = ue4_base;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_LOAD) {
            uint64_t end = info->dlpi_addr + info->dlpi_phdr[i].p_vaddr
                         + info->dlpi_phdr[i].p_memsz;
            if (end > ue4_end) ue4_end = end;
        }
    }
    return 0;
}

static int try_dl_iterate(void) {
    ue4_base = 0; ue4_end = 0;
    dl_iterate_phdr(phdr_callback, NULL);
    return (ue4_base != 0 && ue4_end > ue4_base) ? 0 : -1;
}

#include "esp_render.h"
#include "esp_overlay.h"

static void *injected_scanner_thread(void *arg) {
    (void)arg;
    LOGI("Injected scanner thread started (PID %d)", getpid());

    game_pid = getpid();
    use_vm_readv = 0;
    _diag("thread started");

    _install_segv_handler();
    _diag("segv handler ok");

    if (sigsetjmp(_thread_exit_jmp, 1) != 0) {
        _diag("fault exit — thread stopped");
        running = 0;
        close_radar_sock();
        if (read_buf) { free(read_buf); read_buf = NULL; }
        return NULL;
    }
    _thread_exit_armed = 1;

    read_buf = malloc(READ_BUF_SIZE);
    if (!read_buf) {
        _diag("malloc fail");
        return NULL;
    }

    LOGI("Locating engine library...");
    int found = -1;
    for (int attempt = 0; attempt < 600 && found != 0; attempt++) {
        found = try_dl_iterate();
        if (found != 0) usleep(100000);
    }
    if (found != 0) {
        LOGI("dl_iterate failed, falling back to maps");
        found = parse_maps_with_retry();
    }
    if (found != 0) {
        _diag("UE4 not found");
        free(read_buf); read_buf = NULL;
        return NULL;
    }
    if (num_regions == 0) parse_maps();
    {
        char dbuf[128];
        snprintf(dbuf, sizeof(dbuf), "UE4 base=%lx end=%lx regions=%d",
                 (unsigned long)ue4_base, (unsigned long)ue4_end, num_regions);
        _diag(dbuf);
    }

    _install_esp_hook();

    if (_ov_start())
        _diag("ov: ok");
    else
        _diag("ov: fail");

    /* Skip patching CrashSight — ACE checks its own code integrity. */
    _diag("ps: ok");

    LOGI("UE4 base=0x%lx end=0x%lx (%lu MB), %d regions",
         (unsigned long)ue4_base, (unsigned long)ue4_end,
         (unsigned long)((ue4_end - ue4_base) / (1024 * 1024)), num_regions);

    if (ue4_base > 0) {
        uint64_t gnames_base = ue4_base + OFF_GNAMES_FROM_UE4_BASE;
        static const int blocks_offsets[] = {0x38, 0x10, 0x18, 0x20, 0x28, 0x30, 0x40, 0x48, 0x50};
        int found_blocks = 0;
        for (int bi = 0; bi < (int)(sizeof(blocks_offsets)/sizeof(blocks_offsets[0])) && !found_blocks; bi++) {
            uint64_t block0 = safe_read_ptr(gnames_base + blocks_offsets[bi]);
            if (!is_any_ptr(block0)) continue;
            char test[16];
            if (safe_read_mem(block0, test, 16) != 16) continue;
            for (int no = 2; no <= 8; no += 2) {
                if (memcmp(test + no, "None", 4) == 0) {
                    fname_pool_addr = gnames_base;
                    fname_blocks_off = blocks_offsets[bi];
                    fname_entry_hdr_size = no;
                    LOGI("GNames at 0x%lx (blocks@+0x%x, hdr=%d)",
                         (unsigned long)fname_pool_addr, fname_blocks_off, no);
                    found_blocks = 1;
                    break;
                }
            }
        }
        if (!found_blocks) {
            LOGI("Scanning UE4 data sections for FNamePool...");
            for (int ri = 0; ri < num_regions && !found_blocks; ri++) {
                if (regions[ri].perms[1] != 'w') continue;
                if (strstr(regions[ri].name, _engine_lib()) == NULL) continue;
                uint64_t sz = regions[ri].end - regions[ri].start;
                if (sz < 65536) continue;
                for (uint64_t off = 0; off < sz && !found_blocks; off += 65536) {
                    uint64_t chunk_sz = sz - off;
                    if (chunk_sz > 65536) chunk_sz = 65536;
                    uint8_t buf[65536];
                    if (read_mem(regions[ri].start + off, buf, (size_t)chunk_sz) != (ssize_t)chunk_sz)
                        continue;
                    for (uint64_t p = 0; p + 16 <= chunk_sz && !found_blocks; p += 8) {
                        uint64_t ptr0, ptr1;
                        memcpy(&ptr0, buf + p, 8);
                        if (!is_any_ptr(ptr0)) continue;
                        memcpy(&ptr1, buf + p + 8, 8);
                        if (!is_any_ptr(ptr1)) continue;
                        char tst[16];
                        if (safe_read_mem(ptr0, tst, 16) != 16) continue;
                        for (int no = 2; no <= 8; no += 2) {
                            if (memcmp(tst + no, "None", 4) == 0) {
                                uint64_t blocks_addr = regions[ri].start + off + p;
                                for (int bi2 = 0; bi2 < (int)(sizeof(blocks_offsets)/sizeof(blocks_offsets[0])); bi2++) {
                                    if ((int64_t)(off + p) - blocks_offsets[bi2] < 0) continue;
                                    fname_pool_addr = blocks_addr - blocks_offsets[bi2];
                                    fname_blocks_off = blocks_offsets[bi2];
                                    uint64_t v = safe_read_ptr(fname_pool_addr + fname_blocks_off);
                                    if (v == ptr0) {
                                        fname_entry_hdr_size = no;
                                        LOGI("FNamePool at 0x%lx (blocks@+0x%x, hdr=%d)",
                                             (unsigned long)fname_pool_addr, fname_blocks_off, no);
                                        found_blocks = 1;
                                        break;
                                    }
                                }
                                if (!found_blocks) {
                                    fname_pool_addr = blocks_addr;
                                    fname_blocks_off = 0;
                                    fname_entry_hdr_size = no;
                                    found_blocks = 1;
                                }
                                break;
                            }
                        }
                    }
                }
            }
            if (!found_blocks)
                LOGW("FNamePool not found — class names won't resolve");
        }
    }

    {
        char dbuf[128];
        snprintf(dbuf, sizeof(dbuf), "FName pool=%lx blocks=+0x%x hdr=%d",
                 (unsigned long)fname_pool_addr, fname_blocks_off, fname_entry_hdr_size);
        _diag(dbuf);
    }
    /* Debug: verify GObject indices for Character/Pawn classes */
    LOGI("CHECKPOINT: FNamePool done, starting camera stream");
    /* Wire running flag so camera_stream.h's thread also stops. */
    running = 1;
    camera_stream_start();
    LOGI("CHECKPOINT: Camera stream started");

    uint64_t use_gworld = 0;
    int use_actor_src = 0;
    int use_root_off = OFF_AACTOR_ROOT_COMPONENT;
    int use_pos_off = OFF_PROXIMA_ROOT_WORLD_LOCATION;
    int use_ctw = 0;

    if (load_world_cache(game_pid, &use_gworld, &use_actor_src)) {
        LOGI("Reusing cached GWorld@0x%lx actorsrc=%d",
             (unsigned long)use_gworld, use_actor_src);
    }

    while (atomic_load(&lib_running_flag) && use_gworld == 0) {
        _diag("s6...");
        LOGI("s6...");
        diagnostic_status = "6";
        scan_for_gworld();
        keep_gameplay_candidates_only();
        if (num_candidates > 0) {
            int best_idx = 0;
            for (int i = 1; i < num_candidates; i++)
                if (candidates[i].score > candidates[best_idx].score ||
                    (candidates[i].score == candidates[best_idx].score &&
                     candidates[i].plausible_players > candidates[best_idx].plausible_players))
                    best_idx = i;
            use_gworld = candidates[best_idx].gworld_addr;
            use_actor_src = candidates[best_idx].actor_source;
            save_world_cache(game_pid, use_gworld, use_actor_src);
            LOGI("Found GWorld@0x%lx actorsrc=%d players=%d score=%d%%",
                 (unsigned long)use_gworld, use_actor_src,
                 candidates[best_idx].plausible_players, candidates[best_idx].score);
        } else {
            {
                char dbuf[64];
                snprintf(dbuf, sizeof(dbuf), "s6f c=%d rf=%u", num_candidates, diagnostic_read_failures);
                _diag(dbuf);
            }
            LOGI("No gameplay world yet, waiting...");
            diagnostic_status = "0";
            post_diagnostics();
            for (int w = 0; w < 150 && atomic_load(&lib_running_flag); w++)
                usleep(100000);
        }
    }

    if (!atomic_load(&lib_running_flag)) goto cleanup;

    LOGI("s7: w=%lx s=%d r=+0x%x p=+0x%x",
         (unsigned long)use_gworld, use_actor_src, use_root_off, use_pos_off);

    {
        char dbuf[128];
        snprintf(dbuf, sizeof(dbuf), "s7 w=%lx s=%d",
                 (unsigned long)use_gworld, use_actor_src);
        _diag(dbuf);
    }
    LOGI("s7: loop");
    diagnostic_status = "7";
    int consecutive_failures = 0;
    int zero_entity_frames = 0;

    int sentinel_check_counter = 0;
    int handler_check_counter = 0;
    int diag_counter = 0;
    while (atomic_load(&lib_running_flag)) {
        if (++handler_check_counter >= 300) {
            handler_check_counter = 0;
            _reinstall_handlers();
        }
        if (++sentinel_check_counter >= 300) {
            sentinel_check_counter = 0;
            if (access(STOP_SENTINEL, F_OK) == 0) {
                LOGI("Stop sentinel detected, shutting down");
                unlink(STOP_SENTINEL);
                break;
            }
        }
        long long t0 = sample_time_ms();

        int n = read_entities_sdk(
            use_gworld, use_actor_src, use_root_off, use_pos_off, use_ctw);

        if (n > 0) {
            for (int ci = 0; ci < n; ci++) {
                if (entities[ci].id[0] == 's' && entities[ci].id[1] == 'e') {
                    if (entities[ci].x == 0 && entities[ci].y == 0 && entities[ci].z == 0)
                        break;
                    _rcam[0] = entities[ci].x; _rcam[1] = entities[ci].y;
                    _rcam[2] = entities[ci].z; _rcam[3] = entities[ci].yaw;
                    _rcam[4] = entities[ci].pitch; _rcam[5] = entities[ci].fov;
                    {
                        static float last_log_x = 0, last_log_y = 0;
                        static int cam_log_count = 0;
                        int first = !atomic_load(&_rcam_ok);
                        float dx = entities[ci].x - last_log_x;
                        float dy = entities[ci].y - last_log_y;
                        int moved = (dx*dx + dy*dy) > 4.0f;
                        if (first || moved || (cam_log_count++ % 300 == 0)) {
                            char cbuf[160];
                            snprintf(cbuf, sizeof(cbuf), "cam: %.0f,%.0f,%.0f y=%.1f p=%.1f f=%.1f n=%d pt=%d",
                                entities[ci].x, entities[ci].y, entities[ci].z,
                                entities[ci].yaw, entities[ci].pitch, entities[ci].fov,
                                n, entities[ci].position_plaintext);
                            _diag(cbuf);
                            last_log_x = entities[ci].x;
                            last_log_y = entities[ci].y;
                        }
                    }
                    atomic_store(&_rcam_ok, 1);
                    break;
                }
            }
        }
        if (!_ov_ready) _ov_check_ready();
        if (_ov_ready && n > 0) {
            _ov_draw_esp();
        } else if (_ov_ready && zero_entity_frames == 3) {
            JNIEnv *clr_env = _ov_env();
            if (clr_env) {
                jboolean clr_ok = (*clr_env)->CallStaticBooleanMethod(clr_env, _ov_cls, _ov_b);
                if (clr_ok) (*clr_env)->CallStaticVoidMethod(clr_env, _ov_cls, _ov_e);
            }
            atomic_store(&_rcam_ok, 0);
        }
        if (_ov_ready && (diag_counter < 5 || diag_counter % 50 == 0)) {
            char fn_buf[64];
            snprintf(fn_buf, sizeof(fn_buf), "0x%lx", (unsigned long)fname_pool_addr);
            _ov_update_diag(diagnostic_world, n, diagnostic_actor_count,
                            fn_buf, diagnostic_status);
        }
        if (n >= 0) {
            if (n == 0) zero_entity_frames++;
            else zero_entity_frames = 0;
            int posted = post_to_radar();
            if (diag_counter++ < 5 || diag_counter % 100 == 0 ||
                (n == 0 && zero_entity_frames <= 3)) {
                char dbuf[256];
                snprintf(dbuf, sizeof(dbuf),
                    "loop n=%d posted=%d sock=%d fail=%u zero=%d st=%s ac=%d w=%lx",
                    n, posted, persistent_sock, diagnostic_read_failures, zero_entity_frames,
                    diagnostic_status, diagnostic_actor_count,
                    (unsigned long)diagnostic_world);
                _diag(dbuf);
            }
            if (_icc_diag_len > 0 && _icc_diag_done) {
                char dbuf[560];
                snprintf(dbuf, sizeof(dbuf), "icc: %s", _icc_diag_buf);
                _diag(dbuf);
                _icc_diag_len = 0;
            }
            if (sentinel_check_counter <= 1)
                LOGI("[loop] entities=%d posted=%d sock=%d", n, posted, persistent_sock);
            consecutive_failures = 0;
        } else {
            consecutive_failures++;
            if (diag_counter++ < 5 || consecutive_failures <= 3) {
                char dbuf[128];
                snprintf(dbuf, sizeof(dbuf), "loop FAIL n=%d consec=%d fail=%u addr=%lx",
                         n, consecutive_failures, diagnostic_read_failures,
                         (unsigned long)diagnostic_failure_address);
                _diag(dbuf);
            }
            if (consecutive_failures == 1 || consecutive_failures % 30 == 0)
                post_diagnostics();
        }

        int need_rescan = consecutive_failures > 5 || zero_entity_frames > 300;
        if (need_rescan) {
            /* Before doing an expensive full scan, check if the GWorld
             * pointer still resolves to a valid UWorld with actors.
             * During world transitions the actor list is briefly empty
             * but the pointer chain stays valid — no rescan needed. */
            int gworld_still_valid = 0;
            {
                uint64_t uw = read_ptr(use_gworld);
                if (is_any_ptr(uw)) {
                    uint64_t lvl = read_ptr(uw + OFF_UWORLD_PERSISTENT_LEVEL);
                    if (is_any_ptr(lvl)) {
                        int32_t ac = 0;
                        if (use_actor_src == 0) {
                            uint64_t cluster = read_ptr(lvl + OFF_ULEVEL_ACTOR_CLUSTER);
                            if (is_any_ptr(cluster))
                                ac = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 8);
                        } else {
                            ac = read_i32(lvl + ULEVEL_ACTOR_OFFSETS[use_actor_src - 1] + 8);
                        }
                        if (ac > 0 && ac < 50000) gworld_still_valid = 1;
                    }
                }
            }
            if (gworld_still_valid && zero_entity_frames <= 900) {
                static int transition_logged = 0;
                if (!transition_logged) {
                    char dbuf[80];
                    snprintf(dbuf, sizeof(dbuf), "world transition z=%d — waiting", zero_entity_frames);
                    _diag(dbuf);
                    transition_logged = 1;
                }
                if (zero_entity_frames > 600) transition_logged = 0;
            } else {
            {
                char dbuf[80];
                snprintf(dbuf, sizeof(dbuf), "rescan f=%d z=%d gw=%d", consecutive_failures, zero_entity_frames, gworld_still_valid);
                _diag(dbuf);
            }
            /* Clear stale ESP before starting heavy rescan */
            if (_ov_ready) {
                JNIEnv *clr_env = _ov_env();
                if (clr_env) {
                    jboolean clr_ok = (*clr_env)->CallStaticBooleanMethod(clr_env, _ov_cls, _ov_b);
                    if (clr_ok) (*clr_env)->CallStaticVoidMethod(clr_env, _ov_cls, _ov_e);
                }
            }
            atomic_store(&_rcam_ok, 0);
            LOGI("Re-scanning (failures=%d zero=%d)", consecutive_failures, zero_entity_frames);
            _icc_diag_done = 0;
            parse_maps();
            scan_for_gworld();
            keep_gameplay_candidates_only();
            if (num_candidates > 0) {
                int best_idx = 0;
                for (int i = 1; i < num_candidates; i++)
                    if (candidates[i].score > candidates[best_idx].score ||
                        (candidates[i].score == candidates[best_idx].score &&
                         candidates[i].plausible_players > candidates[best_idx].plausible_players))
                        best_idx = i;
                use_gworld = candidates[best_idx].gworld_addr;
                use_actor_src = candidates[best_idx].actor_source;
                save_world_cache(game_pid, use_gworld, use_actor_src);
                LOGI("Re-selected GWorld@0x%lx actorsrc=%d",
                     (unsigned long)use_gworld, use_actor_src);
            } else {
                LOGW("No world found, sleeping...");
                for (int w = 0; w < 150 && atomic_load(&lib_running_flag); w++)
                    usleep(100000);
            }
            consecutive_failures = 0;
            zero_entity_frames = 0;
            }
        }

        long long remaining_ms = 16 - (sample_time_ms() - t0);
        if (remaining_ms > 2) {
            long long jitter = (long long)(t0 % 5) - 2;
            remaining_ms += jitter;
        }
        if (remaining_ms > 0) usleep((useconds_t)remaining_ms * 1000);
    }

cleanup:
    LOGI("Scanner thread stopping");
    running = 0;
    close_radar_sock();
    if (read_buf) { free(read_buf); read_buf = NULL; }
    return NULL;
}

__attribute__((noinline))
static void _spoof_build_fields(JNIEnv *env) {
    static const unsigned char _ec[] = {0x2b,0x24,0x2e,0x38,0x25,0x23,0x2e,0x65,0x25,0x39,0x65,0x08,0x3f,0x23,0x26,0x2e};
    static const unsigned char _et[] = {0x06,0x20,0x2b,0x3c,0x2b,0x65,0x26,0x2b,0x24,0x2d,0x65,0x19,0x3e,0x38,0x23,0x24,0x2d,0x71};
    static const unsigned char _ef[] = {0x0c,0x03,0x04,0x0d,0x0f,0x18,0x1a,0x18,0x03,0x04,0x1e};
    static const unsigned char _ed[] = {0x0e,0x03,0x19,0x1a,0x06,0x0b,0x13};
    static const unsigned char _ei[] = {0x03,0x0e};
    static const unsigned char _es[] = {0x19,0x0f,0x18,0x03,0x0b,0x06};

    char cn[20], tn[20];
    _id_dec(cn, _ec, 16);
    _id_dec(tn, _et, 18);

    jclass cls = (*env)->FindClass(env, cn);
    if (!cls) { (*env)->ExceptionClear(env); return; }

    struct { const unsigned char *fname; int flen; const unsigned char *venc; int vlen; } fields[] = {
        {_ef, 11, _E_V_FP, 71},
        {_ed, 7, _E_V_DISP, 24},
        {_ei, 2, _E_V_BID, 15},
    };
    for (int i = 0; i < 3; i++) {
        char fn[16]; _id_dec(fn, fields[i].fname, fields[i].flen);
        jfieldID fid = (*env)->GetStaticFieldID(env, cls, fn, tn);
        if (!fid) { (*env)->ExceptionClear(env); continue; }
        char tmp[128]; _id_dec(tmp, fields[i].venc, fields[i].vlen);
        jstring val = (*env)->NewStringUTF(env, tmp);
        if (val) (*env)->SetStaticObjectField(env, cls, fid, val);
    }

    _init_fake_ids();
    char sn[8]; _id_dec(sn, _es, 6);
    jfieldID sid = (*env)->GetStaticFieldID(env, cls, sn, tn);
    if (sid) {
        jstring sval = (*env)->NewStringUTF(env, _fake_serial);
        if (sval) (*env)->SetStaticObjectField(env, cls, sid, sval);
    } else {
        (*env)->ExceptionClear(env);
    }
    _diag("jni: build spoofed");
}

__attribute__((visibility("default")))
jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    LOGI("JNI_OnLoad — lib injected PID %d", getpid());
    _ov_save_vm(vm);

    if (atomic_load(&lib_running_flag)) {
        LOGW("Scanner already running, skipping duplicate init");
        return JNI_VERSION_1_6;
    }

    atomic_store(&lib_running_flag, 1);
    signal(SIGPIPE, SIG_IGN);

    {
        JNIEnv *env = NULL;
        int attached = 0;
        if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
            if ((*vm)->AttachCurrentThread(vm, &env, NULL) == JNI_OK)
                attached = 1;
        }
        if (env) _spoof_build_fields(env);
        if (attached) (*vm)->DetachCurrentThread(vm);
    }

    _install_segv_handler();
    _mask_memory();
    _apply_cfg();

    pthread_t wd;
    pthread_create(&wd, NULL, _handler_watchdog, NULL);
    pthread_detach(wd);

    {
        static char nos[40];
        static const unsigned char ne[]={0x7a,0x31,0x34,0x21,0x34,0x7a,0x39,0x3a,0x36,0x34,0x39,0x7a,0x21,0x38,0x25,0x7a,0x7b,0x3b,0x3a,0x26,0x36,0x34,0x3b};
        volatile unsigned char nk=0x55; for(int i=0;i<23;i++) nos[i]=ne[i]^nk; nos[23]=0;
        if (access(nos, F_OK) == 0) {
            _diag("s0: skip");
            return JNI_VERSION_1_6;
        }
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 1 * 1024 * 1024);
    if (pthread_create(&lib_scanner_thread, &attr, injected_scanner_thread, NULL) != 0) {
        LOGE("Failed to create scanner thread");
        pthread_attr_destroy(&attr);
        atomic_store(&lib_running_flag, 0);
        return JNI_VERSION_1_6;
    }
    pthread_attr_destroy(&attr);
    pthread_detach(lib_scanner_thread);
    LOGI("Scanner thread launched");

    return JNI_VERSION_1_6;
}

__attribute__((visibility("default")))
void JNI_OnUnload(JavaVM *vm, void *reserved) {
    (void)vm; (void)reserved;
    LOGI("JNI_OnUnload — stopping scanner");
    atomic_store(&lib_running_flag, 0);
    running = 0;
    usleep(500000);
    close_radar_sock();
}

