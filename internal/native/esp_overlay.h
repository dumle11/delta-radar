#pragma once
#include <jni.h>
#include <math.h>
#include "overlay_dex.h"

static JavaVM *_ov_jvm;
static jclass _ov_cls;
static jmethodID _ov_z, _ov_b, _ov_r, _ov_f, _ov_l, _ov_t, _ov_e, _ov_gw, _ov_gh, _ov_ok;
static jmethodID _ov_dg, _ov_de, _ov_df, _ov_ds, _ov_dx;
static jmethodID _ov_ge, _ov_gh2, _ov_gd, _ov_gs;
static int _ov_ready;
static int _ov_w, _ov_h;
static int _ov_dex_loaded;

static JNIEnv *_ov_env(void) {
    JNIEnv *env = NULL;
    if (!_ov_jvm) return NULL;
    if ((*_ov_jvm)->GetEnv(_ov_jvm, (void **)&env, JNI_VERSION_1_6) != JNI_OK)
        (*_ov_jvm)->AttachCurrentThread(_ov_jvm, &env, NULL);
    return env;
}

static void _ov_save_vm(JavaVM *vm) { _ov_jvm = vm; }

static int _ov_load_dex(void) {
    JNIEnv *env = _ov_env();
    if (!env) { _diag("ov: no env"); return 0; }

    jclass bb_cls = (*env)->FindClass(env, "java/nio/ByteBuffer");
    if (!bb_cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); _diag("ov: no BB"); return 0;
    }
    jmethodID wrap = (*env)->GetStaticMethodID(env, bb_cls, "wrap",
        "([B)Ljava/nio/ByteBuffer;");
    jbyteArray dex_arr = (*env)->NewByteArray(env, (jsize)_overlay_dex_len);
    (*env)->SetByteArrayRegion(env, dex_arr, 0, (jsize)_overlay_dex_len,
        (const jbyte *)_overlay_dex);
    jobject dex_buf = (*env)->CallStaticObjectMethod(env, bb_cls, wrap, dex_arr);

    jclass cl_cls = (*env)->FindClass(env, "dalvik/system/InMemoryDexClassLoader");
    if (!cl_cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); _diag("ov: no DCL"); return 0;
    }
    jmethodID cl_init = (*env)->GetMethodID(env, cl_cls, "<init>",
        "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");

    /* Get the app's classloader so our DEX can see Android framework classes */
    jclass at_cls = (*env)->FindClass(env, "android/app/ActivityThread");
    jmethodID cat = (*env)->GetStaticMethodID(env, at_cls,
        "currentActivityThread", "()Landroid/app/ActivityThread;");
    jobject at = (*env)->CallStaticObjectMethod(env, at_cls, cat);
    jobject parent_cl = NULL;
    if (at) {
        jmethodID getApp = (*env)->GetMethodID(env, at_cls,
            "getApplication", "()Landroid/app/Application;");
        jobject app = (*env)->CallObjectMethod(env, at, getApp);
        if (app) {
            jclass ctx_cls = (*env)->FindClass(env, "android/content/Context");
            jmethodID getCL = (*env)->GetMethodID(env, ctx_cls,
                "getClassLoader", "()Ljava/lang/ClassLoader;");
            parent_cl = (*env)->CallObjectMethod(env, app, getCL);
        }
    }
    if (!parent_cl) {
        jclass thr_cls = (*env)->FindClass(env, "java/lang/Thread");
        jmethodID ct2 = (*env)->GetStaticMethodID(env, thr_cls,
            "currentThread", "()Ljava/lang/Thread;");
        jobject thr = (*env)->CallStaticObjectMethod(env, thr_cls, ct2);
        jmethodID gcl = (*env)->GetMethodID(env, thr_cls,
            "getContextClassLoader", "()Ljava/lang/ClassLoader;");
        parent_cl = (*env)->CallObjectMethod(env, thr, gcl);
        if (thr) (*env)->DeleteLocalRef(env, thr);
    }
    if (!parent_cl) { _diag("ov: no CL"); return 0; }

    jobject loader = (*env)->NewObject(env, cl_cls, cl_init, dex_buf, parent_cl);
    if (!loader || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); _diag("ov: dcl fail"); return 0;
    }
    jmethodID loadClass = (*env)->GetMethodID(env,
        (*env)->GetObjectClass(env, loader), "loadClass",
        "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring cls_name = (*env)->NewStringUTF(env, "O");
    jobject cls_obj = (*env)->CallObjectMethod(env, loader, loadClass, cls_name);
    if (!cls_obj || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); _diag("ov: cls fail"); return 0;
    }
    _ov_cls = (*env)->NewGlobalRef(env, cls_obj);

    _ov_z  = (*env)->GetStaticMethodID(env, _ov_cls, "z",
        "(Landroid/app/Activity;)V");
    _ov_b  = (*env)->GetStaticMethodID(env, _ov_cls, "b", "()Z");
    _ov_r  = (*env)->GetStaticMethodID(env, _ov_cls, "r", "(FFFFI)V");
    _ov_f  = (*env)->GetStaticMethodID(env, _ov_cls, "f", "(FFFFI)V");
    _ov_l  = (*env)->GetStaticMethodID(env, _ov_cls, "l", "(FFFFIF)V");
    _ov_t  = (*env)->GetStaticMethodID(env, _ov_cls, "t",
        "(Ljava/lang/String;FFI)V");
    _ov_e  = (*env)->GetStaticMethodID(env, _ov_cls, "e", "()V");
    _ov_gw = (*env)->GetStaticMethodID(env, _ov_cls, "gw", "()I");
    _ov_gh = (*env)->GetStaticMethodID(env, _ov_cls, "gh", "()I");
    _ov_ok = (*env)->GetStaticMethodID(env, _ov_cls, "ok", "()Z");

    _ov_dg = (*env)->GetStaticMethodID(env, _ov_cls, "dg", "(Ljava/lang/String;)V");
    _ov_de = (*env)->GetStaticMethodID(env, _ov_cls, "de", "(Ljava/lang/String;)V");
    _ov_df = (*env)->GetStaticMethodID(env, _ov_cls, "df", "(Ljava/lang/String;)V");
    _ov_ds = (*env)->GetStaticMethodID(env, _ov_cls, "ds", "(Ljava/lang/String;)V");
    _ov_dx = (*env)->GetStaticMethodID(env, _ov_cls, "dx", "(Ljava/lang/String;)V");
    _ov_ge = (*env)->GetStaticMethodID(env, _ov_cls, "ge", "()Z");
    _ov_gh2 = (*env)->GetStaticMethodID(env, _ov_cls, "gh2", "()Z");
    _ov_gd = (*env)->GetStaticMethodID(env, _ov_cls, "gd", "()Z");
    _ov_gs = (*env)->GetStaticMethodID(env, _ov_cls, "gs", "()Z");

    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (!_ov_z || !_ov_b || !_ov_r || !_ov_f || !_ov_l || !_ov_t ||
        !_ov_e || !_ov_gw || !_ov_gh || !_ov_ok) {
        _diag("ov: mid fail"); return 0;
    }

    (*env)->DeleteLocalRef(env, dex_arr);
    (*env)->DeleteLocalRef(env, dex_buf);
    (*env)->DeleteLocalRef(env, cls_name);
    (*env)->DeleteLocalRef(env, cls_obj);
    (*env)->DeleteLocalRef(env, loader);
    _ov_dex_loaded = 1;
    _diag("ov: dex ok");
    return 1;
}

static jobject _ov_find_activity(JNIEnv *env) {
    jclass at_cls = (*env)->FindClass(env, "android/app/ActivityThread");
    if (!at_cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); return NULL;
    }
    jmethodID cat = (*env)->GetStaticMethodID(env, at_cls,
        "currentActivityThread", "()Landroid/app/ActivityThread;");
    jobject at = (*env)->CallStaticObjectMethod(env, at_cls, cat);
    if (!at) { _diag("ov: no AT"); return NULL; }

    /* Try mActivities with ArrayMap signature (API 19+) */
    jfieldID mAct = (*env)->GetFieldID(env, at_cls, "mActivities",
        "Landroid/util/ArrayMap;");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);

    jobject map = mAct ? (*env)->GetObjectField(env, at, mAct) : NULL;

    if (!map) {
        _diag("ov: no mAct");
        /* Fallback: use currentApplication then get Activity via reflection */
        jmethodID getApp = (*env)->GetMethodID(env, at_cls,
            "getApplication", "()Landroid/app/Application;");
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        /* As a last resort, try currentApplication() static method */
        return NULL;
    }

    jclass map_cls = (*env)->GetObjectClass(env, map);
    jmethodID size_m = (*env)->GetMethodID(env, map_cls, "size", "()I");
    jint sz = (*env)->CallIntMethod(env, map, size_m);
    if (sz <= 0) { _diag("ov: map empty"); return NULL; }

    {
        char dbuf[32];
        snprintf(dbuf, sizeof(dbuf), "ov: acts=%d", (int)sz);
        _diag(dbuf);
    }

    jmethodID valAt = (*env)->GetMethodID(env, map_cls, "valueAt",
        "(I)Ljava/lang/Object;");
    jobject rec = (*env)->CallObjectMethod(env, map, valAt, 0);
    if (!rec) { _diag("ov: no rec"); return NULL; }

    jclass rec_cls = (*env)->GetObjectClass(env, rec);
    jfieldID act_f = (*env)->GetFieldID(env, rec_cls, "activity",
        "Landroid/app/Activity;");
    if (!act_f || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        _diag("ov: no act field");
        return NULL;
    }
    jobject activity = (*env)->GetObjectField(env, rec, act_f);
    if (!activity) { _diag("ov: act null"); return NULL; }
    _diag("ov: got act");
    return activity;
}

static int _ov_start(void) {
    JNIEnv *env = _ov_env();
    if (!env) return 0;

    if (!_ov_dex_loaded) {
        if (!_ov_load_dex()) return 0;
    }
    if (!_ov_cls) return 0;

    jobject activity = _ov_find_activity(env);
    if (!activity) return 0;

    (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_z, activity);
    if ((*env)->ExceptionCheck(env)) {
        jthrowable ex = (*env)->ExceptionOccurred(env);
        (*env)->ExceptionClear(env);
        if (ex) {
            jclass tc = (*env)->GetObjectClass(env, ex);
            jmethodID gm = (*env)->GetMethodID(env, tc, "getMessage",
                "()Ljava/lang/String;");
            jstring ms = (jstring)(*env)->CallObjectMethod(env, ex, gm);
            if (ms) {
                const char *u = (*env)->GetStringUTFChars(env, ms, NULL);
                if (u) {
                    char dbuf[128];
                    snprintf(dbuf, sizeof(dbuf), "ov: zx %.100s", u);
                    _diag(dbuf);
                    (*env)->ReleaseStringUTFChars(env, ms, u);
                }
                (*env)->DeleteLocalRef(env, ms);
            }
            jmethodID ts = (*env)->GetMethodID(env, tc, "toString",
                "()Ljava/lang/String;");
            jstring tss = (jstring)(*env)->CallObjectMethod(env, ex, ts);
            if (tss) {
                const char *u2 = (*env)->GetStringUTFChars(env, tss, NULL);
                if (u2) {
                    char dbuf[200];
                    snprintf(dbuf, sizeof(dbuf), "ov: zt %.180s", u2);
                    _diag(dbuf);
                    (*env)->ReleaseStringUTFChars(env, tss, u2);
                }
                (*env)->DeleteLocalRef(env, tss);
            }
            (*env)->DeleteLocalRef(env, ex);
            (*env)->DeleteLocalRef(env, tc);
        } else {
            _diag("ov: z fail");
        }
        (*env)->DeleteLocalRef(env, activity);
        return 0;
    }
    (*env)->DeleteLocalRef(env, activity);
    _diag("ov: started");
    return 1;
}

static int _ov_check_ready(void) {
    if (_ov_ready) return 1;
    JNIEnv *env = _ov_env();
    if (!env || !_ov_cls) return 0;
    _ov_ready = (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_ok);
    if (_ov_ready) {
        _ov_w = (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gw);
        _ov_h = (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gh);
        char dbuf[48];
        snprintf(dbuf, sizeof(dbuf), "ov: rdy %dx%d", _ov_w, _ov_h);
        _diag(dbuf);
    }
    return _ov_ready;
}

/* Delete ACE device fingerprint files before game reads them */
static void _ov_wipe_ace_di(void) {
    JNIEnv *env = _ov_env();
    if (!env) return;
    jclass file_cls = (*env)->FindClass(env, "java/io/File");
    if (!file_cls || (*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env); return;
    }
    jmethodID file_init = (*env)->GetMethodID(env, file_cls, "<init>",
        "(Ljava/lang/String;)V");
    jmethodID file_del = (*env)->GetMethodID(env, file_cls, "delete", "()Z");
    jmethodID file_exists = (*env)->GetMethodID(env, file_cls, "exists", "()Z");

    static const char *paths[] = {
        NULL, NULL
    };
    char p0[52], p1[44];
    {
        static const unsigned char e0[] = {
            0x7a,0x31,0x34,0x21,0x34,0x7a,0x31,0x34,0x21,0x34,0x7a,
            0x36,0x3a,0x38,0x7b,0x25,0x27,0x3a,0x2d,0x3c,0x38,0x34,
            0x7b,0x31,0x33,0x38,0x7a,0x33,0x3c,0x39,0x30,0x26,0x7a,
            0x34,0x36,0x30,0x0a,0x26,0x3d,0x30,0x39,0x39,0x0a,0x31,
            0x3c,0x7b,0x31,0x34,0x21
        };
        volatile unsigned char k=0x55;
        for (int i=0;i<49;i++) p0[i]=e0[i]^k; p0[49]=0;
    }
    {
        static const unsigned char e1[] = {
            0x7a,0x31,0x34,0x21,0x34,0x7a,0x31,0x34,0x21,0x34,0x7a,
            0x36,0x3a,0x38,0x7b,0x25,0x27,0x3a,0x2d,0x3c,0x38,0x34,
            0x7b,0x31,0x33,0x38,0x7a,0x33,0x3c,0x39,0x30,0x26,0x7a,
            0x34,0x3b,0x3a,0x0a,0x21,0x38,0x25
        };
        volatile unsigned char k=0x55;
        for (int i=0;i<40;i++) p1[i]=e1[i]^k; p1[40]=0;
    }
    paths[0] = p0;
    paths[1] = p1;

    int deleted = 0;
    for (int i = 0; i < 2; i++) {
        jstring jp = (*env)->NewStringUTF(env, paths[i]);
        jobject fo = (*env)->NewObject(env, file_cls, file_init, jp);
        if (fo && (*env)->CallBooleanMethod(env, fo, file_exists)) {
            if ((*env)->CallBooleanMethod(env, fo, file_del)) deleted++;
        }
        if (fo) (*env)->DeleteLocalRef(env, fo);
        (*env)->DeleteLocalRef(env, jp);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    {
        char dbuf[32];
        snprintf(dbuf, sizeof(dbuf), "e0: h=%d", deleted);
        _diag(dbuf);
    }
}

/* World-to-screen projection */
static int _ov_w2s(float wx, float wy, float wz,
                   float cx, float cy, float cz,
                   float yaw, float pitch, float fov,
                   float *sx, float *sy) {
    float dx = wx - cx, dy = wy - cy, dz = wz - cz;
    float yr = yaw * (float)(M_PI / 180.0);
    float pr = pitch * (float)(M_PI / 180.0);
    float cy_ = cosf(yr), sy_ = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);

    float fwd_x = cp * cy_, fwd_y = cp * sy_, fwd_z = sp;
    float rt_x = -sy_, rt_y = cy_, rt_z = 0;
    float up_x = -sp * cy_, up_y = -sp * sy_, up_z = cp;

    float dot_f = dx * fwd_x + dy * fwd_y + dz * fwd_z;
    if (dot_f < 0.5f) return 0;

    float dot_r = dx * rt_x + dy * rt_y + dz * rt_z;
    float dot_u = dx * up_x + dy * up_y + dz * up_z;

    float half_hfov = fov * 0.5f * (float)(M_PI / 180.0);
    float th = tanf(half_hfov);
    if (th < 0.001f) th = 0.001f;

    float hw = (float)_ov_w * 0.5f;
    float hh = (float)_ov_h * 0.5f;
    /* FoV from camera is horizontal. Derive vertical from aspect ratio. */
    float aspect = hw / hh;
    float tv = th / aspect;
    *sx = hw + (dot_r / (dot_f * th)) * hw;
    *sy = hh - (dot_u / (dot_f * tv)) * hh;
    return (*sx >= -200 && *sx <= _ov_w + 200 &&
            *sy >= -200 && *sy <= _ov_h + 200);
}

static void _ov_update_diag(uint64_t gworld, int n, int ac,
                            const char *fname_info, const char *status) {
    if (!_ov_ready || !_ov_cls || !_ov_dg) return;
    JNIEnv *env = _ov_env();
    if (!env) return;

    char buf[64];
    snprintf(buf, sizeof(buf), "0x%lx", (unsigned long)gworld);
    jstring jgw = (*env)->NewStringUTF(env, buf);
    (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_dg, jgw);
    (*env)->DeleteLocalRef(env, jgw);

    snprintf(buf, sizeof(buf), "%d / %d actors", n, ac);
    jstring jent = (*env)->NewStringUTF(env, buf);
    (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_de, jent);
    (*env)->DeleteLocalRef(env, jent);

    if (fname_info) {
        jstring jfn = (*env)->NewStringUTF(env, fname_info);
        (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_df, jfn);
        (*env)->DeleteLocalRef(env, jfn);
    }
    if (status) {
        jstring jst = (*env)->NewStringUTF(env, status);
        (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_ds, jst);
        (*env)->DeleteLocalRef(env, jst);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

static void _ov_draw_esp(void) {
    if (!_ov_ready || !_ov_cls) return;
    if (!atomic_load(&_rcam_ok)) return;

    JNIEnv *env = _ov_env();
    if (!env) return;

    jboolean esp_on = _ov_ge ? (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_ge) : 1;
    if (!esp_on) return;

    _ov_w = (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gw);
    _ov_h = (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gh);
    if (_ov_w <= 0 || _ov_h <= 0) return;

    jboolean ok = (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_b);
    if (!ok) return;

    jboolean show_health = _ov_gh2 ? (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_gh2) : 1;
    jboolean show_dist = _ov_gd ? (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_gd) : 1;
    jboolean show_snap = _ov_gs ? (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_gs) : 0;

    float cam[6];
    for (int i = 0; i < 6; i++) cam[i] = _rcam[i];

    static int _esp_dbg = 0;
    int drawn = 0;
    for (int i = 0; i < num_entities; i++) {
        Entity *e = &entities[i];
        if (e->id[0] == 's' && e->id[1] == 'e') continue;
        if (e->dead) continue;

        float sx, sy;
        if (!_ov_w2s(e->x, e->y, e->z, cam[0], cam[1], cam[2],
                     cam[3], cam[4], cam[5], &sx, &sy)) {
            if (_esp_dbg < 3) {
                char dbuf[160];
                snprintf(dbuf, sizeof(dbuf), "w2s fail e=(%.0f,%.0f,%.0f) c=(%.0f,%.0f,%.0f) y=%.1f p=%.1f f=%.1f",
                    e->x, e->y, e->z, cam[0], cam[1], cam[2], cam[3], cam[4], cam[5]);
                _diag(dbuf);
                _esp_dbg++;
            }
            continue;
        }

        float dist = sqrtf((e->x - cam[0]) * (e->x - cam[0]) +
                           (e->y - cam[1]) * (e->y - cam[1]) +
                           (e->z - cam[2]) * (e->z - cam[2]));
        if (_esp_dbg < 3) {
            char dbuf[200];
            snprintf(dbuf, sizeof(dbuf), "w2s ok %s e=(%.0f,%.0f,%.0f) c=(%.0f,%.0f,%.0f) y=%.1f p=%.1f f=%.1f sx=%.0f sy=%.0f d=%.0f",
                e->name, e->x, e->y, e->z, cam[0], cam[1], cam[2], cam[3], cam[4], cam[5], sx, sy, dist);
            _diag(dbuf);
            _esp_dbg++;
        }
        float hh_m = e->half_height > 0.1f ? e->half_height : 1.05f;
        float sx_head, sy_head, sx_feet, sy_feet;
        int head_ok = _ov_w2s(e->x, e->y, e->z + hh_m,
                              cam[0], cam[1], cam[2],
                              cam[3], cam[4], cam[5], &sx_head, &sy_head);
        int feet_ok = _ov_w2s(e->x, e->y, e->z - hh_m,
                              cam[0], cam[1], cam[2],
                              cam[3], cam[4], cam[5], &sx_feet, &sy_feet);
        float box_top, box_bot, box_cx, box_h, bw;
        if (head_ok && feet_ok) {
            box_top = sy_head < sy_feet ? sy_head : sy_feet;
            box_bot = sy_head < sy_feet ? sy_feet : sy_head;
            box_cx = (sx_head + sx_feet) * 0.5f;
            box_h = box_bot - box_top;
            if (box_h < 10.0f) box_h = 10.0f;
            if (box_h > 800.0f) box_h = 800.0f;
            box_bot = box_top + box_h;
        } else {
            box_h = 200.0f / (dist + 1.0f) * 1.8f;
            if (box_h < 10.0f) box_h = 10.0f;
            if (box_h > 800.0f) box_h = 800.0f;
            box_top = sy - box_h * 0.5f;
            box_bot = sy + box_h * 0.5f;
            box_cx = sx;
        }
        bw = box_h * 0.45f;

        int color;
        if (e->team == 1)
            color = (int)0xFF00FF00;
        else
            color = (int)0xFFFF4444;

        drawn++;
        /* ESP box from head to feet */
        (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_r,
            box_cx - bw, box_top, box_cx + bw, box_bot, color);

        /* Entity name/type label above box */
        {
            char lbl[96];
            const char *kind_str = e->kind[0] == 'p' ? "PLR" :
                                   e->kind[0] == 'l' ? "LOOT" :
                                   e->kind[0] == 'a' ? "BOT" : "NPC";
            if (e->name[0])
                snprintf(lbl, sizeof(lbl), "%s [%s]", e->name, kind_str);
            else
                snprintf(lbl, sizeof(lbl), "[%s]", kind_str);
            jstring jlbl = (*env)->NewStringUTF(env, lbl);
            (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_t,
                jlbl, box_cx - bw, box_top - 4, (int)0xFFFFCC00);
            (*env)->DeleteLocalRef(env, jlbl);
        }

        /* Snap line from bottom center to entity */
        if (show_snap) {
            (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_l,
                (float)(_ov_w / 2), (float)_ov_h, box_cx, box_bot,
                color, 1.5f);
        }

        /* Health bar */
        if (show_health && e->health_known && e->max_health > 0) {
            float hp = (float)e->health / (float)e->max_health;
            if (hp < 0) hp = 0;
            if (hp > 1) hp = 1;
            float bar_h = box_h * hp;
            int bar_r = (int)((1.0f - hp) * 255);
            int bar_g = (int)(hp * 255);
            int bar_color = (int)(0xFF000000 | (bar_r << 16) | (bar_g << 8));

            (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_f,
                box_cx - bw - 6, box_bot - bar_h, box_cx - bw - 2, box_bot, bar_color);
        }

        /* Distance text */
        if (show_dist && dist < 500.0f) {
            char dbuf[16];
            snprintf(dbuf, sizeof(dbuf), "%.0fm", dist);
            jstring js = (*env)->NewStringUTF(env, dbuf);
            (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_t,
                js, box_cx - 10, box_bot + 16, (int)0xFFFFFFFF);
            (*env)->DeleteLocalRef(env, js);
        }
    }

    if (_esp_dbg < 10 && drawn == 0 && num_entities > 0) {
        char dbuf[64];
        snprintf(dbuf, sizeof(dbuf), "esp: 0 drawn of %d ent", num_entities);
        _diag(dbuf);
        _esp_dbg++;
    }
    (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_e);
}
