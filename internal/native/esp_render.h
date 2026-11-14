/* GL ESP rendering — included by delta_radar_lib.c */

typedef void *EGLDisplay_;
typedef void *EGLSurface_;
typedef unsigned int EGLBoolean_;
typedef unsigned int GLenum_;
typedef unsigned int GLuint_;
typedef int GLint_;
typedef int GLsizei_;
typedef float GLfloat_;
typedef char GLchar_;
typedef unsigned char GLboolean_;

typedef EGLBoolean_ (*PFN_esb)(EGLDisplay_,EGLSurface_);
typedef GLuint_ (*PFN_gcs)(GLenum_);
typedef void (*PFN_gss)(GLuint_,GLsizei_,const GLchar_**,const GLint_*);
typedef void (*PFN_gcsh)(GLuint_);
typedef void (*PFN_ggsi)(GLuint_,GLenum_,GLint_*);
typedef GLuint_ (*PFN_gcp)(void);
typedef void (*PFN_gas2)(GLuint_,GLuint_);
typedef void (*PFN_glp2)(GLuint_);
typedef void (*PFN_ggpi)(GLuint_,GLenum_,GLint_*);
typedef void (*PFN_gup)(GLuint_);
typedef GLint_ (*PFN_ggal)(GLuint_,const GLchar_*);
typedef GLint_ (*PFN_ggul)(GLuint_,const GLchar_*);
typedef void (*PFN_gu2f)(GLint_,GLfloat_,GLfloat_);
typedef void (*PFN_gu4f)(GLint_,GLfloat_,GLfloat_,GLfloat_,GLfloat_);
typedef void (*PFN_gvap)(GLuint_,GLint_,GLenum_,GLboolean_,GLsizei_,const void*);
typedef void (*PFN_geva)(GLuint_);
typedef void (*PFN_gdva)(GLuint_);
typedef void (*PFN_gda)(GLenum_,GLint_,GLsizei_);
typedef void (*PFN_gen)(GLenum_);
typedef void (*PFN_gdi)(GLenum_);
typedef GLboolean_ (*PFN_gie)(GLenum_);
typedef void (*PFN_gbf)(GLenum_,GLenum_);
typedef void (*PFN_ggi)(GLenum_,GLint_*);
typedef void (*PFN_glw2)(GLfloat_);
typedef void (*PFN_gdsh)(GLuint_);
typedef void (*PFN_gbb)(GLenum_,GLuint_);

static PFN_esb  _o_esb;
static PFN_gcs  _f_cs;  static PFN_gss  _f_ss;  static PFN_gcsh _f_csh;
static PFN_ggsi _f_gsi; static PFN_gcp  _f_cp;  static PFN_gas2 _f_as2;
static PFN_glp2 _f_lp2; static PFN_ggpi _f_gpi; static PFN_gup  _f_up;
static PFN_ggal _f_gal; static PFN_ggul _f_gul;
static PFN_gu2f _f_u2;  static PFN_gu4f _f_u4;  static PFN_gvap _f_vap;
static PFN_geva _f_ea;  static PFN_gdva _f_dva;  static PFN_gda  _f_da;
static PFN_gen  _f_en;  static PFN_gdi  _f_di;   static PFN_gie  _f_ie;
static PFN_gbf  _f_bf;  static PFN_ggi  _f_gi;   static PFN_glw2 _f_lw2;
static PFN_gdsh _f_dsh; static PFN_gbb  _f_bb;

static GLuint_ _ep;
static GLint_  _ea_p, _eu_s, _eu_c;
static int _eok;

__attribute__((noinline))
static void *_gsym(const unsigned char *enc, int len) {
    char buf[32];
    volatile unsigned char k = 0x55;
    for (int i = 0; i < len && i < 31; i++) buf[i] = enc[i] ^ k;
    buf[len] = 0;
    return dlsym(RTLD_DEFAULT, buf);
}

static int _esp_init(void) {
    static const unsigned char _s0[]={0x32,0x39,0x16,0x27,0x30,0x34,0x21,0x30,0x06,0x3d,0x34,0x31,0x30,0x27};
    static const unsigned char _s1[]={0x32,0x39,0x06,0x3d,0x34,0x31,0x30,0x27,0x06,0x3a,0x20,0x27,0x36,0x30};
    static const unsigned char _s2[]={0x32,0x39,0x16,0x3a,0x38,0x25,0x3c,0x39,0x30,0x06,0x3d,0x34,0x31,0x30,0x27};
    static const unsigned char _s3[]={0x32,0x39,0x12,0x30,0x21,0x06,0x3d,0x34,0x31,0x30,0x27,0x3c,0x23};
    static const unsigned char _s4[]={0x32,0x39,0x16,0x27,0x30,0x34,0x21,0x30,0x05,0x27,0x3a,0x32,0x27,0x34,0x38};
    static const unsigned char _s5[]={0x32,0x39,0x14,0x21,0x21,0x34,0x36,0x3d,0x06,0x3d,0x34,0x31,0x30,0x27};
    static const unsigned char _s6[]={0x32,0x39,0x19,0x3c,0x3b,0x3e,0x05,0x27,0x3a,0x32,0x27,0x34,0x38};
    static const unsigned char _s7[]={0x32,0x39,0x12,0x30,0x21,0x05,0x27,0x3a,0x32,0x27,0x34,0x38,0x3c,0x23};
    static const unsigned char _s8[]={0x32,0x39,0x00,0x26,0x30,0x05,0x27,0x3a,0x32,0x27,0x34,0x38};
    static const unsigned char _s9[]={0x32,0x39,0x12,0x30,0x21,0x14,0x21,0x21,0x27,0x3c,0x37,0x19,0x3a,0x36,0x34,0x21,0x3c,0x3a,0x3b};
    static const unsigned char _sA[]={0x32,0x39,0x12,0x30,0x21,0x00,0x3b,0x3c,0x33,0x3a,0x27,0x38,0x19,0x3a,0x36,0x34,0x21,0x3c,0x3a,0x3b};
    static const unsigned char _sB[]={0x32,0x39,0x00,0x3b,0x3c,0x33,0x3a,0x27,0x38,0x67,0x33};
    static const unsigned char _sC[]={0x32,0x39,0x00,0x3b,0x3c,0x33,0x3a,0x27,0x38,0x61,0x33};
    static const unsigned char _sD[]={0x32,0x39,0x03,0x30,0x27,0x21,0x30,0x2d,0x14,0x21,0x21,0x27,0x3c,0x37,0x05,0x3a,0x3c,0x3b,0x21,0x30,0x27};
    static const unsigned char _sE[]={0x32,0x39,0x10,0x3b,0x34,0x37,0x39,0x30,0x03,0x30,0x27,0x21,0x30,0x2d,0x14,0x21,0x21,0x27,0x3c,0x37,0x14,0x27,0x27,0x34,0x2c};
    static const unsigned char _sF[]={0x32,0x39,0x11,0x3c,0x26,0x34,0x37,0x39,0x30,0x03,0x30,0x27,0x21,0x30,0x2d,0x14,0x21,0x21,0x27,0x3c,0x37,0x14,0x27,0x27,0x34,0x2c};
    static const unsigned char _sG[]={0x32,0x39,0x11,0x27,0x34,0x22,0x14,0x27,0x27,0x34,0x2c,0x26};
    static const unsigned char _sH[]={0x32,0x39,0x10,0x3b,0x34,0x37,0x39,0x30};
    static const unsigned char _sI[]={0x32,0x39,0x11,0x3c,0x26,0x34,0x37,0x39,0x30};
    static const unsigned char _sJ[]={0x32,0x39,0x1c,0x26,0x10,0x3b,0x34,0x37,0x39,0x30,0x31};
    static const unsigned char _sK[]={0x32,0x39,0x17,0x39,0x30,0x3b,0x31,0x13,0x20,0x3b,0x36};
    static const unsigned char _sL[]={0x32,0x39,0x12,0x30,0x21,0x1c,0x3b,0x21,0x30,0x32,0x30,0x27,0x23};
    static const unsigned char _sM[]={0x32,0x39,0x19,0x3c,0x3b,0x30,0x02,0x3c,0x31,0x21,0x3d};
    static const unsigned char _sN[]={0x32,0x39,0x11,0x30,0x39,0x30,0x21,0x30,0x06,0x3d,0x34,0x31,0x30,0x27};
    static const unsigned char _sO[]={0x32,0x39,0x17,0x3c,0x3b,0x31,0x17,0x20,0x33,0x33,0x30,0x27};
    _f_cs  = (PFN_gcs)_gsym(_s0,14);
    _f_ss  = (PFN_gss)_gsym(_s1,14);
    _f_csh = (PFN_gcsh)_gsym(_s2,15);
    _f_gsi = (PFN_ggsi)_gsym(_s3,13);
    _f_cp  = (PFN_gcp)_gsym(_s4,15);
    _f_as2 = (PFN_gas2)_gsym(_s5,14);
    _f_lp2 = (PFN_glp2)_gsym(_s6,13);
    _f_gpi = (PFN_ggpi)_gsym(_s7,14);
    _f_up  = (PFN_gup)_gsym(_s8,12);
    _f_gal = (PFN_ggal)_gsym(_s9,19);
    _f_gul = (PFN_ggul)_gsym(_sA,20);
    _f_u2  = (PFN_gu2f)_gsym(_sB,11);
    _f_u4  = (PFN_gu4f)_gsym(_sC,11);
    _f_vap = (PFN_gvap)_gsym(_sD,21);
    _f_ea  = (PFN_geva)_gsym(_sE,25);
    _f_dva = (PFN_gdva)_gsym(_sF,26);
    _f_da  = (PFN_gda)_gsym(_sG,12);
    _f_en  = (PFN_gen)_gsym(_sH,8);
    _f_di  = (PFN_gdi)_gsym(_sI,9);
    _f_ie  = (PFN_gie)_gsym(_sJ,11);
    _f_bf  = (PFN_gbf)_gsym(_sK,11);
    _f_gi  = (PFN_ggi)_gsym(_sL,13);
    _f_lw2 = (PFN_glw2)_gsym(_sM,11);
    _f_dsh = (PFN_gdsh)_gsym(_sN,14);
    _f_bb  = (PFN_gbb)_gsym(_sO,12);
    if (!_f_cs||!_f_ss||!_f_csh||!_f_cp||!_f_up||!_f_da||!_f_vap) return 0;

    const char *vs = "attribute vec2 p;uniform vec2 s;"
        "void main(){gl_Position=vec4(p.x/s.x*2.0-1.0,1.0-p.y/s.y*2.0,0.0,1.0);}";
    const char *fs = "precision mediump float;uniform vec4 c;"
        "void main(){gl_FragColor=c;}";
    GLuint_ v = _f_cs(0x8B31); _f_ss(v,1,&vs,NULL); _f_csh(v);
    GLint_ ok = 0; _f_gsi(v,0x8B81,&ok);
    if (!ok) { _f_dsh(v); _diag("e1: vf"); return 0; }
    GLuint_ f = _f_cs(0x8B30); _f_ss(f,1,&fs,NULL); _f_csh(f);
    _f_gsi(f,0x8B81,&ok);
    if (!ok) { _f_dsh(v); _f_dsh(f); _diag("e1: ff"); return 0; }
    _ep = _f_cp(); _f_as2(_ep,v); _f_as2(_ep,f); _f_lp2(_ep);
    _f_gpi(_ep,0x8B82,&ok); _f_dsh(v); _f_dsh(f);
    if (!ok) { _diag("e1: lf"); return 0; }
    _ea_p = _f_gal(_ep,"p");
    _eu_s = _f_gul(_ep,"s");
    _eu_c = _f_gul(_ep,"c");
    _diag("e1: ok");
    return 1;
}

static int _w2s(float wx, float wy, float wz, float *ox, float *oy, int sw, int sh) {
    float cx = _rcam[0], cy = _rcam[1], cz = _rcam[2];
    float yr = _rcam[3] * 3.14159265f / 180.0f;
    float pr = _rcam[4] * 3.14159265f / 180.0f;
    float fov = _rcam[5];
    if (fov < 5 || fov > 180) fov = 80;
    float dx = wx - cx, dy = wy - cy, dz = wz - cz;
    float cosy = cosf(yr), siny = sinf(yr), cosp = cosf(pr), sinp = sinf(pr);
    float fd = dx * (cosp * cosy) + dy * (cosp * siny) + dz * sinp;
    if (fd <= 0.5f) return 0;
    float rd = dx * siny + dy * (-cosy);
    float ud = dx * (-sinp * cosy) + dy * (-sinp * siny) + dz * cosp;
    float thf = tanf(fov * 3.14159265f / 360.0f);
    float asp = (float)sw / (float)sh;
    *ox = (0.5f + 0.5f * rd / (fd * thf * asp)) * sw;
    *oy = (0.5f - 0.5f * ud / (fd * thf)) * sh;
    return (*ox >= -200 && *ox <= sw + 200 && *oy >= -200 && *oy <= sh + 200);
}

static void _esp_lines(float *v, int n, float r, float g, float b, float a, int sw, int sh) {
    _f_u2(_eu_s, (float)sw, (float)sh);
    _f_u4(_eu_c, r, g, b, a);
    _f_vap((GLuint_)_ea_p, 2, 0x1406, 0, 0, v);
    _f_ea((GLuint_)_ea_p);
    _f_da(0x0001, 0, n);
    _f_dva((GLuint_)_ea_p);
}

static void _esp_box(float x1, float y1, float x2, float y2,
                     float r, float g, float b, float a, int sw, int sh) {
    float v[16] = {x1,y1,x2,y1, x2,y1,x2,y2, x2,y2,x1,y2, x1,y2,x1,y1};
    _esp_lines(v, 8, r, g, b, a, sw, sh);
}

static void _esp_fill(float x1, float y1, float x2, float y2,
                      float r, float g, float b, float a, int sw, int sh) {
    float v[8] = {x1, y1, x1, y2, x2, y1, x2, y2};
    _f_u2(_eu_s, (float)sw, (float)sh);
    _f_u4(_eu_c, r, g, b, a);
    _f_vap((GLuint_)_ea_p, 2, 0x1406, 0, 0, v);
    _f_ea((GLuint_)_ea_p);
    _f_da(0x0005, 0, 4);
    _f_dva((GLuint_)_ea_p);
}

static EGLBoolean_ _hook_esb(EGLDisplay_ dpy, EGLSurface_ sfc) {
    if (!_eok) _eok = _esp_init() ? 1 : -1;
    if (_eok > 0 && atomic_load(&_rcam_ok) && num_entities > 0) {
        GLint_ vp[4]; _f_gi(0x0BA2, vp);
        int sw = vp[2], sh = vp[3];
        if (sw > 100 && sh > 100) {
            GLint_ pp = 0; _f_gi(0x8B8D, &pp);
            GLboolean_ wb = _f_ie(0x0BE2), wd = _f_ie(0x0B71);
            GLboolean_ ws = _f_ie(0x0C11), wc = _f_ie(0x0B44);
            _f_di(0x0B71); _f_di(0x0C11); _f_di(0x0B44);
            _f_en(0x0BE2); _f_bf(0x0302, 0x0303);
            if (_f_bb) _f_bb(0x8889, 0);
            _f_up(_ep);
            if (_f_lw2) _f_lw2(2.0f);

            int self_team = 1, ne = num_entities;
            if (ne > 512) ne = 512;
            for (int i = 0; i < ne; i++)
                if (entities[i].id[0] == 's' && entities[i].id[1] == 'e')
                    { self_team = entities[i].team; break; }

            for (int i = 0; i < ne; i++) {
                Entity *e = &entities[i];
                if (e->id[0] == 's' && e->id[1] == 'e') continue;
                if (e->dead) continue;
                if (e->x == 0 && e->y == 0 && e->z == 0) continue;
                float sx, sy, stx, sty;
                if (!_w2s(e->x, e->y, e->z, &sx, &sy, sw, sh)) continue;
                if (!_w2s(e->x, e->y, e->z + 1.8f, &stx, &sty, sw, sh)) continue;
                float bh = fabsf(sy - sty);
                if (bh < 10) bh = 10;
                if (bh > sh * 0.8f) bh = sh * 0.8f;
                float bw = bh * 0.4f;
                float cr, cg, cb;
                if (e->team == self_team && e->team > 0) { cr = 0; cg = 1; cb = 0; }
                else if (e->team > 0) { cr = 1; cg = 0.2f; cb = 0.2f; }
                else { cr = 1; cg = 1; cb = 0; }
                _esp_box(sx - bw / 2, sty, sx + bw / 2, sy,
                         cr, cg, cb, 0.85f, sw, sh);
                if (e->health_known && e->max_health > 0) {
                    float hp = e->health / e->max_health;
                    if (hp < 0) hp = 0; if (hp > 1) hp = 1;
                    float bl = sx - bw / 2, br = sx + bw / 2, bt = sty - 5;
                    _esp_fill(bl, bt, br, bt + 3, 0, 0, 0, 0.6f, sw, sh);
                    float hcr, hcg;
                    if (hp < 0.5f) { hcr = 1; hcg = hp * 2; }
                    else { hcr = (1 - hp) * 2; hcg = 1; }
                    _esp_fill(bl, bt, bl + (br - bl) * hp, bt + 3,
                              hcr, hcg, 0, 0.9f, sw, sh);
                }
            }
            _f_up(pp);
            if (!wb) _f_di(0x0BE2);
            if (wd)  _f_en(0x0B71);
            if (ws)  _f_en(0x0C11);
            if (wc)  _f_en(0x0B44);
        }
    }
    return _o_esb(dpy, sfc);
}

static void _install_esp_hook(void) {
    if (_o_esb) return;
    char elib[12];
    { static const unsigned char el[] = {0x39,0x3c,0x37,0x10,0x12,0x19,0x7b,0x26,0x3a};
      volatile unsigned char k = 0x55;
      for (int i = 0; i < 9; i++) elib[i] = el[i] ^ k; elib[9] = 0; }
    char ename[16];
    { static const unsigned char en[] = {0x30,0x32,0x39,0x06,0x22,0x34,0x25,0x17,0x20,0x33,0x33,0x30,0x27,0x26};
      volatile unsigned char k = 0x55;
      for (int i = 0; i < 14; i++) ename[i] = en[i] ^ k; ename[14] = 0; }
    void *egl = dlopen(elib, RTLD_NOLOAD | RTLD_NOW);
    if (!egl) { _diag("e0: nl"); return; }
    PFN_esb real = (PFN_esb)dlsym(egl, ename);
    dlclose(egl);
    if (!real) { _diag("e0: nf"); return; }
    FILE *em = fopen("/proc/self/maps", "r");
    if (!em) return;
    char eline[512]; int efound = 0; long eps = sysconf(_SC_PAGESIZE);
    while (fgets(eline, sizeof(eline), em) && !efound) {
        uintptr_t rs, re; char eperms[8] = {0};
        if (sscanf(eline, "%lx-%lx %4s", &rs, &re, eperms) != 3) continue;
        if (rs < ue4_base || rs >= ue4_end) continue;
        if (eperms[1] != 'w') continue;
        for (uintptr_t p = rs; p + 8 <= re; p += 8) {
            if (*(uintptr_t *)p == (uintptr_t)real) {
                uintptr_t pg = p & ~(eps - 1);
                if (syscall(__NR_mprotect, (void *)pg, (size_t)eps,
                            PROT_READ | PROT_WRITE) == 0) {
                    _o_esb = real;
                    *(volatile uintptr_t *)p = (uintptr_t)_hook_esb;
                    syscall(__NR_mprotect, (void *)pg, (size_t)eps, PROT_READ);
                    efound = 1;
                }
                break;
            }
        }
    }
    fclose(em);
    if (!efound) {
        em = fopen("/proc/self/maps", "r");
        if (!em) return;
        while (fgets(eline, sizeof(eline), em) && !efound) {
            uintptr_t rs, re; char eperms[8] = {0};
            if (sscanf(eline, "%lx-%lx %4s", &rs, &re, eperms) != 3) continue;
            if (eperms[1] != 'w' || eperms[0] != 'r') continue;
            if (re - rs > 0x200000) continue;
            for (uintptr_t p = rs; p + 8 <= re; p += 8) {
                if (*(uintptr_t *)p == (uintptr_t)real) {
                    uintptr_t pg = p & ~(eps - 1);
                    if (syscall(__NR_mprotect, (void *)pg, (size_t)eps,
                                PROT_READ | PROT_WRITE) == 0) {
                        _o_esb = real;
                        *(volatile uintptr_t *)p = (uintptr_t)_hook_esb;
                        syscall(__NR_mprotect, (void *)pg, (size_t)eps, PROT_READ);
                        efound = 1;
                    }
                    break;
                }
            }
        }
        fclose(em);
    }
    { char db[32]; snprintf(db, sizeof(db), "e0: h=%d", efound); _diag(db); }
}
