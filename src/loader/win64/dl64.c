// libdl (see ll64.h): the game's own Linux libraries are loaded as ELF
// modules; the system ones it opens (libX11, libGL, PulseAudio...) are
// bridges, handed out as handles of their own.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "ll64.h"

typedef struct
{
    const char *name;
    // Unknown symbols: a stub (all of the library is needed), or NULL (its
    // users check).
    int stubUnknown;
} VirtualLib;

static const VirtualLib virtualLibs[] = {
    {"libX11.so.6", 1}, {"libX11.so", 1}, {"libXext.so.6", 0}, {"libGL.so.1", 0}, {"libGL.so", 0},
{"libudev.so.1", 0}, {"libudev.so.0", 0},
    {"libc.so.6", 0}, {"libm.so.6", 0}, {"libpthread.so.0", 0}, {"libdl.so.2", 0}, {"librt.so.1", 0},
    {"libGLU.so.1", 0}, {"libasound.so.2", 0},
};

static char globalHandle;
static __thread char dlError[512];
static __thread int dlErrorSet;

void *(*lxDlsymHook)(const char *name);

static int isVirtual(const void *h)
{
    return (const char *)h >= (const char *)virtualLibs && (const char *)h < (const char *)(virtualLibs + LX_COUNT(virtualLibs));
}

static SYSV void *lx_dlopen(const char *name, int flags)
{
    const char *base;
    LxModule *m;
    (void)flags;
    if (!name)
        return &globalHandle;
    base = strrchr(name, '/');
    base = base ? base + 1 : name;
    for (size_t i = 0; i < LX_COUNT(virtualLibs); i++)
        if (!strcmp(base, virtualLibs[i].name))
        {
            lxTraceLog("dlopen %s: the bridge\n", name);
            return (void *)&virtualLibs[i];
        }
    if ((m = lxFindModule(base)) || (m = lxLoadLibrary(name)) || (m = lxLoadLibrary(base)))
    {
        lxLog("dlopen %s: %s\n", name, lxModulePath(m));
        return m;
    }
    lxLog("dlopen %s: not found\n", name);
    snprintf(dlError, sizeof(dlError), "%s: cannot open shared object file: No such file or directory", name);
    dlErrorSet = 1;
    return NULL;
}

static SYSV void *lx_dlsym(void *h, const char *name)
{
    void *p = NULL;
    if (lxDlsymHook && (p = lxDlsymHook(name)))
        return p;
    if (!h || h == &globalHandle || h == (void *)-1)
        p = lxGlobalSymbol(name);
    else if (isVirtual(h))
    {
        const VirtualLib *v = h;
        static const char *const extensions[] = {"XRR", "XShm", "XF86VidMode", "XShape", "Xinerama", "XScreenSaver",
                                                 "XI", "XFixes", "Xcursor", "Xext", "XSS", "XSync", "Xdbe"};
        int isExtension = 0;
        for (size_t i = 0; i < LX_COUNT(extensions); i++)
            isExtension |= !strncmp(name, extensions[i], strlen(extensions[i]));
        p = lxBridge(name);
        if (!p && v->stubUnknown && !isExtension)
        {
            lxLog("dlsym %s:%s: a stub\n", v->name, name);
            p = lxUnresolvedStub(name);
        }
    }
    else
    {
        p = lxModuleSymbol(h, name);
        if (!p)
            p = lxGlobalSymbol(name);
    }
    if (!p)
    {
        snprintf(dlError, sizeof(dlError), "undefined symbol: %s", name);
        dlErrorSet = 1;
    }
    lxTraceLog("dlsym %s -> %p\n", name, p);
    return p;
}

static SYSV void *lx_dlvsym(void *h, const char *name, const char *version)
{
    (void)version;
    return lx_dlsym(h, name);
}

static SYSV int lx_dlclose(void *h) { (void)h; return 0; }

static SYSV char *lx_dlerror(void)
{
    if (!dlErrorSet)
        return NULL;
    dlErrorSet = 0;
    return dlError;
}

typedef struct
{
    const char *fname;
    void *fbase;
    const char *sname;
    void *saddr;
} LxDlInfo;

static SYSV int lx_dladdr(const void *addr, LxDlInfo *info)
{
    LxModule *m = lxModuleAt(addr);
    const char *sname = NULL;
    void *saddr = NULL;
    if (!m)
        return 0;
    lxModuleAddr(addr, &sname, &saddr);
    info->fname = lxModulePath(m);
    info->fbase = lxModuleBase(m);
    info->sname = sname;
    info->saddr = saddr;
    return 1;
}
static SYSV int lx_dladdr1(const void *addr, LxDlInfo *info, void **extra, int flags)
{
    (void)flags;
    if (extra)
        *extra = NULL;
    return lx_dladdr(addr, info);
}

static SYSV int lx_dl_iterate_phdr(LxPhdrCallback cb, void *data) { return lxIteratePhdr(cb, data); }

typedef struct
{
    unsigned long long flags;
    void *mapStart, *mapEnd;
    void *linkMap;
    void *ehFrame;
    unsigned long long reserved[7];
} LxFindObject;

static SYSV int lx_dl_find_object(void *pc, LxFindObject *r)
{
    LxModule *m;
    if (lxFindObject(pc, &r->mapStart, &r->mapEnd, &r->ehFrame, &m))
        return -1;
    r->flags = 0;
    r->linkMap = m;
    return 0;
}

static const LxSym syms[] = {
    {"dlopen", lx_dlopen}, {"dlsym", lx_dlsym}, {"dlvsym", lx_dlvsym}, {"dlclose", lx_dlclose},
    {"dlerror", lx_dlerror}, {"dladdr", lx_dladdr}, {"dladdr1", lx_dladdr1}, {"dl_iterate_phdr", lx_dl_iterate_phdr},
    {"_dl_find_object", lx_dl_find_object},
};

void lxDlInit(void)
{
    lxRegister(syms, LX_COUNT(syms));
}
