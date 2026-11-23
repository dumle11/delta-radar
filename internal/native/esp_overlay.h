#pragma once
#include <jni.h>
#include <math.h>
#include "loot_filter.h"
#include "render_camera.h"
#include "render_projection.h"
static float _ov_cam_roll;
#include "overlay_dex.h"

static JavaVM *_ov_jvm;
static jclass _ov_cls;
static jmethodID _ov_z, _ov_b, _ov_r, _ov_f, _ov_l, _ov_t, _ov_e, _ov_gw, _ov_gh, _ov_ok;
static jmethodID _ov_dg, _ov_de, _ov_df, _ov_ds, _ov_dx;
static jmethodID _ov_ge, _ov_gf, _ov_gr, _ov_gso, _ov_gc;
static jmethodID _ov_gvc, _ov_grd, _ov_dt, _ov_glr, _ov_glm, _ov_glf, _ov_lc;
static jmethodID _ov_circle;
static jmethodID _ov_ga, _ov_gaf, _ov_gas, _ov_gab, _ov_gaa, _ov_gam, _ov_gaz, _ov_gal, _ov_ga3, _ov_gap;
static jmethodID _ov_gnr, _ov_gns, _ov_gcf, _ov_gis;
static jmethodID _ov_gce, _ov_gcv, _ov_gci, _ov_gcb;
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

    JNINativeMethod compositor_methods[]={
        {"submitPixels","(Landroid/graphics/Bitmap;I)V",(void*)vp_bitmap},
        {"compositorActive","()Z",(void*)vp_is_active},
        {"renderEvent","(Ljava/lang/String;)V",(void*)vp_event},
        {"inputRegions","([F)V",(void*)vp_regions}};
    if((*env)->RegisterNatives(env,_ov_cls,compositor_methods,4)!=JNI_OK) {
        (*env)->ExceptionClear(env);_diag("ov: compositor native registration failed");return 0;
    }
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
    _ov_gc = (*env)->GetStaticMethodID(env, _ov_cls, "gc", "(I)I");
    _ov_gvc = (*env)->GetStaticMethodID(env, _ov_cls, "gvc", "(IZ)I");
    _ov_dt = (*env)->GetStaticMethodID(env, _ov_cls, "dt", "(Ljava/lang/String;FFI)V");
    _ov_grd = (*env)->GetStaticMethodID(env, _ov_cls, "grd", "()I");
    _ov_ge = (*env)->GetStaticMethodID(env, _ov_cls, "ge", "()Z");
    _ov_gf = (*env)->GetStaticMethodID(env, _ov_cls, "gf", "(I)I");
    _ov_gr = (*env)->GetStaticMethodID(env, _ov_cls, "gr", "(I)I");
    _ov_gso = (*env)->GetStaticMethodID(env, _ov_cls, "gso", "()I");
    _ov_glr = (*env)->GetStaticMethodID(env, _ov_cls, "glr", "()I");
    _ov_glm = (*env)->GetStaticMethodID(env, _ov_cls, "glm", "()I");
    _ov_glf = (*env)->GetStaticMethodID(env, _ov_cls, "glf", "()I");
    _ov_lc = (*env)->GetStaticMethodID(env, _ov_cls, "lc", "(II)V");
    _ov_circle = (*env)->GetStaticMethodID(env, _ov_cls, "cr", "(FFFI)V");
    _ov_ga = (*env)->GetStaticMethodID(env, _ov_cls, "ga", "()Z");
    _ov_gaf = (*env)->GetStaticMethodID(env, _ov_cls, "gaf", "()I");
    _ov_gas = (*env)->GetStaticMethodID(env, _ov_cls, "gas", "()I");
    _ov_gab = (*env)->GetStaticMethodID(env, _ov_cls, "gab", "()I");
    _ov_gaa = (*env)->GetStaticMethodID(env, _ov_cls, "gaa", "()Z");
    _ov_gaz = (*env)->GetStaticMethodID(env, _ov_cls, "gaz", "()Z");
    _ov_gam = (*env)->GetStaticMethodID(env, _ov_cls, "gam", "()I");
    _ov_gal = (*env)->GetStaticMethodID(env, _ov_cls, "gal", "()Z");
    _ov_ga3 = (*env)->GetStaticMethodID(env, _ov_cls, "ga3", "()Z");
    _ov_gap = (*env)->GetStaticMethodID(env, _ov_cls, "gap", "()I");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    _ov_gnr = (*env)->GetStaticMethodID(env, _ov_cls, "gnr", "()Z");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    _ov_gns = (*env)->GetStaticMethodID(env, _ov_cls, "gns", "()Z");
    _ov_gis = (*env)->GetStaticMethodID(env, _ov_cls, "gis", "()Z");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    _ov_gcf = (*env)->GetStaticMethodID(env, _ov_cls, "gcf", "()I");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    _ov_gce = (*env)->GetStaticMethodID(env, _ov_cls, "gce", "()Z");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    _ov_gcv = (*env)->GetStaticMethodID(env, _ov_cls, "gcv", "()I");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    _ov_gcb = (*env)->GetStaticMethodID(env, _ov_cls, "gcb", "()I");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    _ov_gci = (*env)->GetStaticMethodID(env, _ov_cls, "gci", "()I");
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    if (!_ov_z || !_ov_b || !_ov_r || !_ov_f || !_ov_l || !_ov_t ||
        !_ov_e || !_ov_gw || !_ov_gh || !_ov_ok || !_ov_gvc || !_ov_grd || !_ov_dt) {
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
    atomic_store(&ea_screen_w, _ov_w);
    atomic_store(&ea_screen_h, _ov_h);
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
    int native=render_projection_point(wx,wy,wz,sx,sy);
    if(native>=0) return native;
    float dx = wx - cx, dy = wy - cy, dz = wz - cz;
    float yr = yaw * (float)(M_PI / 180.0);
    float pr = pitch * (float)(M_PI / 180.0);
    float cy_ = cosf(yr), sy_ = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);

    float fwd_x = cp * cy_, fwd_y = cp * sy_, fwd_z = sp;
    float rt_x = -sy_, rt_y = cy_, rt_z = 0;
    float up_x = -sp * cy_, up_y = -sp * sy_, up_z = cp;
    float rr=_ov_cam_roll*(float)(M_PI/180.0),cr=cosf(rr),sr=sinf(rr);
    float old_rt_x=rt_x,old_rt_y=rt_y,old_rt_z=rt_z;
    rt_x=old_rt_x*cr-up_x*sr;rt_y=old_rt_y*cr-up_y*sr;rt_z=old_rt_z*cr-up_z*sr;
    up_x=old_rt_x*sr+up_x*cr;up_y=old_rt_y*sr+up_y*cr;up_z=old_rt_z*sr+up_z*cr;

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
    /* Cull the completed box, not individual head/feet points at screen edges. */
    return isfinite(*sx) && isfinite(*sy);
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
    if (_ov_dx) {
        snprintf(buf, sizeof(buf), "Collision bounds: %d%s; amber = unknown", num_occluders,
            occlusion_incomplete ? " (incomplete)" : "");
        jstring extra = (*env)->NewStringUTF(env, buf);
        (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_dx, extra);
        (*env)->DeleteLocalRef(env, extra);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

/* Entity type indices matching O.java */
#define ET_ENEMY     0
#define ET_TEAMMATE  1
#define ET_NPC       2
#define ET_BOT_PLAYER 3
#define ET_LOOT      4
#define ET_COUNT     5

/* Feature flags matching O.java */
#define EF_ENABLED   1
#define EF_BOXES     2
#define EF_SKELETON  4
#define EF_NAMES     8
#define EF_HEALTH    16
#define EF_DISTANCE  32
#define EF_SNAPLINES 64

static int _ov_entity_type(Entity *e) {
    if (e->kind[0] == 'l') return ET_LOOT;
    if (e->kind[0] == 'a') return e->is_player_bot ? ET_BOT_PLAYER : ET_NPC;
    return e->team == 1 ? ET_TEAMMATE : ET_ENEMY;
}

/* Debug rendering uses our collected collision bounds, never invokes game code. */
static void _ov_debug_text(JNIEnv *env, const char *text, float x, float y, int color) {
    jstring js = (*env)->NewStringUTF(env, text);
    if (js) {
        (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_dt, js, x, y, color);
        (*env)->DeleteLocalRef(env, js);
    }
}
static int _ov_debug_project(const float p[3], const float cam[6], float *x, float *y) {
    return _ov_w2s(p[0],p[1],p[2],cam[0],cam[1],cam[2],cam[3],cam[4],cam[5],x,y);
}
static void _ov_debug_segment(JNIEnv *env, const float from[3], const float to[3],
                               const float cam[6], int color, float width) {
    float a[3], b[3], forward[3];
    float yaw=cam[3]*(float)(M_PI/180), pitch=cam[4]*(float)(M_PI/180);
    forward[0]=cosf(pitch)*cosf(yaw); forward[1]=cosf(pitch)*sinf(yaw); forward[2]=sinf(pitch);
    float da=0,db=0;
    for(int k=0;k<3;k++){a[k]=from[k];b[k]=to[k];da+=(a[k]-cam[k])*forward[k];db+=(b[k]-cam[k])*forward[k];}
    if(da<0.55f && db<0.55f)return;
    if(da<0.55f || db<0.55f){
        float t=(0.55f-da)/(db-da);
        for(int k=0;k<3;k++){float v=a[k]+t*(b[k]-a[k]);if(da<0.55f)a[k]=v;else b[k]=v;}
    }
    float ax,ay,bx,by;
    if(_ov_debug_project(a,cam,&ax,&ay)&&_ov_debug_project(b,cam,&bx,&by))
        (*env)->CallStaticVoidMethod(env,_ov_cls,_ov_l,ax,ay,bx,by,color,width);
}
static void _ov_debug_box(JNIEnv *env, const OccluderAABB *b, const float cam[6]) {
    for(int v=0;v<8;v++)for(int axis=0;axis<3;axis++)if(!(v&(1<<axis))){
        float a[3],z[3];int w=v|(1<<axis);
        for(int k=0;k<3;k++){a[k]=(v&(1<<k))?b->max[k]:b->min[k];z[k]=(w&(1<<k))?b->max[k]:b->min[k];}
        _ov_debug_segment(env,a,z,cam,(int)0xFFFFB300,1.0f);
    }
}
static void _ov_debug_marker(JNIEnv *env,const float point[3],const float cam[6],int color,int link) {
    float x,y;if(!_ov_debug_project(point,cam,&x,&y))return;
    if(link)(*env)->CallStaticVoidMethod(env,_ov_cls,_ov_l,(float)_ov_w*.5f,(float)_ov_h*.5f,x,y,color,1.0f);
    (*env)->CallStaticVoidMethod(env,_ov_cls,_ov_l,x-5,y,x+5,y,color,2.0f);
    (*env)->CallStaticVoidMethod(env,_ov_cls,_ov_l,x,y-5,x,y+5,color,2.0f);
}
static int _ov_debug_name(uint64_t object,char *out,int size) {
    if(!is_telemetry_ptr(object))return 0;
    int n=resolve_fname(read_i32(object+OFF_UOBJECT_NAME),out,size);
    if(n<=0)return 0;
    for(int i=0;i<n;i++)if((unsigned char)out[i]<32 || (unsigned char)out[i]>126){out[0]=0;return 0;}
    return 1;
}
static void _ov_debug_hit_name(int index,char *out,int size) {
    if(index<0){snprintf(out,size,"no collected hit");return;}
    static struct {uint64_t component,actor;long long at;char text[200];} cache[128];
    static unsigned next;
    OccluderAABB *b=&occluders[index];long long now=sample_time_ms();
    for(unsigned i=0;i<128;i++)if(cache[i].component==b->component_addr && cache[i].actor==b->actor_addr && now-cache[i].at<1000){
        snprintf(out,size,"%s",cache[i].text);return;
    }
    char name[72]={0},cls_name[72]={0};
    uint64_t owner=is_telemetry_ptr(b->component_outer)?b->component_outer:b->actor_addr;
    uint64_t cls=read_ptr(owner+OFF_UOBJECT_CLASS);
    if(_ov_debug_name(b->component_addr,name,sizeof(name)) || _ov_debug_name(owner,name,sizeof(name))){
        if(_ov_debug_name(cls,cls_name,sizeof(cls_name)))snprintf(out,size,"%s [%s]",name,cls_name);
        else snprintf(out,size,"%s [class#%X]",name,read_i32(cls+0x24));
    }else snprintf(out,size,"name unavailable: class#%X obj@%llx cmp@%llx",read_i32(cls+0x24),
        (unsigned long long)owner,(unsigned long long)b->component_addr);
    unsigned slot=next++%128;cache[slot].component=b->component_addr;cache[slot].actor=b->actor_addr;
    cache[slot].at=now;snprintf(cache[slot].text,sizeof(cache[slot].text),"%s",out);
}
static void _ov_draw_ray_debug(JNIEnv *env,const float cam[6],int flags) {
    if(!(flags&1))return;
    int nearest[32],count=0;float distances[32];
    for(int i=0;i<num_entities;i++){
        Entity *e=&entities[i];if(e->dead || e->kind[0]=='l' || !strcmp(e->id,self_entity_id))continue;
        float d=0;for(int k=0;k<3;k++){float p=k==0?e->x:k==1?e->y:e->z;d+=(p-cam[k])*(p-cam[k]);}
        if(!isfinite(d)||d>10000)continue;
        int at=count;while(at>0 && distances[at-1]>d)at--;
        if(at>=32)continue;
        int end=count<32?count:31;for(int j=end;j>at;j--){nearest[j]=nearest[j-1];distances[j]=distances[j-1];}
        nearest[at]=i;distances[at]=d;if(count<32)count++;
    }
    char caption[220];snprintf(caption,sizeof(caption),"RAY DEBUG: engine LOS; view rays = bounds estimates | actors %d/32 | bounds %d%s",count,num_occluders,occlusion_incomplete?" INCOMPLETE":"");
    _ov_debug_text(env,caption,20,72,(int)0xFFFFB300);
    snprintf(caption,sizeof(caption),"cam %.2f %.2f %.2f | yaw %.1f pitch %.1f | view cyan / hit red / LOS green / unknown amber",cam[0],cam[1],cam[2],cam[3],cam[4]);
    _ov_debug_text(env,caption,20,96,(int)0xFFFFFFFF);
    int boxes[64],box_count=0;
    if(flags&2){
        float yr=cam[3]*(float)(M_PI/180),pr=cam[4]*(float)(M_PI/180);
        float target[3]={cam[0]+50*cosf(pr)*cosf(yr),cam[1]+50*cosf(pr)*sinf(yr),cam[2]+50*sinf(pr)};
        uint64_t self_actor=0;
        for(int i=0;i<num_entities;i++)if(!strcmp(entities[i].id,self_entity_id))self_actor=entities[i].actor_address;
        BoundsRayHit hit=trace_debug_bounds(cam,target,self_actor,0);char name[200],line[280];
        _ov_debug_hit_name(hit.index,name,sizeof(name));
        snprintf(line,sizeof(line),"LOCAL camera view: %.2fm | inside %d | %s",hit.distance,hit.inside,name);
        int color=hit.index>=0?(int)0xFFFF5555:hit.incomplete?(int)0xFFFFB300:(int)0xFF46D9FF;
        _ov_debug_text(env,line,20,120,color);_ov_debug_marker(env,hit.point,cam,color,0);
        if(hit.index>=0)boxes[box_count++]=hit.index;
    }
    int detail_rows=0;
    for(int i=0;i<count;i++){
        Entity *e=&entities[nearest[i]];float label_point[3]={e->x,e->y,e->z+e->half_height},lx,ly;
        int on_screen=_ov_debug_project(label_point,cam,&lx,&ly)&&lx>=0&&lx<_ov_w&&ly>=0&&ly<_ov_h;
        int detail=on_screen && detail_rows<12;
        float row_y=160.0f+detail_rows*46.0f;
        if(detail)detail_rows++;
        char hit_name[200],label[280];
        if(flags&2){
            float origin[3]={e->x,e->y,e->z},target[3];
            float height=read_float(e->actor_address+0x37C)/100.0f;
            if(!isfinite(height)||height<0||height>e->half_height*1.5f)height=e->half_height*.8f;
            origin[2]+=height;
            float yaw=e->yaw,pitch=e->pitch;const char *source="body";
            uint64_t controller=read_ptr(e->actor_address+OFF_APAWN_CONTROLLER);
            if(is_telemetry_ptr(controller)&&read_ptr(controller+0x3A0)==e->actor_address){
                float rotation[2];
                if(read_mem(controller+OFF_PC_CONTROL_PITCH,rotation,sizeof(rotation))==sizeof(rotation)&&
                   isfinite(rotation[0])&&isfinite(rotation[1])&&fabsf(rotation[0])<10000&&fabsf(rotation[1])<10000){
                    pitch=rotation[0];yaw=rotation[1];source="controller";
                }
            }
            if(isfinite(yaw)&&isfinite(pitch)){
                float yr=yaw*(float)(M_PI/180),pr=pitch*(float)(M_PI/180);
                target[0]=origin[0]+50*cosf(pr)*cosf(yr);target[1]=origin[1]+50*cosf(pr)*sinf(yr);target[2]=origin[2]+50*sinf(pr);
                BoundsRayHit hit=trace_debug_bounds(origin,target,e->actor_address,0);
                int color=hit.index>=0?(int)0xFFFF5555:hit.incomplete?(int)0xFFFFB300:(int)0xFF46D9FF;
                _ov_debug_segment(env,origin,hit.point,cam,color,2.0f);_ov_debug_marker(env,hit.point,cam,color,0);
                _ov_debug_hit_name(hit.index,hit_name,sizeof(hit_name));
                if(detail){snprintf(label,sizeof(label),"%s view(%s) %.1fm: %s",e->name,source,hit.distance,hit_name);_ov_debug_text(env,label,20,row_y,color);}
                if(hit.index>=0 && box_count<64)boxes[box_count++]=hit.index;
            }
        }
        if(flags&4){
#ifdef BUILD_AS_LIBRARY
            char states[6] = {0}; int blocked = 0;
            for (int r = 0; r < 5; r++) {
                int hit = (e->visibility_ray_mask >> r) & 1;
                states[r] = !e->visibility_known ? '?' : hit ? 'X' : 'O';
                blocked += hit;
                float target[3]; visibility_sample_point(cam[0], cam[1], e->x, e->y, e->z, e->half_height, r, target);
                int color = !e->visibility_known ? (int)0xFFFFB300 : hit ? (int)0xFFFF5555 : (int)0xFF00FF00;
                _ov_debug_marker(env, target, cam, color, 1);
            }
            if(detail){
                snprintf(label,sizeof(label),"%s ENGINE LOS %s (%d/5 blocked)%s", e->name, states, blocked,
                    e->visibility_known ? "" : " awaiting fresh query");
                _ov_debug_text(env,label,20,row_y+21,(int)0xFFFFFFFF);
            }
#else
            int blocked=0,inside=0;char states[6]={0};int first=-1;float nearest_hit=1e9;
            for(int r=0;r<5;r++){
                float target[3];visibility_sample_point(cam[0],cam[1],e->x,e->y,e->z,e->half_height,r,target);
                BoundsRayHit hit=trace_debug_bounds(cam,target,0,e->actor_address);
                states[r]=hit.index>=0?'X':hit.incomplete?'?':'O';blocked+=hit.index>=0;inside+=hit.inside;
                int color=hit.index>=0?(int)0xFFFF5555:hit.incomplete?(int)0xFFFFB300:(int)0xFF00FF00;
                _ov_debug_marker(env,hit.point,cam,color,1);
                if(hit.index>=0 && hit.distance<nearest_hit){first=hit.index;nearest_hit=hit.distance;}
            }
            _ov_debug_hit_name(first,hit_name,sizeof(hit_name));
            if(detail){snprintf(label,sizeof(label),"%s LOS %s (%d/5 blocked; inside %d): %s",e->name,states,blocked,inside,hit_name);_ov_debug_text(env,label,20,row_y+21,(int)0xFFFFFFFF);}
            if(first>=0 && box_count<64)boxes[box_count++]=first;
#endif
        }
    }
    snprintf(caption,sizeof(caption),"Details: %d nearest on-screen actors | LOS order: centre / upper / lower / side / side | X hit, O clear, ? unknown",detail_rows);
    _ov_debug_text(env,caption,20,140,(int)0xFFFFFFFF);
    if(flags&8)for(int i=0;i<box_count;i++){
        int duplicate=0;for(int j=0;j<i;j++)if(boxes[j]==boxes[i])duplicate=1;
        if(!duplicate)_ov_debug_box(env,&occluders[boxes[i]],cam);
    }
}

static unsigned _ov_aim_config(void) {
    if (!_ov_ready || !_ov_cls) return 0;
    JNIEnv *env = _ov_env(); if (!env) return 0;
    int enabled = (*env)->CallStaticBooleanMethod(env,_ov_cls,_ov_ga);
    int fov = (*env)->CallStaticIntMethod(env,_ov_cls,_ov_gaf);
    int bone = (*env)->CallStaticIntMethod(env,_ov_cls,_ov_gab);
    int mask = (*env)->CallStaticIntMethod(env,_ov_cls,_ov_gam) & 7;
    int autoshoot = _ov_gaa ? (*env)->CallStaticBooleanMethod(env,_ov_cls,_ov_gaa) : 0;
    int autozoom = _ov_gaz ? (*env)->CallStaticBooleanMethod(env,_ov_cls,_ov_gaz) : 0;
    int aimlocked = _ov_gal ? (*env)->CallStaticBooleanMethod(env,_ov_cls,_ov_gal) : 1;
    int aim360 = _ov_ga3 ? (*env)->CallStaticBooleanMethod(env,_ov_cls,_ov_ga3) : 0;
    int priority = _ov_gap ? (*env)->CallStaticIntMethod(env,_ov_cls,_ov_gap) : 0;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); return 0; }
    if (fov<5) fov=5; if (fov>45) fov=45;
    if (bone<0 || bone>2) bone=2;
    if (priority<0 || priority>3) priority=0;
    return (enabled?1u:0u) | ((unsigned)mask<<1) | ((unsigned)fov<<8) | ((unsigned)bone<<16) | (autoshoot?1u<<24:0u) | (autozoom?1u<<25:0u) | (aimlocked?1u<<26:0u) | (aim360?1u<<27:0u) | ((unsigned)priority<<28);
}
static _Atomic int _ov_camera_fov_override;

static void _ov_misc_config(void) {
    if (!_ov_ready || !_ov_cls) { atomic_store(&ew_no_recoil, 0); atomic_store(&ew_no_spread, 0); atomic_store(&_ov_camera_fov_override, 0); atomic_store(&es_enabled, 0); return; }
    JNIEnv *env = _ov_env(); if (!env) return;
    int nr = _ov_gnr ? (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_gnr) : 0;
    int ns = _ov_gns ? (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_gns) : 0;
    int stamina = _ov_gis ? (*env)->CallStaticBooleanMethod(env,_ov_cls,_ov_gis) : 0;
    int cf = _ov_gcf ? (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gcf) : 0;
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionClear(env); nr = ns = cf = stamina = 0; }
    atomic_store(&ew_no_recoil, nr);
    atomic_store(&ew_no_spread, ns);
    atomic_store(&_ov_camera_fov_override, cf);
    atomic_store(&es_enabled, stamina);
    int ray_flags=_ov_grd?(*env)->CallStaticIntMethod(env,_ov_cls,_ov_grd):0;
    atomic_store(&internal_collect_occluders,(ray_flags&1)!=0);
}

static void _ov_esp_render_config(void) {
    if (!_ov_ready || !_ov_cls) return;
    JNIEnv *env = _ov_env(); if (!env) return;
    int esp_on = _ov_ge ? (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_ge) : 1;
    atomic_store(&_er_esp_on, esp_on);
    atomic_store(&_er_snap_origin, _ov_gso ? (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gso) : 0);
    for (int t = 0; t < ER_ET_COUNT; t++) {
        atomic_store(&_er_feat[t], _ov_gf ? (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gf, t) : 0xFF);
        atomic_store(&_er_rdist[t], _ov_gr ? (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gr, t) : 500);
        for (int v = 0; v < 2; v++)
            atomic_store(&_er_palette[t][v], (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gvc, t, (jboolean)v));
    }
    int chams = _ov_gce ? (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_gce) : 0;
    atomic_store(&_er_chams_enabled, chams);
    if (chams) {
        unsigned vis = _ov_gcv ? (unsigned)(*env)->CallStaticIntMethod(env, _ov_cls, _ov_gcv) : 0xFF00FF00u;
        unsigned inv = _ov_gci ? (unsigned)(*env)->CallStaticIntMethod(env, _ov_cls, _ov_gci) : 0xFFFF0000u;
        atomic_store(&_er_chams_brightness, _ov_gcb ? (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gcb) : 70);
        atomic_store(&_er_chams_vis, vis);
        atomic_store(&_er_chams_inv, inv);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
}

static void _ov_draw_aim(JNIEnv *env, const float cam[6]) {
    unsigned config = atomic_load(&ea_config);
    if (config & 1) {
        float radius = aim_radius_pixels((float)_ov_w, cam[5], (config >> 8) & 255);
        unsigned status = atomic_load(&ea_status);
        int color = status == 2 ? (int)0xFF00FF00 : (int)0xDDFFFFFF;
        if (radius > 0) (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_circle,
            _ov_w * 0.5f, _ov_h * 0.5f, radius, color);
        const char *text = status == 2 ? "Aim: locked" :
            status == 3 ? "Aim: unavailable" : "Aim: searching";
        _ov_debug_text(env, text, 20, (float)_ov_h - 28, color);
    }
    if (config & (1u << 24)) {
        unsigned status = atomic_load(&et_status);
        const char *text = status == 2 ? "Trigger: firing" :
            status == 3 ? "Trigger: unavailable" : status == 4 ? "Trigger: zooming" : "Trigger: ready";
        _ov_debug_text(env, text, 20, (float)_ov_h - ((config & 1) ? 56 : 28),
            status == 2 ? (int)0xFF00FF00 : (int)0xDDFFFFFF);
    }
}

/* Current-frame failures only; successful SDK/software recovery is silent. */
static void _ov_draw_position_status(JNIEnv *env) {
    if (!position_frame_failures || !camera_frame_valid || !local_player_controlled) return;
    char message[96];
    snprintf(message,sizeof(message),"Position recovery failed: %u %s",position_frame_failures,
        position_frame_failures==1 ? "entity" : "entities");
    jstring text=(*env)->NewStringUTF(env,message);
    if (!text) return;
    (*env)->CallStaticVoidMethod(env,_ov_cls,_ov_dt ? _ov_dt : _ov_t,
        text,16.0f,(float)_ov_h-26.0f,(jint)0xffffbf47);
    (*env)->DeleteLocalRef(env,text);
}

static void _ov_draw_esp(void) {
    if (!_ov_ready || !_ov_cls) return;

    JNIEnv *env = _ov_env();
    if (!env) return;

    jboolean esp_on = _ov_ge ? (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_ge) : 1;
    int camera_ready=atomic_load(&_rcam_ok);
    if (!camera_ready && !(position_frame_failures && camera_frame_valid)) return;
    jboolean ok = (*env)->CallStaticBooleanMethod(env, _ov_cls, _ov_b);
    if (!ok) return;

    _ov_w = (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gw);
    _ov_h = (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gh);
    atomic_store(&ea_screen_w, _ov_w);
    atomic_store(&ea_screen_h, _ov_h);
    if (_ov_w <= 0 || _ov_h <= 0) {
        (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_e);
        return;
    }

    _ov_draw_position_status(env);
    if (!camera_ready) { (*env)->CallStaticVoidMethod(env,_ov_cls,_ov_e); return; }

    int feat[ET_COUNT], rdist[ET_COUNT], palette[ET_COUNT][2];
    for (int t = 0; t < ET_COUNT; t++) {
        feat[t] = _ov_gf ? (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gf, t) : 0xFF;
        rdist[t] = _ov_gr ? (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gr, t) : 500;
        for (int visible = 0; visible < 2; visible++)
            palette[t][visible] = (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gvc,
                t, (jboolean)visible);
    }
    unsigned loot_mask = _ov_glr ? (unsigned)(*env)->CallStaticIntMethod(env,_ov_cls,_ov_glr) & 255u : 255u;
    int loot_minimum = _ov_glm ? (*env)->CallStaticIntMethod(env,_ov_cls,_ov_glm) : 0;
    int loot_flags = _ov_glf ? (*env)->CallStaticIntMethod(env,_ov_cls,_ov_glf) : 3;
    static uint32_t loot_published[8];
    for (int q=1;q<8;q++) if (_ov_lc && el_color_known[q] && loot_published[q] != el_colors[q]) {
        (*env)->CallStaticVoidMethod(env,_ov_cls,_ov_lc,q,(jint)el_colors[q]);
        loot_published[q] = el_colors[q];
    }
    int snap_origin = _ov_gso ? (*env)->CallStaticIntMethod(env, _ov_cls, _ov_gso) : 0;

    float cam[6];
    for (int i = 0; i < 6; i++) cam[i] = _rcam[i];
    _ov_cam_roll=0;render_camera_snapshot(cam,&_ov_cam_roll);
    render_projection_begin(_ov_w,_ov_h,cam);

    _ov_draw_aim(env,cam);
    if (!esp_on) { (*env)->CallStaticVoidMethod(env,_ov_cls,_ov_e); return; }

    static int _esp_dbg = 0;
    static int _esp_frame = 0;
    _esp_frame++;
    int drawn = 0, skipped_dead = 0, skipped_type = 0, skipped_dist = 0, skipped_w2s = 0, skipped_box = 0;
    uint64_t loot_group_actor[MAX_ACTORS]={0};int loot_group_lines[MAX_ACTORS]={0},loot_groups=0;
    for (int i = 0; i < num_entities; i++) {
        Entity *e = &entities[i];
        if (strcmp(e->id, self_entity_id) == 0) continue;
        if (e->dead) { skipped_dead++; continue; }

        int etype = _ov_entity_type(e);
        int ef = feat[etype];
        if (!(ef & EF_ENABLED)) { skipped_type++; continue; }

        float dist = sqrtf((e->x - cam[0]) * (e->x - cam[0]) +
                           (e->y - cam[1]) * (e->y - cam[1]) +
                           (e->z - cam[2]) * (e->z - cam[2]));
        if (dist > (float)rdist[etype]) { skipped_dist++; continue; }

        float sx, sy;
        if (!_ov_w2s(e->x, e->y, e->z, cam[0], cam[1], cam[2],
                     cam[3], cam[4], cam[5], &sx, &sy)) {
            skipped_w2s++;
            if (_esp_dbg < 3) {
                char dbuf[160];
                snprintf(dbuf, sizeof(dbuf), "w2s fail e=(%.0f,%.0f,%.0f) c=(%.0f,%.0f,%.0f) y=%.1f p=%.1f f=%.1f",
                    e->x, e->y, e->z, cam[0], cam[1], cam[2], cam[3], cam[4], cam[5]);
                _diag(dbuf);
                _esp_dbg++;
            }
            continue;
        }

        if (etype == ET_LOOT) {
            if (!loot_matches(e->loot_quality,e->loot_metadata_known,e->loot_unit_value,e->loot_value_known,
                e->loot_kind != LOOT_ITEM || e->loot_in_container,loot_mask,loot_minimum > 0 ? (uint64_t)loot_minimum : 0,
                (loot_flags & 2) != 0,dist,(float)rdist[ET_LOOT])) continue;
            int color = e->loot_color_known ? (int)e->loot_color : (int)0xffdedede;
            int line=e->loot_line;
            if(e->loot_in_container) {
                int group=0;while(group<loot_groups && loot_group_actor[group]!=e->actor_address) ++group;
                if(group==loot_groups && loot_groups<MAX_ACTORS) loot_group_actor[loot_groups++]=e->actor_address;
                if(group<MAX_ACTORS) line=++loot_group_lines[group];
            }
            sy+=(float)line*38;
            if (sx < -20 || sx > _ov_w+20 || sy < -20 || sy > _ov_h+20) continue;
            ++drawn;
            if (ef & EF_BOXES) (*env)->CallStaticVoidMethod(env,_ov_cls,_ov_r,sx-3,sy-3,sx+3,sy+3,color);
            if (ef & EF_NAMES) {
                char label[300];
                if (e->loot_stack > 1) snprintf(label,sizeof(label),"%s ×%d",e->loot_name,e->loot_stack);
                else snprintf(label,sizeof(label),"%s",e->loot_name);
                _ov_debug_text(env,label,sx+8,sy-7,color);
            }
            char details[100] = "";
            if ((loot_flags & 1) && e->loot_value_known)
                snprintf(details,sizeof(details),"%llu ref./item",(unsigned long long)e->loot_unit_value);
            if (ef & EF_DISTANCE) {
                size_t length = strlen(details);
                snprintf(details+length,sizeof(details)-length,"%s%.0fm",length?" · ":"",dist);
            }
            if (details[0]) _ov_debug_text(env,details,sx+8,sy+16,color);
            continue;
        }

        if (_esp_dbg < 3) {
            char dbuf[200];
            snprintf(dbuf, sizeof(dbuf), "w2s ok %s e=(%.0f,%.0f,%.0f) sx=%.0f sy=%.0f d=%.0f et=%d",
                e->name, e->x, e->y, e->z, sx, sy, dist, etype);
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
            box_bot = box_top + box_h;
        } else {
            skipped_box++; continue;
        }
        bw = box_h * 0.45f * 0.5f;
        int box_visible = !(box_cx + bw < 0 || box_cx - bw > _ov_w ||
            box_bot < 0 || box_top > _ov_h);
        if (!box_visible && !(ef & EF_SKELETON)) continue;

        int color = e->visibility_known ? palette[etype][e->visible_from_camera != 0] : (int)0xFFFFB300;
        drawn++;
        if ((ef & EF_BOXES) && box_visible)
            (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_r,
                box_cx - bw, box_top, box_cx + bw, box_bot, color);

        if ((ef & EF_SKELETON) && e->has_bones && e->bone_count >= 2 &&
            e->bone_count <= MAX_SKELETON_BONES) {
            for (int joint = 0; joint < e->bone_count; joint++) {
                int parent = e->bone_parent[joint];
                if (parent < 0 || parent >= joint) continue;
                float ax, ay, bx_, by_;
                if (!_ov_w2s(e->bone_pos[joint][0], e->bone_pos[joint][1], e->bone_pos[joint][2],
                    cam[0], cam[1], cam[2], cam[3], cam[4], cam[5], &ax, &ay) ||
                    !_ov_w2s(e->bone_pos[parent][0], e->bone_pos[parent][1], e->bone_pos[parent][2],
                    cam[0], cam[1], cam[2], cam[3], cam[4], cam[5], &bx_, &by_)) continue;
                (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_l, ax, ay, bx_, by_, color, 2.0f);
            }
        }

        if (!box_visible) continue;

        if (ef & EF_NAMES) {
            char lbl[96];
            const char *kind_str = e->kind[0] == 'p' ? "PLR" :
                                   e->kind[0] == 'l' ? "LOOT" :
                                   e->is_player_bot ? "BOT PLAYER" : "NPC";
            if (e->name[0])
                snprintf(lbl, sizeof(lbl), "%s [%s]", e->name, kind_str);
            else
                snprintf(lbl, sizeof(lbl), "[%s]", kind_str);
            jstring jlbl = (*env)->NewStringUTF(env, lbl);
            (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_t,
                jlbl, box_cx - bw, box_top - 4, color);
            (*env)->DeleteLocalRef(env, jlbl);
        }

        if (ef & EF_SNAPLINES) {
            float snap_y = snap_origin ? 0.0f : (float)_ov_h;
            (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_l,
                (float)(_ov_w / 2), snap_y, box_cx, snap_origin ? box_top : box_bot,
                color, 1.5f);
        }

        if ((ef & EF_HEALTH) && e->health_known && e->max_health > 0) {
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

        if ((ef & EF_DISTANCE) && dist < (float)rdist[etype]) {
            char dbuf[16];
            snprintf(dbuf, sizeof(dbuf), "%.0fm", dist);
            jstring js = (*env)->NewStringUTF(env, dbuf);
            (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_t,
                js, box_cx - 10, box_bot + 16, color);
            (*env)->DeleteLocalRef(env, js);
        }
    }

    int ray_flags = (*env)->CallStaticIntMethod(env, _ov_cls, _ov_grd);
    if (ray_flags & 1) _ov_draw_ray_debug(env, cam, ray_flags);

    if ((_esp_frame <= 5 || _esp_frame % 200 == 0) && num_entities > 0) {
        char dbuf[200];
        snprintf(dbuf, sizeof(dbuf), "esp f%d: %d drawn of %d ent (dead=%d type=%d dist=%d w2s=%d box=%d) cam=(%.0f,%.0f,%.0f) fov=%.0f",
            _esp_frame, drawn, num_entities, skipped_dead, skipped_type, skipped_dist, skipped_w2s, skipped_box,
            cam[0], cam[1], cam[2], cam[5]);
        _diag(dbuf);
    }
    (*env)->CallStaticVoidMethod(env, _ov_cls, _ov_e);
}
