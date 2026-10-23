/*
 * codm_tex - Zygisk module for Call of Duty: Mobile
 *
 * Installs PLT hooks (LSPlt) on the Unity renderer (libunity.so) to:
 *   - change texture sampling (Pixel Blocks, Detail Cut, Flat, True Pixelated)
 *   - lower the render resolution (render scale) and cap the frame rate
 *   - record frame-time, freeze, thread and CPU-core diagnostics
 *
 * Settings are plain text files in the game's data directory (see the *F
 * macros below). They are read once at launch and are edited through the
 * WebUI in webroot/ (SukiSU Ultra / KernelSU).
 */
#include <cstdarg>
#include <cerrno>
#include <jni.h>
#include <dlfcn.h>
#include <unistd.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <dirent.h>
#include <fcntl.h>
#include <sched.h>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <EGL/egl.h>
#include <android/native_window.h>
#include <android/dlext.h>
#include <android/log.h>
#include <sys/stat.h>
#include <GLES3/gl3.h>
#include "zygisk.hpp"
#include "lsplt.hpp"

#ifndef RUSAGE_THREAD
#define RUSAGE_THREAD 1
#endif

/* One place for the version. Keep in sync with module.prop (bump +1 every update). */
#define VERSION "v0.44.0"
#define PKG "com.garena.game.codm"
/* v0.40: the settings folder is chosen at run time. A clone (Dual Apps, work profile, user 999) keeps its data
   under /data/user/<id>/<pkg>, which the old hard-coded /data/data/<pkg> path does not reach. */
#define DEFAULT_DIR "/data/data/" PKG "/files/"
static char g_dir[320] = DEFAULT_DIR;
static char g_app_dir[256] = "";            /* app_data_dir given by Zygisk in preAppSpecialize (without trailing slash) */
static const char *P(const char *name) {    /* ring of buffers so rename(P(a), P(b)) is safe */
    static thread_local char buf[8][400];
    static thread_local unsigned idx = 0;
    char *b = buf[idx++ & 7];
    snprintf(b, 400, "%s%s", g_dir, name);
    return b;
}
static void set_dir(const char *app_data_dir) {
    if (app_data_dir && app_data_dir[0] == '/' && strlen(app_data_dir) < 240) {
        char cand[320];
        size_t n = strlen(app_data_dir);
        while (n > 1 && app_data_dir[n - 1] == '/') n--;
        snprintf(cand, sizeof cand, "%.*s/files/", (int)n, app_data_dir);
        if (access(cand, W_OK) == 0) { snprintf(g_dir, sizeof g_dir, "%s", cand); return; }
        if (access(DEFAULT_DIR, W_OK) == 0) { snprintf(g_dir, sizeof g_dir, "%s", DEFAULT_DIR); return; }
        snprintf(g_dir, sizeof g_dir, "%s", cand);      /* neither is writable: report the one the game really owns */
        return;
    }
    snprintf(g_dir, sizeof g_dir, "%s", DEFAULT_DIR);
}
#define LOGF P("codm_tex.log")
#define CFGF P("bias.txt")
#define FPSF P("fps.txt")
#define SCLF P("scale.txt")
#define MODEF P("mode.txt")
#define FZF P("fz.txt")
#define THF P("th.txt")
#define COREF P("core.txt")
#define NEARF P("near.txt")
#define BLKF P("blk.txt")
#define MINF P("minsz.txt")
#define DLYF P("delay.txt")
#define SKIPF P("skip.txt")
#define BLOFFF P("bltoff.txt")
#define STYF P("style.txt")
#define SEDF P("sedge.txt")
#define FLATF P("flat.txt")
#define TTEXF P("testtex.txt")
#define TTSZF P("ttsize.txt")
#define TTSMF P("ttsm.txt")
#define ALPHAF P("alpha.txt")       /* v0.44: 1 = alpha-channel textures (smoke, dust, muzzle flash, leaves) keep their soft edge (default 1) */
#define GRASSF P("grass.txt")       /* v0.44: "format size" lines; a matching alpha texture is uploaded fully transparent (Remove grass) */
#define HDLYF P("hookdelay.txt")
#define FGLF P("forcegl.txt")       /* v0.41: 1 = force OpenGL ES: Vulkan is hidden from libunity so Unity falls back to GLES (default 1) */
#define HSAFEF P("hooksafe.txt")      /* v0.40: 1 = hooks skipped (the previous launch died while hooking); the WebUI resets it to 0 */
#define STATF P("codm_tex.status")   /* small key=value file the WebUI reads: how far the module got (written by the game process) */
#define FULLF P("fullsize.txt")      /* largest landscape window size ever seen (so a window shrunk by something else is never taken as "native") */
#define SCRASHF P("scale_crash.txt")   /* marker: written before render scale is applied, cleared after 120 good frames */

static std::mutex mu;
static FILE *lf = nullptr;
static long lf_bytes = 0;
#define LOG_CAP_BYTES (2L * 1024 * 1024)
static void L(const char *fmt, ...) {
    std::lock_guard<std::mutex> g(mu);
    if (lf && lf_bytes > LOG_CAP_BYTES) {          /* keep shared reports small: start the log over */
        fclose(lf); lf = fopen(LOGF, "w"); lf_bytes = 0;
        if (lf) fputs("=== log truncated (2 MB cap) ===\n", lf);
    }
    if (!lf) { lf = fopen(LOGF, "a"); if (lf) { fseek(lf, 0, SEEK_END); lf_bytes = ftell(lf); } }
    if (!lf && access(LOGF, F_OK) == 0) {          /* file exists but the game cannot append (e.g. created by root): replace it, the folder is ours */
        unlink(LOGF);
        lf = fopen(LOGF, "a"); lf_bytes = 0;
        if (lf) fputs("=== log file was not writable by the game; recreated ===\n", lf);
    }
    va_list a; va_start(a, fmt);
    if (!lf) {                                     /* log file unavailable: make it visible in logcat instead */
        __android_log_vprint(ANDROID_LOG_INFO, "codm_tex", fmt, a);
        va_end(a);
        return;
    }
    int n = vfprintf(lf, fmt, a);
    va_end(a);
    if (n > 0) lf_bytes += n;
    fflush(lf);
}

/* ---------- Status file (codm_tex.status) ----------
   The log can be missing, truncated or unreadable; this tiny file is rewritten atomically
   (tmp + rename) at every stage so the WebUI can tell "module never ran" from "ran, 0 applied". */
static std::mutex st_mu;
static std::map<std::string, std::string> st_kv;
static std::atomic<int> g_hook_state{0};   /* 0 = no hooks yet, 1 = CommitHook ok, 2 = CommitHook failed */
static void st_set(const char *k, const std::string &v) {
    std::lock_guard<std::mutex> g(st_mu);
    st_kv[k] = v;
    st_kv["updated"] = std::to_string((long long)time(nullptr));
    const std::string tmp = std::string(STATF) + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (!f) { unlink(tmp.c_str()); f = fopen(tmp.c_str(), "w"); }   /* leftover root-owned .tmp: the folder is the game's, replace it */
    if (!f) {
        static std::atomic<int> warned{0};
        if (!warned.exchange(1)) __android_log_print(ANDROID_LOG_WARN, "codm_tex", "%s status file not writable errno=%d", VERSION, errno);
        return;
    }
    for (auto &kv : st_kv) fprintf(f, "%s=%s\n", kv.first.c_str(), kv.second.c_str());
    fclose(f);
    chmod(tmp.c_str(), 0666);
    rename(tmp.c_str(), STATF);
}
static void st_seti(const char *k, long long v) { st_set(k, std::to_string(v)); }

/* ---------- Original-function safety net (v0.37) ----------
   Every hook calls the "original" through a pointer that LSPlt (or wrap()) fills in. If that pointer is
   still null, or points back at our own hook (another injector chained through us), the old code either
   dropped the GL/EGL call or recursed. orig_or() resolves the real system function instead and caches it. */
static void *real_sym(const char *n) {
    void *p = dlsym(RTLD_DEFAULT, n);
    if (p) return p;
    static std::atomic<int> misses{0};
    if (misses.load() >= 64) return nullptr;      /* symbol really is not there: stop probing on every GL call */
    misses++;
    static const char *libs[] = {"libEGL.so", "libGLESv3.so", "libGLESv2.so", "libandroid.so"};
    for (const char *l : libs) {
        void *h = dlopen(l, RTLD_NOW);
        if (h && (p = dlsym(h, n))) return p;
    }
    return nullptr;
}
/* v0.38: a stored "original" that is not inside any loaded library (for example a trampoline in a /memfd: region that
   another injector later unmapped) must never be called: that is a SEGV_ACCERR. Such pointers are replaced by the
   real system symbol. dladdr() is only run once per distinct pointer; good pointers are cached. */
static std::atomic<void *> g_okp[96];
static std::atomic<int> g_okn{0};
static bool ptr_ok(void *p) {
    if (!p) return false;
    int n = std::min(g_okn.load(), 96);
    for (int i = 0; i < n; i++) if (g_okp[i].load() == p) return true;
    Dl_info di;
    if (!dladdr(p, &di) || !di.dli_fname) return false;
    int i = g_okn.fetch_add(1);
    if (i < 96) g_okp[i] = p;
    return true;
}
template <class F> static F orig_or(F &slot, const char *name, F self) {
    F f = slot;
    if (f && f != self) {
        if (ptr_ok((void *)f)) return f;
        static std::atomic<int> warned{0};
        if (warned++ < 20) L("CHAINFIX %s: original %p is outside every loaded library (foreign/memfd hook?) -> using the system symbol\n", name, (void *)f);
        slot = nullptr;
    }
    void *p = real_sym(name);
    if (p && p != (void *)self) { slot = (F)p; return (F)p; }
    return nullptr;
}
static void maps_report(const char *tag);    /* defined below: lists non-system executable mappings (other injectors) */
static void chain_report(const char *tag);   /* defined below: who owns the "original" function behind each of our hooks */

/* ---------- ANativeWindow calls without our own PLT/GOT (v0.42.1) ----------
   v0.42 crashed on some phones inside ANativeWindow_getWidth(): the GOT slot of THIS module for that libandroid
   import held a stale pointer (SIGSEGV, pc in an unrelated mapping, backtrace #01 = h_sbg). Since v0.39 the calls
   run on every launch even with render scale off. The functions are now looked up with dlsym at the time of use
   (cached once found), so a corrupted or stale import slot can no longer be called. Missing symbol -> harmless 0. */
static void *nw_sym(std::atomic<void *> &c, const char *n) {
    void *p = c.load();
    if (!p) { p = real_sym(n); if (p) c = p; }
    return p;
}
static int32_t nw_width(ANativeWindow *w) {
    static std::atomic<void *> c{nullptr};
    auto f = (int32_t (*)(ANativeWindow *))nw_sym(c, "ANativeWindow_getWidth");
    return (f && w) ? f(w) : 0;
}
static int32_t nw_height(ANativeWindow *w) {
    static std::atomic<void *> c{nullptr};
    auto f = (int32_t (*)(ANativeWindow *))nw_sym(c, "ANativeWindow_getHeight");
    return (f && w) ? f(w) : 0;
}
static int32_t nw_format(ANativeWindow *w) {
    static std::atomic<void *> c{nullptr};
    auto f = (int32_t (*)(ANativeWindow *))nw_sym(c, "ANativeWindow_getFormat");
    return (f && w) ? f(w) : 0;
}
static int32_t nw_sbg(ANativeWindow *w, int32_t ww, int32_t hh, int32_t fmt) {
    static std::atomic<void *> c{nullptr};
    auto f = (int32_t (*)(ANativeWindow *, int32_t, int32_t, int32_t))nw_sym(c, "ANativeWindow_setBuffersGeometry");
    return (f && w) ? f(w, ww, hh, fmt) : -22;
}

/* ---------- Render scale and mip bias ---------- */
static std::atomic<int> g_scale{0};        /* 0 = off, 20-99 = percent of native resolution */
static std::atomic<int> g_scale_marked{0}; /* 1 once scale_crash.txt has been written this session */
static std::atomic<int> g_bias{0};
static std::atomic<int> g_applied{0};
static void (*real_tp)(GLenum, GLenum, GLint) = nullptr;

static bool is_cmp(GLenum f) {
    return (f >= 0x9270 && f <= 0x9279) || (f >= 0x93b0 && f <= 0x93dd);
}

/* ---------- Pixel Blocks (near.txt, blk.txt) ---------- */
/* near.txt: 0 = off, 1 = on (default). Only used by the Pixel Blocks path.
   Textures that get a raised base level are also switched to NEAREST
   filtering, which is what produces the mosaic look. */
static std::atomic<int> g_near{1};
static std::atomic<int> g_blk{0};       /* blk.txt: target edge length in px per texture; 0 = fall back to bias.txt */
static std::atomic<int> g_flagged{0}, g_forced{0}, g_smp{0};
static const GLuint MAXTEX = 1u << 18;
static std::atomic<uint8_t> g_tf[1u << 18];        /* 1 = NEAREST filtering is forced on this texture */
static std::atomic<uint8_t> g_blank[1u << 18];     /* v0.44: 1 = uploads to this texture id are replaced with transparent blocks (Remove grass) */
static thread_local GLuint tl_bound[32];
static thread_local int tl_unit = 0;

static void (*o_act)(GLenum) = nullptr;
static void h_act(GLenum u) {
    int i = (int)u - 0x84C0;
    tl_unit = (i >= 0 && i < 32) ? i : 0;
    if (auto f = orig_or(o_act, "glActiveTexture", h_act)) f(u);
}

static void (*o_bind)(GLenum, GLuint) = nullptr;
static void h_bind(GLenum t, GLuint id) {
    if (t == GL_TEXTURE_2D) tl_bound[tl_unit] = id;
    if (auto f = orig_or(o_bind, "glBindTexture", h_bind)) f(t, id);
}

static void defer_drop(GLuint id);
static void (*o_del)(GLsizei, const GLuint *) = nullptr;
static void h_del(GLsizei n, const GLuint *ids) {
    if (ids) {
        for (GLsizei i = 0; i < n; i++) {
            if (ids[i] < MAXTEX) { g_tf[ids[i]] = 0; g_blank[ids[i]] = 0; }
            defer_drop(ids[i]);
            for (int u = 0; u < 32; u++) if (tl_bound[u] == ids[i]) tl_bound[u] = 0;
        }
    }
    if (auto f = orig_or(o_del, "glDeleteTextures", h_del)) f(n, ids);
}

/* ---------- Detail Cut (style.txt: 0 = off, 1-4 = minimum LOD; sedge.txt: 1 = NEAREST magnification) ---------- */
/* ---------- Flat (flat.txt: 0 = off, 1-4 = strength) ----------
   Locks a texture to its smallest mip levels so it renders as a near-solid color. */
static std::atomic<int> g_style{0};
static std::atomic<int> g_sedge{1};
static std::atomic<int> g_flat{0};
static std::atomic<int> g_ttsize{0};   /* ttsize.txt: -2 = base level only (shimmers) | -1 = mips capped at level 3 | 0 = normal | 1-3 = coarser pixels (2x, 4x, 8x) */
static std::atomic<int> g_ttsm{0};     /* ttsm.txt: 0 = sharp | 1 = soft blend between mip levels */
static std::atomic<int> g_forcegl{1};  /* v0.41: forcegl.txt 1 = hide Vulkan from libunity (every effect here needs OpenGL ES) */
static std::atomic<int> g_vk_blocked{0}; /* how many Vulkan loads / lookups were refused */
static std::atomic<int> g_testtex{1};  /* testtex.txt: 0 = off | 1 = True Pixelated (NEAREST sampling, full mip chain) */
#define TEX_MIN_LOD 0x813A
#define TEX_MAX_LEVEL 0x813D
#define TEX_ANISO 0x84FE
#ifndef GL_NEAREST_MIPMAP_NEAREST
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#endif

#ifndef GL_NEAREST_MIPMAP_LINEAR
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#endif
#ifndef GL_LINEAR_MIPMAP_NEAREST
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#endif
/* Min/mag filters for True Pixelated, derived from ttsize and ttsm */
static void tt_filters(GLint &mn, GLint &mg) {
    int sz = g_ttsize.load(), sm = g_ttsm.load();
    bool nomip = sz <= -2;
    if (sm >= 1) { mn = nomip ? GL_NEAREST : GL_NEAREST_MIPMAP_LINEAR; mg = GL_NEAREST; }
    else              { mn = nomip ? GL_NEAREST : GL_NEAREST_MIPMAP_NEAREST; mg = GL_NEAREST; }
}

static void h_tp(GLenum t, GLenum p, GLint v) {
    int st = g_style.load();
    int fl = g_flat.load();
    int tt = g_testtex.load();
    if ((st > 0 || fl > 0 || tt > 0) && p == TEX_ANISO && v > 1) v = 1;
    if (t == GL_TEXTURE_2D && (p == GL_TEXTURE_MIN_FILTER || p == GL_TEXTURE_MAG_FILTER)) {
        GLuint id = tl_bound[tl_unit];
        if (id > 0 && id < MAXTEX && g_tf[id].load()) {
            if (tt > 0) {
                /* True Pixelated: sharp texels, keep the full-resolution mip chain */
                GLint mn, mg; tt_filters(mn, mg);
                v = (p == GL_TEXTURE_MAG_FILTER) ? mg : mn;
                g_forced++;
            } else if (fl > 0) {
                /* Flat: NEAREST for both filters so the small mip stays a solid color */
                v = GL_NEAREST;
                g_forced++;
            } else if (st > 0) {
                /* Detail Cut: only the magnification filter is changed */
                if (p == GL_TEXTURE_MAG_FILTER) { v = GL_NEAREST; g_forced++; }
            } else {
                v = GL_NEAREST;
                g_forced++;
            }
        }
    }
    if (auto f = orig_or(real_tp, "glTexParameteri", h_tp)) f(t, p, v);
}

static void (*o_tpf)(GLenum, GLenum, GLfloat) = nullptr;
static void h_tpf(GLenum t, GLenum p, GLfloat v) {
    if ((g_style.load() > 0 || g_flat.load() > 0 || g_testtex.load() > 0) && p == TEX_ANISO && v > 1.0f) v = 1.0f;
    if (auto f = orig_or(o_tpf, "glTexParameterf", h_tpf)) f(t, p, v);
}

static void (*o_smp)(GLuint, GLenum, GLint) = nullptr;
static void h_smp(GLuint s, GLenum p, GLint v) {
    g_smp++;
    if (auto f = orig_or(o_smp, "glSamplerParameteri", h_smp)) f(s, p, v);
}

/* ---------- Texture groups, skip rules, minimum size, start delay ---------- */
static inline int64_t now_ns();
static std::atomic<int> g_minsz{64};                 /* minsz.txt: textures smaller than this are left untouched */
static std::atomic<int> g_delay{0};                     /* delay.txt: seconds after launch during which no textures are modified */
static std::atomic<int64_t> g_tstart{0};
struct Sk { int fmt, a, b; int hit = 0; };   /* hit: set once the rule has matched a texture (logged as SKIPHIT) */
static std::vector<Sk> g_bloff;                      /* bltoff.txt: built-in skip rules disabled from the WebUI; read once at startup */
static std::vector<Sk> g_skip;                       /* skip.txt: "fmt size" (size 0 = any) or "fmt w h"; read once at startup */
struct Grp { int fmt; int sz; int n; int ap; };
static const int MAXG = 96;
static Grp g_grp[MAXG];
static int g_ng = 0;
static std::mutex g_gm;
static std::atomic<int> g_gdirty{0};
static std::atomic<int> g_sk_delay{0}, g_sk_minsz{0}, g_sk_rule{0}, g_cand{0}, g_nobound{0};   /* why candidates were left untouched */

static thread_local bool tl_revisit = false;          /* true while a delayed texture is being processed (see revisit_pending) */
static void grp_note(int fmt, int sz, bool applied) {
    std::lock_guard<std::mutex> lk(g_gm);
    for (int i = 0; i < g_ng; i++)
        if (g_grp[i].fmt == fmt && g_grp[i].sz == sz) { if (!tl_revisit) g_grp[i].n++; if (applied) g_grp[i].ap++; g_gdirty = 1; return; }
    if (tl_revisit) return;
    if (g_ng < MAXG) { g_grp[g_ng].fmt = fmt; g_grp[g_ng].sz = sz; g_grp[g_ng].n = 1; g_grp[g_ng].ap = applied ? 1 : 0; g_ng++; g_gdirty = 1; }
}

/* Built-in skip rules, compiled in. Each one can be switched off through
   bltoff.txt, which the WebUI manages.
   37497 256x512                  scope / reticle atlas (Lava Remix)
   37493, 37492, 37497 128x128    small character-detail textures */
static const Sk g_builtin[] = {
    /* Fix scope: scope frame and reticle (sizes change between sessions, so every size seen so far is listed) */
    {37497, 256, 512},
    {37497, 512, 1024},
    {37497, 512, 256},
    {37497, 512, 512},
    /* Fix menu: lobby and menu buttons */
    {37496, 512, 512},
};

static bool in_skip(int fmt, int w, int h) {
    int sz = std::max(w, h);
    for (auto &p : g_builtin) {
        if (p.fmt != fmt || p.a != w || p.b != h) continue;
        bool off = false;
        for (auto &o : g_bloff)
            if (o.fmt == p.fmt && o.a == p.a && o.b == p.b) { off = true; break; }
        if (!off) return true;
    }
    for (auto &p : g_skip) {
        if (p.fmt != 0 && p.fmt != fmt) continue;               /* v0.38: fmt 0 = any format */
        bool m = false;
        if (p.b == 0) m = (p.a == 0 || p.a == sz);
        else m = (p.a == w && p.b == h);
        if (m) {
            if (!p.hit) { p.hit = 1; L("SKIPHIT rule fmt=%d a=%d b=%d matched a texture fmt=%d %dx%d\n", p.fmt, p.a, p.b, fmt, w, h); }
            return true;
        }
    }
    return false;
}

/* TEXN: one log line per created texture, stamped with the wall-clock time.
   Comparing two logs makes it easy to pick out scopes and red-dot sights. */
static std::atomic<int> g_texn{0};
static void texn_log(int w, int h, int lv, int fmt, bool cand) {
    int n = ++g_texn;
    if (n > 6000) return;
    time_t tt = time(nullptr); struct tm tmv; localtime_r(&tt, &tmv);
    L("TEXN %02d:%02d:%02d t=%.1f %dx%d lv=%d fmt=0x%x cand=%d\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
      (now_ns() - g_tstart.load()) / 1e9, w, h, lv, fmt, cand ? 1 : 0);
}

/* v0.38: textures created during the start delay are remembered and processed on the render thread once the
   delay is over (they used to be skipped for good, which is why delay=50 gave applied=0). */
struct Pend { GLuint id; GLsizei lv; GLenum fmt; GLsizei w, h; };
static std::vector<Pend> g_pend;
static size_t g_pend_i = 0;
static std::mutex g_pm;
static std::atomic<int> g_pend_n{0}, g_pend_drop{0}, g_revisited{0};
static std::atomic<int> g_n_ts3{0}, g_n_ts3_cmp{0}, g_n_cti2{0};   /* other texture upload paths (diagnostics) */
static void defer_add(GLuint id, GLsizei lv, GLenum fmt, GLsizei w, GLsizei h) {
    if (id == 0 || id >= MAXTEX) return;
    std::lock_guard<std::mutex> lk(g_pm);
    if (g_pend.size() >= 8192) { g_pend_drop++; return; }
    g_pend.push_back({id, lv, fmt, w, h});
    g_pend_n = (int)(g_pend.size() - g_pend_i);
}
static void defer_drop(GLuint id) {
    if (g_pend_n.load() == 0) return;
    std::lock_guard<std::mutex> lk(g_pm);
    for (size_t i = g_pend_i; i < g_pend.size(); i++) if (g_pend[i].id == id) { g_pend[i].id = 0; }
}

/* ---------- v0.44: Keep alpha soft (alpha.txt) and Remove grass (grass.txt) ----------
   Keep alpha soft: a compressed texture with an alpha channel (ETC2 punch-through / ETC2 EAC) is left exactly as the game
   made it, so smoke, dust, muzzle flash and leaves are not turned into hard blocks by the pixel look.
   Remove grass: for every "format size" line in grass.txt, a texture created with that format and longest side keeps its
   size and mips but its pixel data is replaced, at upload time, with fully transparent blocks. The sprite is still drawn,
   it just has nothing to show. Only formats with alpha can vanish; the scope / reticle atlas and the lobby buttons are
   never blanked. Written for this module from scratch. */
static std::atomic<int> g_alpha{1};
struct GrRule { int fmt; int sz; };
static std::vector<GrRule> g_grass;                  /* grass.txt, read once at startup */
static std::atomic<int> g_grass_on{0};
static std::atomic<int> g_alpha_kept{0}, g_grass_n{0}, g_grass_guard{0}, g_grass_noalpha{0}, g_grass_pbo{0}, g_grass_nobound{0};

static inline bool fmt_alpha(GLenum f) { return f >= 0x9276 && f <= 0x9279; }   /* ETC2 punch-through (+sRGB), ETC2 EAC RGBA (+sRGB) */
static inline bool tex_track() { return g_near.load() || g_style.load() || g_flat.load() || g_testtex.load() || g_grass_on.load(); }

/* One fully transparent block for the format; returns its size in bytes, 0 = this format cannot be made transparent. */
static int blank_block(GLenum f, uint8_t *out) {
    if (f == 0x9276 || f == 0x9277) {            /* punch-through: opaque bit 0, every pixel index 2 (= transparent) */
        static const uint8_t b[8] = {0, 0, 0, 0, 0xFF, 0xFF, 0, 0};
        memcpy(out, b, 8); return 8;
    }
    if (f == 0x9278 || f == 0x9279) {            /* EAC alpha half: base 0, multiplier 0 -> alpha 0 everywhere */
        memset(out, 0, 16); return 16;
    }
    if ((f >= 0x93B0 && f <= 0x93BD) || (f >= 0x93D0 && f <= 0x93DD)) {   /* ASTC: LDR void-extent block, colour 0,0,0,0 */
        static const uint8_t b[16] = {0xFC, 0xFD, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0};
        memcpy(out, b, 16); return 16;
    }
    return 0;
}
/* The scope / reticle atlases and the lobby buttons (exact sizes) are never blanked. */
static bool grass_guard(int fmt, int w, int h) {
    if (fmt == 37497 && ((w == 256 && h == 512) || (w == 512 && h == 1024) || (w == 512 && h == 256))) return true;
    if (fmt == 37496 && w == 512 && h == 512) return true;
    return false;
}
static bool grass_rule(int fmt, int mx) {
    for (auto &r : g_grass) if (r.fmt == fmt && r.sz == mx) return true;
    return false;
}
/* Which kind of texture this upload belongs to; returns true when the pixel data must be replaced. */
static bool grass_check(int fmt, int w, int h, bool log_it) {
    int mx = std::max(w, h);
    if (!grass_rule(fmt, mx)) return false;
    if (grass_guard(fmt, w, h)) {
        int c = ++g_grass_guard; st_seti("grass_guard", c);
        if (log_it && c <= 5) L("GRASS kept fmt=%d %dx%d: it is the scope / reticle atlas (or the lobby buttons)\n", fmt, w, h);
        return false;
    }
    uint8_t blk[16];
    if (!blank_block((GLenum)fmt, blk)) {
        int c = ++g_grass_noalpha; st_seti("grass_noalpha", c);
        if (log_it && c <= 5) L("GRASS refused fmt=%d %dx%d: this format has no alpha channel, it cannot be made transparent\n", fmt, w, h);
        return false;
    }
    return true;
}
/* Called from the texture-storage hook: remember which texture ids must be blanked. */
static void grass_on_storage(GLenum t, GLsizei w, GLsizei h, GLenum fmt) {
    if (!g_grass_on.load() || t != GL_TEXTURE_2D) return;
    GLuint id = tl_bound[tl_unit];
    bool hit = grass_check((int)fmt, (int)w, (int)h, true);
    if (id == 0 || id >= MAXTEX) { if (hit) { g_grass_nobound++; st_seti("grass_nobound", g_grass_nobound.load()); } return; }
    g_blank[id] = hit ? 1 : 0;
    if (hit) {
        int n = ++g_grass_n; st_seti("grass_n", n);
        if (n <= 40) L("GRASS rule hit #%d fmt=0x%x %dx%d tex=%u\n", n, (unsigned)fmt, (int)w, (int)h, (unsigned)id);
    }
}
/* Transparent replacement for `n` bytes of compressed data (same size, same format). */
static thread_local std::vector<uint8_t> tl_blank;
static const void *grass_data(GLenum fmt, GLsizei n, const void *p) {
    uint8_t blk[16];
    int bs = blank_block(fmt, blk);
    if (!bs || n <= 0 || !p || n > (8 << 20) || (n % bs)) return p;
    typedef void (*giv_t)(GLenum, GLint *);
    static giv_t giv = (giv_t)real_sym("glGetIntegerv");
    if (giv) {
        GLint pbo = 0; giv(0x88EF /* GL_PIXEL_UNPACK_BUFFER_BINDING */, &pbo);
        if (pbo != 0) { int c = ++g_grass_pbo; if (c == 1) L("GRASS cannot blank an upload that comes from a pixel buffer\n"); st_seti("grass_pbo", c); return p; }
    }
    tl_blank.resize((size_t)n);
    for (GLsizei i = 0; i < n; i += bs) memcpy(&tl_blank[(size_t)i], blk, (size_t)bs);
    return tl_blank.data();
}

static void tex_post(GLenum t, GLsizei lv, GLenum fmt, GLsizei w, GLsizei h, bool revisit);
static void (*o_st)(GLenum, GLsizei, GLenum, GLsizei, GLsizei) = nullptr;
static void h_st(GLenum t, GLsizei lv, GLenum fmt, GLsizei w, GLsizei h) {
    if (auto f = orig_or(o_st, "glTexStorage2D", h_st)) f(t, lv, fmt, w, h);
    tex_post(t, lv, fmt, w, h, false);
}
static void tex_post(GLenum t, GLsizei lv, GLenum fmt, GLsizei w, GLsizei h, bool revisit) {
    int b = g_bias.load(), k = g_blk.load();
    int mx = std::max(w, h);
    if (!revisit) grass_on_storage(t, w, h, fmt);
    bool cand = (t == GL_TEXTURE_2D && lv >= 3 && mx >= 64 && is_cmp(fmt));
    if (!revisit && t == GL_TEXTURE_2D && mx >= 16) texn_log((int)w, (int)h, (int)lv, (int)fmt, cand);
    if (!cand) return;
    if (!revisit) g_cand++;
    bool skip = false;
    if (in_skip((int)fmt, (int)w, (int)h)) { skip = true; if (!revisit) g_sk_rule++; }
    else if (mx < g_minsz.load()) { skip = true; if (!revisit) g_sk_minsz++; }
    else if (g_alpha.load() && fmt_alpha(fmt)) {          /* v0.44: Keep alpha soft */
        skip = true;
        if (!revisit) { int c = ++g_alpha_kept; if (c <= 20 || c % 200 == 0) L("ALPHAKEEP #%d %dx%d lv=%d fmt=0x%x left soft\n", c, (int)w, (int)h, (int)lv, (unsigned)fmt); }
    }
    int dl = g_delay.load();
    if (!skip && !revisit && dl > 0 && now_ns() - g_tstart.load() < (int64_t)dl * 1000000000LL) {
        skip = true; g_sk_delay++;
        defer_add(tl_bound[tl_unit], lv, fmt, w, h);       /* v0.38: come back to it after the delay */
    }
    if (!skip && !revisit && tl_bound[tl_unit] == 0) g_nobound++;

    /* Test Tex: force NEAREST sampling only — full texture data, sharp pixel look. */
    int tt = g_testtex.load();
    if (tt > 0) {
        grp_note((int)fmt, mx, !skip);
        if (!skip && real_tp) {
            GLuint id = tl_bound[tl_unit];
            if (id > 0 && id < MAXTEX) {
                g_tf[id] = 1; g_flagged++;
                GLint mn, mg; tt_filters(mn, mg);
                real_tp(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mn);
                real_tp(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mg);
                int sz = g_ttsize.load();
                if (sz == -1) {
                    /* Fine: never sample coarser than mip 3, so distant surfaces stay finer than Normal with less shimmer than Finer */
                    int cap = (int)lv - 1;
                    if (cap > 3) cap = 3;
                    if (cap < 0) cap = 0;
                    real_tp(GL_TEXTURE_2D, TEX_MAX_LEVEL, cap);
                } else if (sz > 0) {
                    /* bigger pixels: start from a coarser mip, keep at least 8 px */
                    int base = sz;
                    while (base > 0 && (mx >> base) < 8) base--;
                    if (base > (int)lv - 1) base = (int)lv - 1;
                    if (base > 0) real_tp(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, base);
                }
            }
            int n = ++g_applied;
            if (n <= 100 || n % 500 == 0)
                L("TESTTEX #%d %dx%d lv=%d fmt=0x%x size=%d smooth=%d\n", n, w, h, lv, fmt, g_ttsize.load(), g_ttsm.load());
        }
        return;
    }

    /* v19 Flat Color: lock to tiny mips so the surface is near-solid color (lighting still applies). */
    int fl = g_flat.load();
    if (fl > 0) {
        /* strength 1..4 → target remaining size ~8, 4, 2, 1 px */
        static const int FTGT[] = {0, 8, 4, 2, 1};
        int tgt = FTGT[fl < 4 ? fl : 4];
        int base = 0;
        if (!skip && mx > tgt)
            while ((mx >> (base + 1)) >= tgt && base < (int)lv - 1) base++;
        grp_note((int)fmt, mx, base > 0);
        if (base > 0 && real_tp) {
            real_tp(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, base);
            real_tp(GL_TEXTURE_2D, TEX_MAX_LEVEL, base);   /* lock: no higher mips */
            GLuint id = tl_bound[tl_unit];
            if (id > 0 && id < MAXTEX) {
                g_tf[id] = 1; g_flagged++;
                real_tp(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                real_tp(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            int n = ++g_applied;
            if (n <= 100 || n % 500 == 0)
                L("FLAT #%d %dx%d lv=%d fmt=0x%x base=%d tgt=%d\n", n, w, h, lv, fmt, base, tgt);
        }
        return;
    }

    int sty = g_style.load();
    if (sty > 0) {
        /* Detail Cut: raise min LOD only; original filters kept except optional sharp MAG */
        int lod = skip ? 0 : std::min(sty, (int)lv - 2);
        grp_note((int)fmt, mx, lod > 0);
        if (lod > 0 && real_tp) {
            real_tp(GL_TEXTURE_2D, TEX_MIN_LOD, lod);
            if (g_sedge.load()) {
                GLuint id = tl_bound[tl_unit];
                if (id > 0 && id < MAXTEX) {
                    g_tf[id] = 1; g_flagged++;
                    real_tp(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
            }
            int n = ++g_applied;
            if (n <= 100 || n % 500 == 0)
                L("STYLE #%d %dx%d lv=%d fmt=0x%x lod=%d edge=%d\n", n, w, h, lv, fmt, lod, g_sedge.load());
        }
        return;
    }
    int base = 0;
    if (!skip) {
        if (k > 0) {
            if (mx > k)
                while ((mx >> (base + 1)) >= k && base < (int)lv - 2) base++;
        } else if (b > 0 && lv >= 4 && mx >= 512) {
            base = std::min(b, (int)lv - 2);
        }
    }
    grp_note((int)fmt, mx, base > 0);
    if (base > 0 && real_tp) {
        real_tp(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, base);
        if (g_near.load()) {
            GLuint id = tl_bound[tl_unit];
            if (id > 0 && id < MAXTEX) {
                g_tf[id] = 1; g_flagged++;
                real_tp(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                real_tp(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
        }
        int n = ++g_applied;
        if (n <= 100 || n % 500 == 0)
            L("APPLIED #%d %dx%d lv=%d fmt=0x%x base=%d blk=%d\n", n, w, h, lv, fmt, base, k);
    }
}

/* Render thread, once per frame: process textures that were created during the start delay. */
static void revisit_pending() {
    if (g_pend_n.load() == 0) return;
    int dl = g_delay.load();
    if (dl > 0 && now_ns() - g_tstart.load() < (int64_t)dl * 1000000000LL) return;
    Pend batch[48]; int nb = 0;
    {
        std::lock_guard<std::mutex> lk(g_pm);
        while (nb < 48 && g_pend_i < g_pend.size()) { Pend p = g_pend[g_pend_i++]; if (p.id) batch[nb++] = p; }
        if (g_pend_i >= g_pend.size()) { g_pend.clear(); g_pend_i = 0; g_pend_n = 0; }
        else g_pend_n = (int)(g_pend.size() - g_pend_i);
    }
    if (!nb) return;
    typedef GLboolean (*is_t)(GLuint);
    typedef void (*giv_t)(GLenum, GLint *);
    static is_t is_tex = (is_t)real_sym("glIsTexture");
    static giv_t giv = (giv_t)real_sym("glGetIntegerv");
    auto bindf = orig_or(o_bind, "glBindTexture", h_bind);
    if (!is_tex || !giv || !bindf || !real_tp) { static int w = 0; if (!w++) L("REVISIT unavailable (is=%d giv=%d bind=%d tp=%d)\n", is_tex != nullptr, giv != nullptr, bindf != nullptr, real_tp != nullptr); return; }
    GLint prev = 0; giv(0x8069 /* GL_TEXTURE_BINDING_2D */, &prev);
    GLuint prev_tl = tl_bound[tl_unit];
    int done = 0;
    for (int i = 0; i < nb; i++) {
        if (!is_tex(batch[i].id)) continue;
        bindf(GL_TEXTURE_2D, batch[i].id);
        tl_bound[tl_unit] = batch[i].id;
        tl_revisit = true;
        tex_post(GL_TEXTURE_2D, batch[i].lv, batch[i].fmt, batch[i].w, batch[i].h, true);
        tl_revisit = false;
        done++;
    }
    bindf(GL_TEXTURE_2D, (GLuint)prev);
    tl_bound[tl_unit] = prev_tl;
    int tot = (g_revisited += done);
    static int logged = 0;
    if (logged++ < 10) L("REVISIT batch=%d done=%d total=%d pending=%d applied=%d\n", nb, done, tot, g_pend_n.load(), g_applied.load());
}

/* Plain-words reason why nothing was changed yet; "none" once at least one texture was applied. */
static std::string why0_reason() {
    if (g_applied.load() > 0) return "none";
    std::string r;
    auto add = [&](const std::string &x) { if (!r.empty()) r += ","; r += x; };
    if (g_hook_state.load() != 1) add("hook_not_active");
    if (!g_near.load() && !g_style.load() && !g_flat.load() && !g_testtex.load()) add("look_off");
    if (g_cand.load() == 0) add("no_candidates");
    if (g_sk_delay.load() > 0) add("delay(" + std::to_string(g_sk_delay.load()) + ")");
    if (g_sk_minsz.load() > 0) add("minsz(" + std::to_string(g_sk_minsz.load()) + ")");
    if (g_sk_rule.load() > 0) add("skip_rule(" + std::to_string(g_sk_rule.load()) + ")");
    if (g_alpha_kept.load() > 0) add("alpha_soft(" + std::to_string(g_alpha_kept.load()) + ")");
    if (g_nobound.load() > 0) add("unbound(" + std::to_string(g_nobound.load()) + ")");
    return r.empty() ? "unknown" : r;
}

static void grp_dump(double t_s) {
    st_seti("cand", g_cand.load()); st_seti("applied", g_applied.load());
    st_seti("skip_rule", g_sk_rule.load()); st_seti("skip_minsz", g_sk_minsz.load());
    st_seti("skip_delay", g_sk_delay.load()); st_seti("alpha_kept", g_alpha_kept.load()); st_seti("grass_n", g_grass_n.load()); st_seti("revisited", g_revisited.load()); st_seti("ts3_cmp", g_n_ts3_cmp.load()); st_set("why0", why0_reason());
    L("TEXSKIP applied=%d cand=%d skip_rule=%d skip_minsz=%d skip_delay=%d unbound=%d revisited=%d pending=%d dropped=%d ts3=%d ts3cmp=%d cti2=%d\n", g_applied.load(), g_cand.load(), g_sk_rule.load(), g_sk_minsz.load(), g_sk_delay.load(), g_nobound.load(), g_revisited.load(), g_pend_n.load(), g_pend_drop.load(), g_n_ts3.load(), g_n_ts3_cmp.load(), g_n_cti2.load());
    Grp tmp[MAXG]; int n;
    { std::lock_guard<std::mutex> lk(g_gm); n = g_ng; for (int i = 0; i < n; i++) tmp[i] = g_grp[i]; g_gdirty = 0; }
    for (int i = 0; i < n; i++)
        L("TEXG t=%.0f fmt=%d hex=0x%x sz=%d n=%d ap=%d\n", t_s, tmp[i].fmt, tmp[i].fmt, tmp[i].sz, tmp[i].n, tmp[i].ap);
}

/* ---------- Frame-time logger and FPS cap ---------- */
static std::atomic<int> g_fps{0};          /* 0 = no cap (logging only) */
static std::atomic<pid_t> g_rt{0};         /* render thread (first thread to call eglSwapBuffers) */

static inline int64_t now_ns() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/* ---------- Freeze logger (diagnostics only, no effect on gameplay) ---------- */
/* fz.txt: 0 = off (no extra hooks installed), 1 = on (default) */
static std::atomic<int> g_fz{1};
static std::atomic<int64_t> g_t0{0};       /* time of the first rendered frame; log timestamps are relative to it */

enum { K_COMP, K_LINK, K_PBIN, K_BUF, K_TEX, K_N };
static std::atomic<int> c_cnt[2][K_N];     /* [0] = render thread, [1] = all other threads */
static std::atomic<int64_t> c_ns[2][K_N];

static thread_local pid_t tl_tid = 0;
static inline bool on_rt() {
    if (!tl_tid) tl_tid = (pid_t)syscall(SYS_gettid);
    return tl_tid == g_rt.load();
}

#define TIMED(K, call) do { \
    int64_t t0_ = now_ns(); call; int64_t d_ = now_ns() - t0_; \
    int i_ = on_rt() ? 0 : 1; c_cnt[i_][K]++; c_ns[i_][K] += d_; } while (0)

static void (*o_comp)(GLuint) = nullptr;
static void h_comp(GLuint s) { auto fn_ = orig_or(o_comp, "glCompileShader", h_comp); if (!fn_) return; TIMED(K_COMP, fn_(s)); }

static void (*o_link)(GLuint) = nullptr;
static void h_link(GLuint p) { auto fn_ = orig_or(o_link, "glLinkProgram", h_link); if (!fn_) return; TIMED(K_LINK, fn_(p)); }

static void (*o_pbin)(GLuint, GLenum, const void *, GLsizei) = nullptr;
static void h_pbin(GLuint p, GLenum f, const void *b, GLsizei n) {
    auto fn_ = orig_or(o_pbin, "glProgramBinary", h_pbin); if (!fn_) return; TIMED(K_PBIN, fn_(p, f, b, n));
}

static void (*o_buf)(GLenum, GLsizeiptr, const void *, GLenum) = nullptr;
static void h_buf(GLenum t, GLsizeiptr sz, const void *d, GLenum u) {
    auto fn_ = orig_or(o_buf, "glBufferData", h_buf); if (!fn_) return; TIMED(K_BUF, fn_(t, sz, d, u));
}

static void (*o_ti2)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *) = nullptr;
static void h_ti2(GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void *p) {
    auto fn_ = orig_or(o_ti2, "glTexImage2D", h_ti2); if (!fn_) return; TIMED(K_TEX, fn_(t, l, ifmt, w, h, b, f, ty, p));
}

static void (*o_cti2)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *) = nullptr;
static void h_cti2(GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLsizei n, const void *p) {
    if (l == 0) g_n_cti2++;
    auto fn_ = orig_or(o_cti2, "glCompressedTexImage2D", h_cti2); if (!fn_) return;
    if (g_grass_on.load() && p && t == GL_TEXTURE_2D && l >= 0 && l < 14) {      /* v0.44 Remove grass (texture made without glTexStorage2D) */
        int bw = (int)w << l, bh = (int)h << l;
        if (grass_check((int)f, bw, bh, l == 0)) {
            if (l == 0) { int c = ++g_grass_n; st_seti("grass_n", c); if (c <= 40) L("GRASS rule hit #%d fmt=0x%x %dx%d (glCompressedTexImage2D)\n", c, (unsigned)f, (int)w, (int)h); }
            p = grass_data(f, n, p);
        }
    }
    TIMED(K_TEX, fn_(t, l, f, w, h, b, n, p));
}

static void (*o_tsi2)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *) = nullptr;
static void h_tsi2(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum ty, const void *p) {
    auto fn_ = orig_or(o_tsi2, "glTexSubImage2D", h_tsi2); if (!fn_) return; TIMED(K_TEX, fn_(t, l, x, y, w, h, f, ty, p));
}

static void (*o_ctsi2)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void *) = nullptr;
static void h_ctsi2(GLenum t, GLint l, GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLsizei n, const void *p) {
    auto fn_ = orig_or(o_ctsi2, "glCompressedTexSubImage2D", h_ctsi2); if (!fn_) return;
    if (g_grass_on.load() && p && t == GL_TEXTURE_2D) {                           /* v0.44 Remove grass */
        GLuint id = tl_bound[tl_unit];
        if (id > 0 && id < MAXTEX && g_blank[id].load()) p = grass_data(f, n, p);
    }
    TIMED(K_TEX, fn_(t, l, x, y, w, h, f, n, p));
}

static void (*o_ts3)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei) = nullptr;
static void h_ts3(GLenum t, GLsizei lv, GLenum f, GLsizei w, GLsizei h, GLsizei d) {
    int n3 = ++g_n_ts3; bool c3 = is_cmp(f); if (c3) g_n_ts3_cmp++;
    if (n3 <= 25) L("TS3 target=0x%x lv=%d fmt=0x%x %dx%dx%d cmp=%d (not modified)\n", t, (int)lv, f, (int)w, (int)h, (int)d, c3 ? 1 : 0);
    auto fn_ = orig_or(o_ts3, "glTexStorage3D", h_ts3); if (!fn_) return; TIMED(K_TEX, fn_(t, lv, f, w, h, d));
}

/* ---------- Per-thread CPU usage (shows which threads were busy during a freeze) ---------- */
/* th.txt: 0 = off, 1 = on (default). Runs on the render thread; no extra thread is created. */
static std::atomic<int> g_th{1};
static int g_sched_mode = -1;              /* 1 = /proc schedstat (ns), 0 = /proc stat (ticks) */
struct Thr { pid_t tid; int fd; int64_t prev, cur, wprev, wcur; char name[20]; };
static std::vector<Thr> g_thr;             /* only accessed from the render thread */
static int g_nthr = 0;
static const size_t MAXT = 48;

/* ---------- CPU core logger (diagnostics only) ---------- */
/* core.txt: 0 = off, 1 = on (default). Requires fz.txt = 1 and th.txt = 1. */
static std::atomic<int> g_core{1};
static const int NC = 8;
static std::vector<std::pair<pid_t, int>> g_um;    /* UnityMain threads: tid and an open fd on /proc/self/task/<tid>/stat */
static int g_um_now = -1, g_um_prev = -1;
static int g_h_um[NC], g_h_rt[NC], g_h_fz[NC];
static int g_h_n = 0;

static int read_int_file(const char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char b[32];
    ssize_t n = read(fd, b, sizeof(b) - 1);
    close(fd);
    if (n <= 0) return -1;
    b[n] = 0;
    return atoi(b);
}

static int cpu_khz(int c) {
    char p[96];
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", c);
    return read_int_file(p);
}

/* Field 39 ("processor") of /proc/.../stat is the CPU the thread last ran on. */
static int core_of(int fd) {
    char b[640];
    ssize_t n = pread(fd, b, sizeof(b) - 1, 0);
    if (n <= 0) return -1;
    b[n] = 0;
    char *p = strrchr(b, ')');
    if (!p || !p[1]) return -1;
    p += 2;
    for (int i = 0; i < 36; i++) {
        p = strchr(p, ' ');
        if (!p) return -1;
        p++;
    }
    return atoi(p);
}

static void core_info() {
    char buf[480]; int len = snprintf(buf, sizeof buf, "CORES");
    for (int c = 0; c < NC && len < (int)sizeof(buf) - 60; c++) {
        char p[96], a[16], m[16];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpu_capacity", c);
        int cap = read_int_file(p);
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
        int mx = read_int_file(p);
        if (cap >= 0) snprintf(a, sizeof a, "%d", cap); else strcpy(a, "n/a");
        if (mx >= 0) snprintf(m, sizeof m, "%d", mx); else strcpy(m, "n/a");
        len += snprintf(buf + len, sizeof(buf) - len, " cpu%d=cap:%s/max:%s", c, a, m);
    }
    L("%s\n", buf);
}

static void core_sample() {
    g_um_prev = g_um_now;
    int c = -1;
    for (auto &u : g_um) {
        if (u.second < 0) continue;
        int x = core_of(u.second);
        if (x >= 0) { c = x; break; }
    }
    g_um_now = c;
    int r = sched_getcpu();
    if (c >= 0 && c < NC) g_h_um[c]++;
    if (r >= 0 && r < NC) g_h_rt[r]++;
    g_h_n++;
}

static void core_freeze(long long frame, float ms, double t_s) {
    int c = g_um_now;
    if (c >= 0 && c < NC) g_h_fz[c]++;
    L("CORE t=%.2f f=%lld ms=%.0f um=%d prev=%d um_khz=%d rt=%d\n",
      t_s, frame, ms, c, g_um_prev, c >= 0 ? cpu_khz(c) : -1, sched_getcpu());
}

static int put_arr(char *b, size_t cap, const int *a) {
    int len = 0;
    for (int i = 0; i < NC; i++)
        len += snprintf(b + len, cap - len, "%s%d", i ? "," : "", a[i]);
    return len;
}

static void core_hist(double t_s) {
    if (g_h_n <= 0) return;
    char buf[300]; int len = snprintf(buf, sizeof buf, "CH t=%.1f n=%d um=", t_s, g_h_n);
    len += put_arr(buf + len, sizeof(buf) - len, g_h_um);
    len += snprintf(buf + len, sizeof(buf) - len, " rt=");
    len += put_arr(buf + len, sizeof(buf) - len, g_h_rt);
    len += snprintf(buf + len, sizeof(buf) - len, " umfz=");
    put_arr(buf + len, sizeof(buf) - len, g_h_fz);
    L("%s\n", buf);
    memset(g_h_um, 0, sizeof g_h_um);
    memset(g_h_rt, 0, sizeof g_h_rt);
    memset(g_h_fz, 0, sizeof g_h_fz);
    g_h_n = 0;
}

static bool read_cpu(int fd, int64_t &run, int64_t &wait) {
    char b[256];
    ssize_t n = pread(fd, b, sizeof(b) - 1, 0);
    if (n <= 0) return false;
    b[n] = 0;
    if (g_sched_mode == 1) {
        char *e = nullptr;
        run = strtoll(b, &e, 10);
        wait = (e && *e) ? strtoll(e, &e, 10) : 0;
        return true;
    }
    char *p = strrchr(b, ')');
    if (!p) return false;
    unsigned long ut = 0, st = 0;
    if (sscanf(p + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu", &ut, &st) != 2) return false;
    run = (int64_t)(ut + st) * 10000000LL;
    wait = 0;
    return true;
}

static void th_init() {
    char path[64];
    snprintf(path, sizeof path, "/proc/self/task/%d/schedstat", (int)getpid());
    g_sched_mode = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        g_sched_mode = 1;
        int64_t r = 0, w = 0;
        if (!read_cpu(fd, r, w) || r <= 0) g_sched_mode = 0;
        close(fd);
    }
    L("THR mode=%s\n", g_sched_mode == 1 ? "schedstat" : "stat");
    if (g_core.load()) core_info();
}

static void th_rescan() {
    for (auto &t : g_thr) if (t.fd >= 0) close(t.fd);
    g_thr.clear();
    for (auto &u : g_um) if (u.second >= 0) close(u.second);
    g_um.clear();
    auto d = opendir("/proc/self/task");
    if (!d) return;
    std::vector<Thr> all;
    while (dirent *e = readdir(d)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        pid_t tid = (pid_t)atoi(e->d_name);
        char path[64];
        snprintf(path, sizeof path, "/proc/self/task/%d/%s", (int)tid, g_sched_mode == 1 ? "schedstat" : "stat");
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) continue;
        Thr t; memset(&t, 0, sizeof t);
        t.tid = tid; t.fd = fd;
        if (!read_cpu(fd, t.cur, t.wcur)) { close(fd); continue; }
        t.prev = t.cur; t.wprev = t.wcur;
        snprintf(path, sizeof path, "/proc/self/task/%d/comm", (int)tid);
        int cf = open(path, O_RDONLY | O_CLOEXEC);
        ssize_t n = 0;
        if (cf >= 0) { n = read(cf, t.name, 15); close(cf); }
        if (n > 0) {
            t.name[n] = 0;
            for (char *c = t.name; *c; c++) if (*c == '\n' || *c == ' ') *c = (*c == '\n') ? 0 : '_';
        } else strcpy(t.name, "?");
        all.push_back(t);
    }
    closedir(d);
    if (g_core.load()) {
        for (auto &a : all) {
            if (strcmp(a.name, "UnityMain") != 0 || g_um.size() >= 2) continue;
            char sp[64];
            snprintf(sp, sizeof sp, "/proc/self/task/%d/stat", (int)a.tid);
            int sf = open(sp, O_RDONLY | O_CLOEXEC);
            if (sf >= 0) g_um.push_back({a.tid, sf});
        }
    }
    g_nthr = (int)all.size();
    std::sort(all.begin(), all.end(), [](const Thr &a, const Thr &b) { return a.cur > b.cur; });
    pid_t me = getpid(), rt = g_rt.load();
    for (size_t i = 0; i < all.size(); i++) {
        if (i < MAXT || all[i].tid == me || all[i].tid == rt) g_thr.push_back(all[i]);
        else close(all[i].fd);
    }
}

static void th_snapshot() {
    for (auto &x : g_thr) {
        if (x.fd < 0) continue;
        x.prev = x.cur; x.wprev = x.wcur;
        if (!read_cpu(x.fd, x.cur, x.wcur)) { close(x.fd); x.fd = -1; x.cur = x.prev; x.wcur = x.wprev; }
    }
}

static void th_log(long long frame, float ms, double t_s) {
    pid_t me = getpid(), rt = g_rt.load();
    std::vector<std::pair<int64_t, size_t>> v;
    double sum = 0, rwait = 0;
    for (size_t i = 0; i < g_thr.size(); i++) {
        int64_t dl = g_thr[i].cur - g_thr[i].prev;
        if (dl < 0) dl = 0;
        sum += dl / 1e6;
        if (g_thr[i].tid == rt) rwait = (g_thr[i].wcur - g_thr[i].wprev) / 1e6;
        if (dl >= 3000000LL) v.push_back({dl, i});
    }
    std::sort(v.begin(), v.end(), [](const std::pair<int64_t, size_t> &a, const std::pair<int64_t, size_t> &b) { return a.first > b.first; });
    char buf[420]; int len = 0;
    len += snprintf(buf + len, sizeof(buf) - len, "THR t=%.2f f=%lld ms=%.0f sum=%.0f rwait=%.0f nthr=%d |", t_s, frame, ms, sum, rwait, g_nthr);
    for (size_t k = 0; k < v.size() && k < 6 && len < (int)sizeof(buf) - 40; k++) {
        const Thr &x = g_thr[v[k].second];
        len += snprintf(buf + len, sizeof(buf) - len, " %s%s=%.0f", x.name,
                        x.tid == rt ? "*" : (x.tid == me ? "!" : ""), v[k].first / 1e6);
    }
    L("%s\n", buf);
}

static inline double tv_ms(const timeval &a) { return a.tv_sec * 1000.0 + a.tv_usec / 1000.0; }

static void log_gl_info() {
    typedef const GLubyte *(*gs_t)(GLenum);
    gs_t gs = (gs_t)dlsym(RTLD_DEFAULT, "glGetString");
    if (!gs) { void *h = dlopen("libGLESv3.so", RTLD_NOW); if (h) gs = (gs_t)dlsym(h, "glGetString"); }
    if (!gs) { L("GLINFO glGetString n/a\n"); return; }
    const char *r = (const char *)gs(GL_RENDERER), *v = (const char *)gs(GL_VERSION), *e = (const char *)gs(GL_EXTENSIONS);
    L("GLINFO renderer=%s version=%s\n", r ? r : "?", v ? v : "?");
    L("GLINFO ext lod_bias=%d aniso=%d\n", (e && strstr(e, "lod_bias")) ? 1 : 0, (e && strstr(e, "anisotropic")) ? 1 : 0);
}

static void mark_scale_progress(int frames);
/* v0.40 live FPS cap: fps.txt is looked at about once per second on the render thread (a stat() call), and
   a changed file is read again, so the WebUI can change the cap while the game is running. */
static void live_fps_poll(int64_t t) {
    static int64_t next = 0;
    static dev_t last_dev = 0; static ino_t last_ino = 0; static int64_t last_mt = -1; static off_t last_sz = -1;
    if (t < next) return;
    next = t + 1000000000LL;
    struct stat sb;
    if (stat(FPSF, &sb) != 0) return;
    int64_t mt = (int64_t)sb.st_mtim.tv_sec * 1000000000LL + sb.st_mtim.tv_nsec;
    if (last_mt < 0) { last_dev = sb.st_dev; last_ino = sb.st_ino; last_mt = mt; last_sz = sb.st_size; return; }   /* first look: the value was read at launch */
    if (mt == last_mt && sb.st_ino == last_ino && sb.st_dev == last_dev && sb.st_size == last_sz) return;
    last_dev = sb.st_dev; last_ino = sb.st_ino; last_mt = mt; last_sz = sb.st_size;
    int v = -1;
    FILE *f = fopen(FPSF, "r");
    if (!f) return;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    if (v < 0) return;                                  /* half-written or garbage: keep the current cap */
    int nv = (v >= 20 && v <= 120) ? v : 0;
    if (nv != g_fps.load()) {
        L("LIVEFPS cap %d -> %d (fps.txt changed while the game runs)\n", g_fps.load(), nv);
        g_fps = nv;
    }
    st_seti("live_fps", nv);
}
static EGLBoolean (*o_swap)(EGLDisplay, EGLSurface) = nullptr;
static EGLBoolean h_swap(EGLDisplay d, EGLSurface s) {
    int64_t t_pre = now_ns();
    auto f_swap = orig_or(o_swap, "eglSwapBuffers", h_swap);
    EGLBoolean r = f_swap ? f_swap(d, s) : EGL_FALSE;
    int64_t t_post = now_ns();

    pid_t tid = (pid_t)syscall(SYS_gettid);
    pid_t exp = 0;
    g_rt.compare_exchange_strong(exp, tid);
    if (g_rt.load() != tid) return r;       /* not the render thread: nothing to do */

    static int64_t last = 0;                /* time of previous frame (after cap) */
    static int64_t deadline = 0;            /* when the current frame should finish */
    static int64_t win_start = 0;
    static std::vector<float> ft;           /* frame times (ms) in a 5-second window */
    static int64_t frame_no = 0;
    static int win_fz = 0;                  /* freeze count in the stats window */
    static int fz_logged = 0;               /* cap on FREEZE log lines per session */
    static rusage ru_last;
    static bool ru_ok = false;
    static int64_t next_scan = 0;

    int64_t t = t_post;
    if (g_t0.load() == 0) g_t0 = t;
    frame_no++;
    revisit_pending();
    if (frame_no == 1) { L("GFX api=gles (eglSwapBuffers seen)\n"); st_set("gfx", (g_forcegl.load() && g_vk_blocked.load()) ? "gles_forced" : "gles"); st_set("stage", "gfx"); }
    if (frame_no == 20 && g_scale_marked.load()) mark_scale_progress(20);
    if (frame_no == 120 && g_scale_marked.load()) { unlink(SCRASHF); L("scale ok: crash marker cleared after 120 frames\n"); }
    if (frame_no == 120) maps_report("frame120");
    if (frame_no == 30) log_gl_info();

    /* FPS cap: sleep the remaining time so frame intervals stay even */
    live_fps_poll(t);
    int cap = g_fps.load();
    if (cap > 0) {
        int64_t iv = 1000000000LL / cap;
        if (deadline == 0) deadline = t;
        deadline += iv;
        if (t > deadline + iv) deadline = t;          /* fell far behind: reset deadline */
        else if (t < deadline) {
            timespec ts;
            ts.tv_sec = deadline / 1000000000LL;
            ts.tv_nsec = deadline % 1000000000LL;
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {}
            t = now_ns();
        }
    }

    float ms = 0.f;
    if (last != 0) {
        ms = (float)((t - last) / 1e6);
        if (ms < 1000.f) ft.push_back(ms);           /* ignore loading pauses in the statistics */
    }
    last = t;
    if (win_start == 0) win_start = t;

    /* Freeze logger: read and reset counters each frame */
    if (g_fz.load()) {
        rusage ru; bool ok = (getrusage(RUSAGE_THREAD, &ru) == 0);
        bool th_on = g_th.load() != 0;
        if (th_on && g_sched_mode < 0) th_init();
        if (th_on && g_thr.empty()) th_rescan();
        if (th_on) th_snapshot();
        if (th_on && g_core.load()) core_sample();
        int cc[2][K_N]; int64_t cn[2][K_N];
        for (int i = 0; i < 2; i++)
            for (int k = 0; k < K_N; k++) { cc[i][k] = c_cnt[i][k].exchange(0); cn[i][k] = c_ns[i][k].exchange(0); }
        if (last != 0 && ms >= 100.f) {
            win_fz++;
            if (fz_logged < 400) {
                fz_logged++;
                double cpu = 0, vcs = 0, ics = 0, minf = 0, majf = 0;
                if (ok && ru_ok) {
                    cpu = tv_ms(ru.ru_utime) + tv_ms(ru.ru_stime) - tv_ms(ru_last.ru_utime) - tv_ms(ru_last.ru_stime);
                    vcs = (double)(ru.ru_nvcsw - ru_last.ru_nvcsw);
                    ics = (double)(ru.ru_nivcsw - ru_last.ru_nivcsw);
                    minf = (double)(ru.ru_minflt - ru_last.ru_minflt);
                    majf = (double)(ru.ru_majflt - ru_last.ru_majflt);
                }
                L("FREEZE t=%.2f f=%lld ms=%.0f swap=%.0f cpu=%.0f vcs=%.0f ics=%.0f minf=%.0f majf=%.0f"
                  " | rt comp=%d/%.0f link=%d/%.0f pbin=%d/%.0f buf=%d/%.0f tex=%d/%.0f"
                  " | bg comp=%d/%.0f link=%d/%.0f pbin=%d/%.0f buf=%d/%.0f tex=%d/%.0f\n",
                  (t - g_t0.load()) / 1e9, (long long)frame_no, ms, (t_post - t_pre) / 1e6, cpu, vcs, ics, minf, majf,
                  cc[0][K_COMP], cn[0][K_COMP] / 1e6, cc[0][K_LINK], cn[0][K_LINK] / 1e6,
                  cc[0][K_PBIN], cn[0][K_PBIN] / 1e6, cc[0][K_BUF], cn[0][K_BUF] / 1e6,
                  cc[0][K_TEX], cn[0][K_TEX] / 1e6,
                  cc[1][K_COMP], cn[1][K_COMP] / 1e6, cc[1][K_LINK], cn[1][K_LINK] / 1e6,
                  cc[1][K_PBIN], cn[1][K_PBIN] / 1e6, cc[1][K_BUF], cn[1][K_BUF] / 1e6,
                  cc[1][K_TEX], cn[1][K_TEX] / 1e6);
                if (th_on) th_log((long long)frame_no, ms, (t - g_t0.load()) / 1e9);
                if (th_on && g_core.load()) core_freeze((long long)frame_no, ms, (t - g_t0.load()) / 1e9);
            }
        }
        if (ok) { ru_last = ru; ru_ok = true; }
        if (th_on && t >= next_scan) { th_rescan(); next_scan = t + 5000000000LL; }
    }

    if (t - win_start >= 5000000000LL && ft.size() >= 10) {
        std::sort(ft.begin(), ft.end());
        double sum = 0; for (float v : ft) sum += v;
        size_t n = ft.size();
        float p50 = ft[n / 2], p99 = ft[std::min(n - 1, (size_t)(n * 0.99))], mx = ft[n - 1];
        size_t s33 = 0, s50 = 0;
        for (float v : ft) { if (v > 33.4f) s33++; if (v > 50.f) s50++; }
        L("FT fps=%.1f avg=%.1fms p50=%.1f p99=%.1f max=%.1f >33ms=%zu >50ms=%zu n=%zu cap=%d bias=%d scale=%d t=%.1f fz=%d\n",
          1000.0 * n / sum, sum / n, p50, p99, mx, s33, s50, n, cap, g_bias.load(), g_scale.load(),
          (t - g_t0.load()) / 1e9, win_fz);
        if (g_near.load()) L("NEAR flagged=%d forced=%d smp=%d\n", g_flagged.load(), g_forced.load(), g_smp.load());
        {
            static int64_t last_gd = 0;
            if (g_gdirty.load() && t - last_gd >= 20000000000LL) { last_gd = t; grp_dump((t - g_t0.load()) / 1e9); }
        }
        if (g_fz.load() && g_th.load() && g_core.load()) core_hist((t - g_t0.load()) / 1e9);
        ft.clear();
        win_fz = 0;
        win_start = t;
    }
    return r;
}

/* ---------- Render scale (shrinks the window buffer) ----------
   v0.29: the native size is learned from the real window (ANativeWindow_getWidth/Height), never guessed.
   Calls with a 0 width/height ("keep native size") and portrait probes are passed through untouched.
   A marker file (scale_crash.txt) is written before scaling and removed after 120 rendered frames;
   if it is still there at the next launch, render scale is switched off so it can never boot-loop. */
static std::atomic<int> g_fullw{0}, g_fullh{0};   /* native landscape size, 0 = not learned yet */
static std::atomic<int> g_ovw{0}, g_ovh{0};       /* the last buffer size WE set: seeing it again must never be shrunk a second time */
static std::atomic<int> g_sbgn{0};
/* Scale a w x h pair together so the aspect ratio is kept. The shorter side never goes below 270 px
   (tiny buffers crash some drivers) unless the original is already smaller; then the longer side follows
   the same factor instead of being clamped on its own (which used to stretch the picture at low percentages).
   Returns false (and leaves the outputs alone) for sizes that make no sense, so the caller passes the
   original request through untouched. */
static bool scaled_pair(int w, int h, int s, int *ow, int *oh) {
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192 || s < 20 || s > 99) return false;
    int64_t m = std::min(w, h);
    int64_t tgt = m * s / 100;
    int64_t floor_ = m < 270 ? m : 270;
    if (tgt < floor_) tgt = floor_;
    int nw = (int)((int64_t)w * tgt / m), nh = (int)((int64_t)h * tgt / m);
    nw &= ~1; nh &= ~1;                                    /* keep both even */
    if (nw < 2) nw = 2;
    if (nh < 2) nh = 2;
    if (nw > w) nw = w & ~1;
    if (nh > h) nh = h & ~1;
    *ow = nw; *oh = nh;
    return true;
}

static void learn_full_wh(int w, int h) {
    if (w <= 0 || h <= 0 || w < h || w > 8192) return;
    if (w == g_ovw.load() && h == g_ovh.load()) return;      /* our own scaled size is never "native" */
    if (w > g_fullw.load()) {
        g_fullw = w; g_fullh = h;
        FILE *f = fopen(FULLF, "w");
        if (f) { fprintf(f, "%d %d\n", w, h); fclose(f); }
    }
}
static void learn_full(ANativeWindow *nwin) {
    if (!nwin) return;
    learn_full_wh(nw_width(nwin), nw_height(nwin));
}
static void mark_scale_progress(int frames) {
    FILE *f = fopen(SCRASHF, "w");
    if (f) { fprintf(f, "%d %d\n", g_scale.load(), frames); fclose(f); }
}

static void mark_scale_in_use() {
    if (g_scale_marked.exchange(1)) return;
    FILE *f = fopen(SCRASHF, "w");
    if (f) { fprintf(f, "%d\n", g_scale.load()); fclose(f); }     /* the scale that was active, for the next launch's message */
}

static EGLSurface (*o_cws)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint *) = nullptr;
static EGLSurface h_cws(EGLDisplay d, EGLConfig c, EGLNativeWindowType win, const EGLint *at) {
    { static std::atomic<int> first{0}; if (!first.exchange(1)) st_set("first_cws", "1"); }
    int s = g_scale.load();
    ANativeWindow *nwin = (ANativeWindow *)win;
    if (s > 0 && nwin) {
        int w = nw_width(nwin), h = nw_height(nwin);
        if (w > 0 && h > 0) {
            if (w < h) {
                L("SURFACE portrait %dx%d: skip\n", w, h);
            } else if (w == g_ovw.load() && h == g_ovh.load()) {
                L("SURFACE cur=%dx%d is already our scaled size: not shrunk again\n", w, h);
            } else {
                learn_full(nwin);
                int fw = g_fullw.load(), fh = g_fullh.load();
                if (fw > 0 && fh > 0) {
                    int nw = 0, nh = 0;
                    if (scaled_pair(fw, fh, s, &nw, &nh) && (nw >= w || nh >= h)) {
                        /* v0.38: the window is already as small as (or smaller than) the target, for example because another
                           module/the system shrank it. Never make it smaller: that was the 1600x720 -> 880x396 -> 600x270 chain. */
                        L("SURFACE cur=%dx%d full=%dx%d target=%dx%d: window already that small, left alone\n", w, h, fw, fh, nw, nh);
                    } else if (scaled_pair(fw, fh, s, &nw, &nh)) {
                        int fmt = nw_format(nwin);
                        if (fmt < 0) fmt = 0;           /* keep the window's current format */
                        mark_scale_in_use();
                        g_ovw = nw; g_ovh = nh;
                        int r = nw_sbg(nwin, nw, nh, fmt);
                        L("SURFACE cur=%dx%d full=%dx%d -> %dx%d fmt=%d r=%d\n", w, h, fw, fh, nw, nh, fmt, r);
                    } else L("SURFACE %dx%d (full %dx%d): scale skipped (size out of range)\n", w, h, fw, fh);
                }
            }
        }
    }
    if (s == 0 && nwin) {
        static std::atomic<int> wn{0};
        if (wn++ < 10) L("WINSIZE cws cur=%dx%d (render scale off, window untouched)\n", nw_width(nwin), nw_height(nwin));
    }
    auto f_cws = orig_or(o_cws, "eglCreateWindowSurface", h_cws);
    return f_cws ? f_cws(d, c, win, at) : EGL_NO_SURFACE;
}

static int32_t (*o_sbg)(ANativeWindow *, int32_t, int32_t, int32_t) = nullptr;
static int32_t h_sbg(ANativeWindow *w, int32_t ww, int32_t hh, int32_t f) {
    int s = g_scale.load();
    int n = ++g_sbgn;
    if (n == 1) st_set("first_sbg", "1");
    if (n <= 20) L("SBG call %dx%d fmt=%d cur=%dx%d\n", ww, hh, f, w ? nw_width(w) : 0, w ? nw_height(w) : 0);
    if (w && ww >= hh && ww >= 640 && hh >= 360) learn_full_wh(ww, hh);   /* what Unity asks for is the real native size */
    if (s > 0 && w) {
        if (ww == 0 || hh == 0) {
            learn_full(w);                          /* "keep native size": never override, just learn it */
        } else if (ww >= hh && ww >= 640 && hh >= 360) {
            /* Scale whatever landscape size Unity asks for (1600x720, 1280x720, ...). Scaling the request keeps
               its aspect ratio and cannot compound, because the request comes from Unity, not from us. */
            int nw = 0, nh = 0;
            if (scaled_pair(ww, hh, s, &nw, &nh)) {
                g_ovw = nw; g_ovh = nh;
                ww = nw; hh = nh;
                mark_scale_in_use();
                if (n <= 20) L("SBG overridden -> %dx%d\n", ww, hh);
            } else if (n <= 20) L("SBG %dx%d: scale skipped (size out of range)\n", ww, hh);
        }
    }
    auto ob = (o_sbg && o_sbg != h_sbg) ? o_sbg : nullptr;
    return ob ? ob(w, ww, hh, f) : nw_sbg(w, ww, hh, f);
}

static void *wrap(const char *n, void *r) {
    if (!n || !r) return r;
    if (n[0] == 'v' && n[1] == 'k' && n[2] >= 'A' && n[2] <= 'Z' && g_forcegl.load()) {
        /* v0.41 Force OpenGL ES: libunity must not find any Vulkan entry point, so Unity cannot create a
           Vulkan device and falls back to its next graphics API (GLES). Idea and method: the contributors' v0.39.1. */
        int c = ++g_vk_blocked;
        if (c == 1) { st_set("gfx", "gles_forced"); L("FORCEGL blocked Vulkan symbol %s (Unity should fall back to OpenGL ES)\n", n); }
        st_seti("vk_blocked", c);
        return nullptr;
    }
    if (n[0] == 'v' && n[1] == 'k') {          /* Vulkan entry point requested by libunity */
        static std::atomic<int> vk_once{0};
        if (!vk_once.exchange(1)) {
            st_set("gfx", "vulkan");
            L("GFX vk-symbol %s requested\n", n);
            L("GFX api=vulkan (detected) - texture effects, fps cap and render scale are NOT supported on Vulkan in %s; use GLES (disable modules that force Vulkan, e.g. core-vulkan)\n", VERSION);
        }
        return r;
    }
    /* v0.37: never keep our own hook as the "original" (another injector may hand our pointer back to us),
       and hand an already-wrapped pointer back untouched, so the chain can never loop. */
#define WRAP_ONE(NAME, O, H) if (!strcmp(n, NAME)) { if (r == (void *)(H)) return r; if (!(O)) (O) = (decltype(O))r; return (void *)(H); }
    if (!strcmp(n, "glTexParameteri")) { if (r == (void *)h_tp) return r; if (!real_tp) real_tp = (decltype(real_tp))r; return (g_near.load() || g_style.load() || g_flat.load() || g_testtex.load()) ? (void *)h_tp : r; }
    if (!strcmp(n, "glTexParameterf")) { if (r == (void *)h_tpf) return r; if (!o_tpf) o_tpf = (decltype(o_tpf))r; return (g_style.load() || g_flat.load() || g_testtex.load()) ? (void *)h_tpf : r; }
    if (tex_track()) {
        WRAP_ONE("glActiveTexture", o_act, h_act)
        WRAP_ONE("glBindTexture", o_bind, h_bind)
        WRAP_ONE("glDeleteTextures", o_del, h_del)
        WRAP_ONE("glSamplerParameteri", o_smp, h_smp)
    }
    WRAP_ONE("glTexStorage2D", o_st, h_st)
    WRAP_ONE("eglSwapBuffers", o_swap, h_swap)
    WRAP_ONE("eglCreateWindowSurface", o_cws, h_cws)
    WRAP_ONE("ANativeWindow_setBuffersGeometry", o_sbg, h_sbg)
    if (g_fz.load()) {
        WRAP_ONE("glCompileShader", o_comp, h_comp)
        WRAP_ONE("glLinkProgram", o_link, h_link)
        WRAP_ONE("glProgramBinary", o_pbin, h_pbin)
        WRAP_ONE("glBufferData", o_buf, h_buf)
        WRAP_ONE("glTexImage2D", o_ti2, h_ti2)
        WRAP_ONE("glCompressedTexImage2D", o_cti2, h_cti2)
        WRAP_ONE("glTexSubImage2D", o_tsi2, h_tsi2)
        WRAP_ONE("glCompressedTexSubImage2D", o_ctsi2, h_ctsi2)
        WRAP_ONE("glTexStorage3D", o_ts3, h_ts3)
    } else if (g_grass_on.load()) {          /* v0.44: Remove grass needs the two compressed upload calls even when the freeze logger is off */
        WRAP_ONE("glCompressedTexImage2D", o_cti2, h_cti2)
        WRAP_ONE("glCompressedTexSubImage2D", o_ctsi2, h_ctsi2)
    }
#undef WRAP_ONE
    return r;
}

static void *(*o_dlsym)(void *, const char *) = nullptr;
static void *h_dlsym(void *h, const char *n) {
    void *r = (o_dlsym && o_dlsym != h_dlsym) ? o_dlsym(h, n) : dlsym(h, n);   /* v0.37: was an unguarded call through o_dlsym */
    return wrap(n, r);
}
static void *(*o_egl)(const char *) = nullptr;
static void *h_egl(const char *n) {
    auto f = orig_or(o_egl, "eglGetProcAddress", h_egl);   /* v0.37: was an unguarded call through o_egl */
    void *r = f ? f(n) : nullptr;
    return wrap(n, r);
}

/* ---------- Force OpenGL ES (v0.41) ----------
   Unity tries Vulkan first when the game lists it, and only falls back to GLES when the Vulkan loader or
   device creation fails. With forcegl.txt=1, libunity is told that libvulkan does not exist: dlopen of it
   fails here, and every vk* symbol lookup (dlsym / eglGetProcAddress, see wrap()) returns null. Only
   libunity's own imports are changed; the rest of the system keeps using Vulkan normally.
   This is NOT Vulkan support: no texture hooks are installed for Vulkan. */
static bool is_vulkan_lib(const char *path) {
    if (!path) return false;
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    return strncmp(b, "libvulkan", 9) == 0;      /* libvulkan.so, libvulkan_*.so, libvulkan.adreno.so ... */
}
static void forcegl_note(const char *path) {
    int c = ++g_vk_blocked;
    st_seti("vk_blocked", c);
    static std::atomic<int> once{0};
    if (!once.exchange(1)) { st_set("gfx", "gles_forced"); L("FORCEGL refused to load %s for libunity (Unity should fall back to OpenGL ES)\n", path); }
}
static void *(*o_dlopen_u)(const char *, int) = nullptr;
static void *h_dlopen_u(const char *path, int flags) {
    if (g_forcegl.load() && is_vulkan_lib(path)) { forcegl_note(path); return nullptr; }
    return (o_dlopen_u && o_dlopen_u != h_dlopen_u) ? o_dlopen_u(path, flags) : dlopen(path, flags);
}
static void *(*o_adle_u)(const char *, int, const android_dlextinfo *) = nullptr;
static void *h_adle_u(const char *path, int flags, const android_dlextinfo *info) {
    if (g_forcegl.load() && is_vulkan_lib(path)) { forcegl_note(path); return nullptr; }
    return (o_adle_u && o_adle_u != h_adle_u) ? o_adle_u(path, flags, info) : android_dlopen_ext(path, flags, info);
}

/* New module version -> start a fresh log, so the top of codm_tex.log is always the installed version
   and nobody has to press "Clear log" after an update. Runs once per game launch, before the first L(). */
static void log_rotate_if_new_version() {
    FILE *f = fopen(LOGF, "r");
    if (!f) return;
    char line[512];
    bool old = false;
    const char *pre = "=== start ";
    size_t pl = strlen(pre), vl = strlen(VERSION);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, pre, pl) != 0) continue;
        /* version token ends at the next space: "=== start v0.33 build=..." */
        if (strncmp(line + pl, VERSION, vl) != 0 || line[pl + vl] != ' ') { old = true; break; }
    }
    fclose(f);
    if (!old) return;
    std::lock_guard<std::mutex> g(mu);
    if (lf) { fclose(lf); lf = nullptr; }
    FILE *w = fopen(LOGF, "w");                    /* truncate */
    if (w) fclose(w);
    lf_bytes = 0;
}

/* ---------- Settings audit ----------
   A missing, zero-length or non-numeric settings file used to leave some values at an
   undefined "whatever the variable started as" (for example delay stayed 20 while the file
   said 0). Now every key has an explicit default; a broken file is reported (log + status)
   and rewritten atomically with that default. Missing files are only reported; load_cfg()
   creates them as before. */
struct CfgKey { const char *name; const char *path; int def; bool optional; };
static const CfgKey CFG_KEYS[] = {
    {"bias", "bias.txt", 0, false}, {"fps", "fps.txt", 0, false}, {"scale", "scale.txt", 0, false}, {"fz", "fz.txt", 1, false},
    {"th", "th.txt", 1, false}, {"core", "core.txt", 1, false}, {"near", "near.txt", 1, false}, {"blk", "blk.txt", 0, false},
    {"minsz", "minsz.txt", 64, false}, {"delay", "delay.txt", 0, false}, {"style", "style.txt", 0, false}, {"sedge", "sedge.txt", 1, false},
    {"flat", "flat.txt", 0, false}, {"testtex", "testtex.txt", 1, false}, {"ttsize", "ttsize.txt", 0, false}, {"ttsm", "ttsm.txt", 0, false},
    {"hookdelay", "hookdelay.txt", 0, true}, {"forcegl", "forcegl.txt", 1, false}, {"alpha", "alpha.txt", 1, false},
};
static int g_scale_safety = 0;   /* >0 = render scale was switched off at this launch; value = the percent that was active */
static std::string g_cfg_note;   /* "" = no broken settings file; else e.g. "delay=empty,scale=invalid" */
static int g_cfg_missing = 0;    /* settings files that did not exist yet (reported, not an error) */

static void write_int_atomic(const char *path, int v) {
    std::string tmp = std::string(path) + ".tmp";
    FILE *f = fopen(tmp.c_str(), "w");
    if (!f) { unlink(tmp.c_str()); f = fopen(tmp.c_str(), "w"); }   /* root-owned leftover .tmp */
    if (!f) return;
    fprintf(f, "%d\n", v);
    fclose(f);
    chmod(tmp.c_str(), 0666);
    rename(tmp.c_str(), path);        /* also replaces a root-owned file: the folder belongs to the game */
}

static void cfg_audit() {
    g_cfg_note.clear(); g_cfg_missing = 0;
    for (const CfgKey &k : CFG_KEYS) {
        const char *kp = P(k.path);
        FILE *f = fopen(kp, "r");
        if (!f) {
            if (!k.optional && access(kp, F_OK) != 0) g_cfg_missing++;   /* normal on a fresh install; load_cfg() creates it */
            continue;
        }
        char buf[32]; size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f);
        buf[n] = 0;
        const char *why = nullptr;
        char *e = nullptr; (void)strtol(buf, &e, 10);
        bool blank = true; for (size_t i = 0; i < n; i++) if (buf[i] != ' ' && buf[i] != '\n' && buf[i] != '\r' && buf[i] != '\t') { blank = false; break; }
        if (blank) why = "empty"; else if (e == buf) why = "invalid";
        if (why) {
            if (!g_cfg_note.empty()) g_cfg_note += ",";
            g_cfg_note += std::string(k.name) + "=" + why;
            write_int_atomic(kp, k.def);
        }
    }
}

static void load_cfg() {
    log_rotate_if_new_version();
    g_scale_safety = 0;
    g_delay = 0;                                    /* explicit defaults for values whose file is missing or unreadable */
    g_scale = 0;
    cfg_audit();
    FILE *f = fopen(CFGF, "r");
    if (f) {
        int v = 2;
        if (fscanf(f, "%d", &v) == 1) g_bias = std::max(0, std::min(v, 6));
        fclose(f);
    } else {
        f = fopen(CFGF, "w");
        if (f) { fprintf(f, "0\n"); fclose(f); }
    }
    f = fopen(FPSF, "r");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1) g_fps = (v >= 20 && v <= 120) ? v : 0;
        fclose(f);
    } else {
        f = fopen(FPSF, "w");
        if (f) { fprintf(f, "0\n"); fclose(f); }
    }
    f = fopen(SCLF, "r");
    if (f) {
        int v = 65;
        if (fscanf(f, "%d", &v) == 1) g_scale = (v >= 20 && v <= 99) ? v : 0;
        fclose(f);
    } else {
        f = fopen(SCLF, "w");
        if (f) { fprintf(f, "0\n"); fclose(f); }
    }
    f = fopen(FZF, "r");
    if (f) {
        int v = 1;
        if (fscanf(f, "%d", &v) == 1) g_fz = v ? 1 : 0;
        fclose(f);
    } else {
        f = fopen(FZF, "w");
        if (f) { fprintf(f, "1\n"); fclose(f); }
    }
    f = fopen(THF, "r");
    if (f) {
        int v = 1;
        if (fscanf(f, "%d", &v) == 1) g_th = v ? 1 : 0;
        fclose(f);
    } else {
        f = fopen(THF, "w");
        if (f) { fprintf(f, "1\n"); fclose(f); }
    }
    f = fopen(NEARF, "r");
    if (f) {
        int v = 1;
        if (fscanf(f, "%d", &v) == 1) g_near = v ? 1 : 0;
        fclose(f);
    } else {
        f = fopen(NEARF, "w");
        if (f) { fprintf(f, "1\n"); fclose(f); }
    }
    f = fopen(BLKF, "r");
    if (f) {
        int v = 16;
        if (fscanf(f, "%d", &v) == 1) g_blk = (v >= 4 && v <= 256) ? v : 0;
        fclose(f);
    } else {
        f = fopen(BLKF, "w");
        if (f) { fprintf(f, "0\n"); fclose(f); }
    }
    f = fopen(MINF, "r");
    if (f) {
        int v = 64;
        if (fscanf(f, "%d", &v) == 1) g_minsz = std::max(64, std::min(v, 2048));
        fclose(f);
    } else {
        f = fopen(MINF, "w");
        if (f) { fprintf(f, "64\n"); fclose(f); }
    }
    f = fopen(DLYF, "r");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1) g_delay = std::max(0, std::min(v, 120));
        fclose(f);
    } else {
        f = fopen(DLYF, "w");
        if (f) { fprintf(f, "0\n"); fclose(f); }
    }
    g_skip.clear();
    f = fopen(SKIPF, "r");
    if (f) {
        char ln[96];
        while (g_skip.size() < 128 && fgets(ln, sizeof ln, f)) {
            int a = 0, b = 0, c = 0;
            /* v0.38: %i also reads 0x9279 (hex); before, a hex format was read as 0 and the whole line was silently ignored */
            int n = sscanf(ln, "%i %i %i", &a, &b, &c);
            if (n == 2) g_skip.push_back({a, b, 0});
            else if (n == 3) g_skip.push_back({a, b, c});
            else if (ln[0] != '\n' && ln[0] != '#') L("SKIPRULE ignored (cannot read it): %.40s", ln);
        }
        for (auto &r : g_skip) L("SKIPRULE loaded fmt=%d a=%d b=%d (%s)\n", r.fmt, r.a, r.b, r.b ? "exact WxH" : (r.a ? "longest side" : "any size"));
        fclose(f);
    }
    g_bloff.clear();
    f = fopen(BLOFFF, "r");
    if (f) {
        char ln[96];
        while (g_bloff.size() < 16 && fgets(ln, sizeof ln, f)) {
            int a = 0, b = 0, c = 0;
            if (sscanf(ln, "%d %d %d", &a, &b, &c) == 3) g_bloff.push_back({a, b, c});
        }
        fclose(f);
    }
    f = fopen(STYF, "r");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1) g_style = std::max(0, std::min(v, 4));
        fclose(f);
    } else {
        f = fopen(STYF, "w");
        if (f) { fprintf(f, "0\n"); fclose(f); }
    }
    f = fopen(SEDF, "r");
    if (f) {
        int v = 1;
        if (fscanf(f, "%d", &v) == 1) g_sedge = v ? 1 : 0;
        fclose(f);
    } else {
        f = fopen(SEDF, "w");
        if (f) { fprintf(f, "1\n"); fclose(f); }
    }
    f = fopen(FLATF, "r");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1) g_flat = std::max(0, std::min(v, 4));
        fclose(f);
    } else {
        f = fopen(FLATF, "w");
        if (f) { fprintf(f, "0\n"); fclose(f); }
    }
    f = fopen(TTEXF, "r");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1) g_testtex = v ? 1 : 0;
        fclose(f);
    } else {
        f = fopen(TTEXF, "w");
        if (f) { fprintf(f, "1\n"); fclose(f); }
    }
    f = fopen(TTSZF, "r");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1) g_ttsize = std::max(-2, std::min(v, 3));
        fclose(f);
    } else {
        f = fopen(TTSZF, "w");
        if (f) { fprintf(f, "0\n"); fclose(f); }
    }
    f = fopen(TTSMF, "r");
    if (f) {
        int v = 0;
        if (fscanf(f, "%d", &v) == 1) g_ttsm = std::max(0, std::min(v, 1));
        fclose(f);
    } else {
        f = fopen(TTSMF, "w");
        if (f) { fprintf(f, "0\n"); fclose(f); }
    }
    f = fopen(ALPHAF, "r");                        /* v0.44: Keep alpha soft, default ON */
    if (f) {
        int v = 1;
        if (fscanf(f, "%d", &v) == 1) g_alpha = v ? 1 : 0;
        fclose(f);
    } else {
        g_alpha = 1;
        f = fopen(ALPHAF, "w");
        if (f) { fprintf(f, "1\n"); fclose(f); }
    }
    g_grass.clear();
    f = fopen(GRASSF, "r");                        /* v0.44: Remove grass, "format size" per line; missing or empty = off */
    if (f) {
        char ln[96];
        while (g_grass.size() < 16 && fgets(ln, sizeof ln, f)) {
            int a = 0, b = 0;
            if (sscanf(ln, "%i %i", &a, &b) == 2 && a > 0 && b > 0) g_grass.push_back({a, b});
            else if (ln[0] != '\n' && ln[0] != '#' && ln[0] != '\r') L("GRASSRULE ignored (cannot read it): %.40s", ln);
        }
        fclose(f);
        for (auto &r : g_grass) L("GRASSRULE loaded fmt=%d size=%d\n", r.fmt, r.sz);
    }
    g_grass_on = g_grass.empty() ? 0 : 1;
    if (FILE *ff = fopen(FULLF, "r")) {
        int fw = 0, fh = 0;
        if (fscanf(ff, "%d %d", &fw, &fh) == 2 && fw >= 640 && fw <= 8192 && fh >= 360 && fw >= fh) { g_fullw = fw; g_fullh = fh; }
        fclose(ff);
    }
    if (access(SCRASHF, F_OK) == 0) {           /* previous launch did not reach 120 frames with render scale active (crash OR closed early) */
        int prev = 0, fr = 0;
        FILE *mf = fopen(SCRASHF, "r");
        if (mf) { if (fscanf(mf, "%d %d", &prev, &fr) < 1) prev = 0; fclose(mf); }
        unlink(SCRASHF);
        /* v0.38: a launch that rendered 20+ frames was not a boot loop (the game was just closed): keep the user's scale. */
        if (fr >= 20) L("SAFETY: last launch rendered %d frames with render scale %d%% then ended: not treated as a crash, scale kept\n", fr, prev);
        else if (g_scale.load() > 0) {
            g_scale = 0;
            write_int_atomic(SCLF, 0);                 /* only scale.txt changes; no other setting is touched */
            L("SAFETY: last launch did not reach 120 frames with render scale %d%% on (crash or closed early) -> render scale forced OFF (scale.txt=0). Other settings unchanged.\n", prev);
            g_scale_safety = prev > 0 ? prev : 1;
        }
    }
    g_tstart = now_ns();
    f = fopen(FGLF, "r");                          /* v0.41: force OpenGL ES, default ON (a missing file counts as 1) */
    if (f) {
        int v = 1;
        if (fscanf(f, "%d", &v) == 1) g_forcegl = v ? 1 : 0;
        fclose(f);
    } else {
        g_forcegl = 1;
        f = fopen(FGLF, "w");
        if (f) { fprintf(f, "1\n"); fclose(f); }
    }
    f = fopen(COREF, "r");
    if (f) {
        int v = 1;
        if (fscanf(f, "%d", &v) == 1) g_core = v ? 1 : 0;
        fclose(f);
    } else {
        f = fopen(COREF, "w");
        if (f) { fprintf(f, "1\n"); fclose(f); }
    }
}

/* mode.txt:
     0 = idle
     1 = background thread only
     2 = thread + locate libunity (no hooks)
     3 = full hooks, installed by polling
     4 = hook libunity as soon as it loads (no polling thread) */
static int read_mode() {
    /* v26: the module always runs in mode 4. mode.txt is no longer a switch; it is only kept
       (and repaired here) so the WebUI shows the right value. A missing, empty or wrong file
       can no longer turn the module off. */
    int v = -1;
    FILE *f = fopen(MODEF, "r");
    if (f) {
        if (fscanf(f, "%d", &v) != 1) v = -1;
        fclose(f);
    }
    if (v != 4) {
        f = fopen(MODEF, "w");
        if (f) { fprintf(f, "4\n"); fclose(f); }
    }
    return 4;
}

/* hookdelay.txt: 0 = hook the moment libunity loads (default).
   N > 0 = wait N seconds after libunity appears, then hook. Slower, but can avoid
   clashes with other loaders / injectors that touch the same library at startup. */
static int read_hookdelay() {
    int v = 0;
    FILE *f = fopen(HDLYF, "r");
    if (f) {
        if (fscanf(f, "%d", &v) != 1) v = 0;
        fclose(f);
    }
    return std::max(0, std::min(v, 30));
}

/* Register every hook on a single libunity mapping */
static void reg_hooks(dev_t dv, ino_t in) {
    if (g_forcegl.load()) {
        lsplt::RegisterHook(dv, in, "dlopen", (void *)h_dlopen_u, (void **)&o_dlopen_u);
        lsplt::RegisterHook(dv, in, "android_dlopen_ext", (void *)h_adle_u, (void **)&o_adle_u);
    }
    lsplt::RegisterHook(dv, in, "dlsym", (void *)h_dlsym, (void **)&o_dlsym);
    lsplt::RegisterHook(dv, in, "eglGetProcAddress", (void *)h_egl, (void **)&o_egl);
    lsplt::RegisterHook(dv, in, "glTexStorage2D", (void *)h_st, (void **)&o_st);
    if (g_style.load() || g_flat.load() || g_testtex.load()) lsplt::RegisterHook(dv, in, "glTexParameterf", (void *)h_tpf, (void **)&o_tpf);
    if (g_near.load() || g_style.load() || g_flat.load() || g_testtex.load())
        lsplt::RegisterHook(dv, in, "glTexParameteri", (void *)h_tp, (void **)&real_tp);
    if (tex_track()) {
        lsplt::RegisterHook(dv, in, "glActiveTexture", (void *)h_act, (void **)&o_act);
        lsplt::RegisterHook(dv, in, "glBindTexture", (void *)h_bind, (void **)&o_bind);
        lsplt::RegisterHook(dv, in, "glDeleteTextures", (void *)h_del, (void **)&o_del);
        lsplt::RegisterHook(dv, in, "glSamplerParameteri", (void *)h_smp, (void **)&o_smp);
    }
    lsplt::RegisterHook(dv, in, "eglSwapBuffers", (void *)h_swap, (void **)&o_swap);
    lsplt::RegisterHook(dv, in, "eglCreateWindowSurface", (void *)h_cws, (void **)&o_cws);
    lsplt::RegisterHook(dv, in, "ANativeWindow_setBuffersGeometry", (void *)h_sbg, (void **)&o_sbg);
    if (g_fz.load()) {
        lsplt::RegisterHook(dv, in, "glCompileShader", (void *)h_comp, (void **)&o_comp);
        lsplt::RegisterHook(dv, in, "glLinkProgram", (void *)h_link, (void **)&o_link);
        lsplt::RegisterHook(dv, in, "glProgramBinary", (void *)h_pbin, (void **)&o_pbin);
        lsplt::RegisterHook(dv, in, "glBufferData", (void *)h_buf, (void **)&o_buf);
        lsplt::RegisterHook(dv, in, "glTexImage2D", (void *)h_ti2, (void **)&o_ti2);
        lsplt::RegisterHook(dv, in, "glCompressedTexImage2D", (void *)h_cti2, (void **)&o_cti2);
        lsplt::RegisterHook(dv, in, "glTexSubImage2D", (void *)h_tsi2, (void **)&o_tsi2);
        lsplt::RegisterHook(dv, in, "glCompressedTexSubImage2D", (void *)h_ctsi2, (void **)&o_ctsi2);
        lsplt::RegisterHook(dv, in, "glTexStorage3D", (void *)h_ts3, (void **)&o_ts3);
    } else if (g_grass_on.load()) {
        lsplt::RegisterHook(dv, in, "glCompressedTexImage2D", (void *)h_cti2, (void **)&o_cti2);
        lsplt::RegisterHook(dv, in, "glCompressedTexSubImage2D", (void *)h_ctsi2, (void **)&o_ctsi2);
    }
}

/* ---------- Mode 4: hook libunity right after it is loaded ---------- */
static std::atomic<bool> g_hooked{false};
static void *(*o_adle)(const char *, int, const android_dlextinfo *) = nullptr;
static void *h_adle(const char *path, int flags, const android_dlextinfo *info);

/* ---------- Coexistence diagnostics (v0.37, log only, no behaviour change) ----------
   chain_report: after CommitHook, say which library owns the function our hook will call as "original".
   A system library (libEGL, libandroid, libdl...) = clean. An app/anonymous mapping = another injector hooked that slot first.
   maps_report: executable mappings that are neither system libraries nor the game's own libs. */
static bool is_system_path(const std::string &p) {
    static const char *pre[] = {"/system/", "/system_ext/", "/apex/", "/vendor/", "/product/", "/odm/", "/linkerconfig/"};
    for (const char *x : pre) if (p.compare(0, strlen(x), x) == 0) return true;
    return false;
}
struct MapOwner { bool found = false; char perms[8] = {0}; std::string path; };
static MapOwner owner_of(const void *ptr) {
    MapOwner o;
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return o;
    char line[768];
    unsigned long long a = (unsigned long long)(uintptr_t)ptr;
    while (fgets(line, sizeof line, f)) {
        unsigned long long s0 = 0, e0 = 0; char pm[8] = {0}; int off = 0;
        if (sscanf(line, "%llx-%llx %7s %*s %*s %*s %n", &s0, &e0, pm, &off) < 3) continue;
        if (a < s0 || a >= e0) continue;
        o.found = true; memcpy(o.perms, pm, sizeof pm);
        o.path = off > 0 ? line + off : "";
        while (!o.path.empty() && (o.path.back() == '\n' || o.path.back() == ' ')) o.path.pop_back();
        break;
    }
    fclose(f);
    return o;
}
static void chain_report(const char *tag) {
    struct C { const char *name; void *ptr; };
    C c[] = {
        {"android_dlopen_ext", (void *)o_adle}, {"dlsym", (void *)o_dlsym}, {"eglGetProcAddress", (void *)o_egl},
        {"glTexStorage2D", (void *)o_st}, {"eglSwapBuffers", (void *)o_swap},
        {"eglCreateWindowSurface", (void *)o_cws}, {"ANativeWindow_setBuffersGeometry", (void *)o_sbg},
    };
    std::string foreign;
    for (auto &x : c) {
        if (!x.ptr) { L("CHAIN %s %s orig=null (not imported by the hooked library)\n", tag, x.name); continue; }
        MapOwner o = owner_of(x.ptr);
        bool fr = !o.found || o.path.empty() || !is_system_path(o.path);
        L("CHAIN %s %s orig=%p in=%s perms=%s foreign=%d\n", tag, x.name, x.ptr,
          o.found ? (o.path.empty() ? "[anon]" : o.path.c_str()) : "[unmapped]", o.perms, fr ? 1 : 0);
        if (fr) { if (!foreign.empty()) foreign += ","; foreign += x.name; }
    }
    st_set("chain_foreign", foreign.empty() ? "none" : foreign);
}
static void maps_report(const char *tag) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[768];
    std::set<std::string> seen;
    int anon_x = 0, shown = 0, total = 0;
    while (fgets(line, sizeof line, f)) {
        char pm[8] = {0}; int off = 0;
        if (sscanf(line, "%*[0-9a-fA-F]-%*[0-9a-fA-F] %7s %*s %*s %*s %n", pm, &off) < 1) continue;
        if (pm[2] != 'x') continue;                           /* executable mappings only */
        std::string p = off > 0 ? line + off : "";
        while (!p.empty() && (p.back() == '\n' || p.back() == ' ')) p.pop_back();
        if (p.empty()) { anon_x++; continue; }
        if (p[0] != '/' || is_system_path(p) || p.find(PKG) != std::string::npos) continue;
        if (!seen.insert(p).second) continue;
        total++;
        if (shown < 24) { shown++; L("MAPS %s non-system exec: %s\n", tag, p.c_str()); }
    }
    fclose(f);
    L("MAPS %s non_system_exec=%d anon_exec_no_path=%d\n", tag, total, anon_x);
    st_seti("maps_nonsys", total); st_seti("maps_anon_exec", anon_x);
}

static void install_unity_hooks() {
    if (g_hooked.exchange(true)) return;
    for (auto &m : lsplt::MapInfo::Scan()) {
        const std::string &p = m.path;
        if (p.size() < 12 || p.compare(p.size() - 12, 12, "/libunity.so") != 0) continue;
        st_set("stage", "hooking");              /* if the game dies inside CommitHook, codm_tex.status stops at "hooking" */
        maps_report("pre-commit");
        reg_hooks(m.dev, m.inode);
        L("pre-commit (unity)\n");
        bool ok = lsplt::CommitHook();
        g_hook_state = ok ? 1 : 2;
        st_set("hook", ok ? "committed" : "commit_failed"); st_set("stage", "hooked");
        L("commit=%d (unity)\n", (int)ok);
        chain_report("unity");
        return;
    }
    st_set("lib", "not_in_maps");
    L("libunity not found in maps\n");
    g_hooked = false;
}

static void *h_adle(const char *path, int flags, const android_dlextinfo *info) {
    { static std::atomic<int> first{0}; if (!first.exchange(1)) { st_set("adle_first", "1"); L("adle first call: %s\n", path ? path : "(null)"); } }
    auto oa = (o_adle && o_adle != h_adle) ? o_adle : nullptr;
    void *r = oa ? oa(path, flags, info) : android_dlopen_ext(path, flags, info);
    if (r && path) {
        size_t n = strlen(path);
        if (n >= 11 && !strcmp(path + n - 11, "libunity.so")) {
            st_set("lib", "loaded"); st_set("stage", "lib_loaded");
            L("dlopen libunity done: %s\n", path);
            install_unity_hooks();
        }
    }
    return r;
}

static void hook_nativeloader() {
    for (auto &m : lsplt::MapInfo::Scan()) {
        const std::string &p = m.path;
        if (p.size() < 19 || p.compare(p.size() - 19, 19, "/libnativeloader.so") != 0) continue;
        lsplt::RegisterHook(m.dev, m.inode, "android_dlopen_ext", (void *)h_adle, (void **)&o_adle);
        st_set("stage", "nl_hooking");
        bool ok = lsplt::CommitHook();
        L("nativeloader hook commit=%d\n", (int)ok);
        st_set("nl_hook", ok ? "committed" : "commit_failed"); st_set("stage", "nl_hooked");
        return;
    }
    L("libnativeloader not found\n");
    st_set("nl_hook", "not_found");
}

static void log_start(int mode) {
#if defined(__aarch64__)
    const char *abi = "arm64-v8a";
#elif defined(__arm__)
    const char *abi = "armeabi-v7a";
#else
    const char *abi = "other";
#endif
    L("=== start %s build=%s %s abi=%s mode=%d bias=%d fpscap=%d scale=%d fz=%d th=%d core=%d near=%d blk=%d min=%d delay=%d skip=%d style=%d sedge=%d flat=%d testtex=%d ttsize=%d ttsm=%d forcegl=%d builtin=%d alpha=%d grass=%d ===\n",
      VERSION, __DATE__, __TIME__, abi, mode, g_bias.load(), g_fps.load(), g_scale.load(), g_fz.load(), g_th.load(), g_core.load(), g_near.load(), g_blk.load(), g_minsz.load(), g_delay.load(), (int)g_skip.size(), g_style.load(), g_sedge.load(), g_flat.load(), g_testtex.load(), g_ttsize.load(), g_ttsm.load(), g_forcegl.load(), (int)(sizeof g_builtin / sizeof g_builtin[0]), g_alpha.load(), (int)g_grass.size());
    st_set("ver", VERSION); st_seti("pid", (long long)getpid()); st_set("abi", abi); st_set("stage", "started");
    st_set("forcegl", g_forcegl.load() ? "on" : "off");
    st_set("alpha", g_alpha.load() ? "keep" : "off"); st_seti("grass_rules", (long long)g_grass.size());
    st_seti("started", (long long)time(nullptr));
    st_seti("live_fps", g_fps.load()); st_set("dir", g_dir); st_set("live", "1");
    st_set("scale_safety", g_scale_safety > 0 ? "forced_off_prev_" + std::to_string(g_scale_safety) : "no");
    st_set("cfg_bad", g_cfg_note.empty() ? "none" : g_cfg_note); st_seti("cfg_missing", g_cfg_missing);
    if (!g_cfg_note.empty()) L("CFG invalid or missing settings were replaced by defaults: %s\n", g_cfg_note.c_str());
    if (g_delay.load() > 0) L("NOTE delay=%d: textures created in the first %d s are modified once the delay is over (v0.38 revisits them)\n", g_delay.load(), g_delay.load());
}

/* ---------- Crash-in-hook breaker (v0.40) ----------
   The status file is rewritten at every stage. If the previous launch left "hooking" / "nl_hooking" behind, the game
   died inside CommitHook. Hooks are then skipped (hooksafe.txt=1) so the game starts clean; the WebUI shows a banner
   and a "Retry hooks" button that writes hooksafe.txt=0. */
static int g_safe = 0;
static std::string g_safe_why = "no";
static std::string read_status_key(const char *key) {
    FILE *f = fopen(STATF, "r");
    if (!f) return "";
    char line[256]; std::string out; size_t kl = strlen(key);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, kl) == 0 && line[kl] == '=') { out = line + kl + 1; while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back(); break; }
    }
    fclose(f);
    return out;
}
static void decide_safe(const std::string &prev_stage) {
    g_safe = 0; g_safe_why = "no";
    int v = 0;
    if (FILE *f = fopen(HSAFEF, "r")) { if (fscanf(f, "%d", &v) != 1) v = 0; fclose(f); }
    if (v == 1) { g_safe = 1; g_safe_why = "hooksafe_file"; }
    else if (prev_stage == "hooking" || prev_stage == "nl_hooking") {
        g_safe = 1; g_safe_why = "crash_in_" + prev_stage;
        write_int_atomic(HSAFEF, 1);
    }
    st_seti("hooksafe", g_safe); st_set("safe_why", g_safe_why);
    if (g_safe) { st_set("stage", "safe_mode"); __android_log_print(ANDROID_LOG_WARN, "codm_tex", "%s safe mode: hooks skipped (%s)", VERSION, g_safe_why.c_str()); }
}

static void run(int mode) {
    load_cfg();
    std::set<std::pair<dev_t, ino_t>> done;
    std::map<std::pair<dev_t, ino_t>, int64_t> first;   /* when libunity was first seen */
    log_start(mode);
    if (g_safe) { L("SAFE MODE: hooks skipped (%s). Press Retry hooks in the WebUI to try again.\n", g_safe_why.c_str()); st_set("stage", "safe_mode"); return; }
    int hd = read_hookdelay();
    int64_t settle_ns = (int64_t)std::max(3, hd) * 1000000000LL;   /* wait this long after libunity appears */
    if (mode >= 3) L("HOOK late: %d s after libunity appears\n", std::max(3, hd));
    int total = (mode == 1) ? 60 : 1200;
    for (int i = 0; i < total; i++) {
        if (mode >= 2) {
            bool added = false;
            for (auto &m : lsplt::MapInfo::Scan()) {
                const std::string &p = m.path;
                if (p.size() < 12 || p.compare(p.size() - 12, 12, "/libunity.so") != 0) continue;
                std::pair<dev_t, ino_t> key{m.dev, m.inode};
                if (done.count(key)) continue;
                auto it = first.find(key);
                if (it == first.end()) { first[key] = now_ns(); st_set("lib", "seen"); st_set("stage", "lib_loaded"); L("SEEN libunity i=%d\n", i); continue; }
                if (now_ns() - it->second < settle_ns) continue;   /* give libunity time to finish loading */
                done.insert(key);
                if (mode == 2) { L("mode2: stable, no hooks\n"); continue; }
                reg_hooks(m.dev, m.inode);
                added = true;
            }
            if (added) {
                L("pre-commit\n");
                st_set("stage", "hooking");
                maps_report("pre-commit-late");
                bool ok = lsplt::CommitHook();
                g_hook_state = ok ? 1 : 2;
                st_set("hook", ok ? "committed" : "commit_failed"); st_set("stage", "hooked");
                L("commit=%d\n", (int)ok);
                chain_report("late");
            }
        }
        sleep(1);
    }
}

class M : public zygisk::ModuleBase {
    zygisk::Api *api; JNIEnv *env; bool target = false;
public:
    void onLoad(zygisk::Api *a, JNIEnv *e) override { api = a; env = e; }
    void preAppSpecialize(zygisk::AppSpecializeArgs *args) override {
        const char *n = env->GetStringUTFChars(args->nice_name, nullptr);
        target = n && strcmp(n, PKG) == 0;
        g_app_dir[0] = 0;
        if (!target && args->app_data_dir) {          /* clones / secondary users may use another process name */
            const char *d = env->GetStringUTFChars(args->app_data_dir, nullptr);
            if (d && strstr(d, PKG)) target = true;
            if (d) env->ReleaseStringUTFChars(args->app_data_dir, d);
        }
        if (target && args->app_data_dir) {               /* v0.40: remember where this copy of the game keeps its data */
            const char *d = env->GetStringUTFChars(args->app_data_dir, nullptr);
            if (d && strlen(d) < sizeof g_app_dir) snprintf(g_app_dir, sizeof g_app_dir, "%s", d);
            if (d) env->ReleaseStringUTFChars(args->app_data_dir, d);
        }
        /* Breadcrumb in logcat (tag codm_tex) for the game and anything that looks like it, so a
           "module never loaded" report can be told apart from "process name did not match". */
        if (n && (target || strstr(n, "garena") || strstr(n, "codm")))
            __android_log_print(ANDROID_LOG_INFO, "codm_tex", "%s preAppSpecialize nice_name=%s uid=%d target=%d", VERSION, n, (int)args->uid, (int)target);
        if (n) env->ReleaseStringUTFChars(args->nice_name, n);
        if (!target) api->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs *) override {
        if (!target) return;
        /* First thing the game process does: leave proof in logcat and in the status file, before any
           other settings / log work, so "module never loaded" can be told apart from "loaded, no log". */
        set_dir(g_app_dir[0] ? g_app_dir : nullptr);
        std::string prev_stage = read_status_key("stage");      /* BEFORE the first st_set below overwrites the file */
        int dir_ok = access(g_dir, W_OK) == 0;
        __android_log_print(ANDROID_LOG_INFO, "codm_tex", "%s postAppSpecialize target pid=%d dir_writable=%d", VERSION, (int)getpid(), dir_ok);
        st_set("ver", VERSION); st_seti("pid", (long long)getpid()); st_set("stage", "injected");
        st_seti("injected", (long long)time(nullptr)); st_set("dir_writable", dir_ok ? "yes" : "no"); st_set("dir", g_dir);
        st_set("live", "1");
        decide_safe(prev_stage);
        int mode = read_mode();
        if (mode == 4) {
            if (g_safe) { load_cfg(); log_start(4); L("SAFE MODE: hooks skipped (%s). Press Retry hooks in the WebUI to try again.\n", g_safe_why.c_str()); st_set("stage", "safe_mode"); return; }
            if (read_hookdelay() > 0) { std::thread(run, 3).detach(); return; }   /* late hooking: run() logs its own start line */
            load_cfg();
            log_start(4);
            L("HOOK early (at libunity load)\n");
            hook_nativeloader();
            return;
        }
        if (mode >= 1) std::thread(run, mode).detach();
    }
};
REGISTER_ZYGISK_MODULE(M)
