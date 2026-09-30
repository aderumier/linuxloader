// Prototype: Halo (64-bit dump): import table rebuild and HASP emulation.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <glad/gl.h>
#include "imports.h"
#include "../../config/iniParser.h"
#include "../../graphics/borderFrame.h"
#include "../../graphics/frameScale.h"

static void detour(uintptr_t at, void *to)
{
    uint8_t *p = (uint8_t *)at;
    p[0] = 0xff; p[1] = 0x25; memset(p + 2, 0, 4);   // jmp *0(%rip)
    memcpy(p + 6, &to, 8);
}

static int haspOk(void) { return 0; }
static int one(void) { return 1; }
static int haspLogin(uint32_t feature, const void *vc, uint32_t *handle)
{
    fprintf(stderr, "halo_rt: hasp_login feature %#x\n", feature);
    *handle = 1;
    return 0;
}
static int haspSessionInfo(uint32_t h, const char *fmt, char **info)
{
    (void)h;
    fprintf(stderr, "halo_rt: hasp_get_sessioninfo %.60s\n", fmt);
    *info = strdup("<?xml version=\"1.0\" encoding=\"UTF-8\" ?><hasp_info><hasp id=\"1234567\"><haspid>1234567</haspid></hasp></hasp_info>");
    return 0;
}
static int haspFree(void *p) { free(p); return 0; }

static long envNum(const char *name, long def)
{
    const char *v = getenv(name);
    return v && *v ? strtol(v, NULL, 0) : def;
}
// The cabinet's identity lives in the dongle's memory file 0xfff4, and the g7
// runtime reads it before anything else: byte 40 is the factory setup flag
// (set by the test menu's factory setup, checked by the attract mode), 41 the
// cabinet type, 42 the region, 43..46 the serial number.  A dongle that reads
// back as zeroes leaves the cabinet type at 0 ("Unknown"), so the conf-group
// registration skips MODEL_CONFS/GUNS/HITFX_DAMAGE_TEMPLATES/PROGRAM entirely,
// GameLoadConf() cannot find MODEL_CONFS and fails, and the game takes itself
// down through a shutdown path that joins a thread it never started.
// Halo's four cabinets, as TeknoParrot names them:
//   3 = Super Deluxe    "SUPER DELUXE TETHERED"  4 players, data/prod/conf
//   4 = Mounted Gun     "STANDARD MOUNTED"       2 players, data/prod/conf_2p
//   8 = 55" Dedicated   "STANDARD TETHERED"      2 players, data/prod/conf_2p
//   9 = Dual Screen     "DUAL-SCREEN MOUNTED"    4 players, data/prod/conf
// The default is the single-screen 4 player cabinet; HALO_CABINET_TYPE
// picks another.
static long cabinetType(void)
{
    return envNum("HALO_CABINET_TYPE", 3);
}
static int cabinetPlayers(void)
{
    long cabinet = cabinetType();
    return cabinet == 4 || cabinet == 8 ? 2 : 4;
}
// The cabinet with the operator's test, service and volume buttons: a 4
// player cabinet is two side by side, and its menus answer to the right
// one's (cabinet 1, the P3/P4 side).
static int operatorCabinet(void)
{
    return cabinetPlayers() == 4 ? 1 : 0;
}
// What the game writes there is kept, and saved in the game's folder
// (dongle_memory.dmp, loaded from init) for the next runs, as the loader
// does for the other games (rtDongle.c).
static uint8_t dongleMem[64];
static char dongleMemPath[PATH_MAX];
static void dongleMemLoad(void)
{
    uint32_t serial = (uint32_t)envNum("HALO_SERIAL_NO", 1234567);
    FILE *f;
    dongleMem[41] = (uint8_t)cabinetType();
    dongleMem[42] = (uint8_t)envNum("HALO_REGION", 0);
    memcpy(dongleMem + 43, &serial, sizeof(serial));
    if ((f = fopen(dongleMemPath, "rb")))
    {
        uint8_t saved[sizeof(dongleMem)];
        if (fread(saved, 1, sizeof(saved), f) == sizeof(saved))
        {
            // The cabinet stays the one asked for.
            saved[41] = dongleMem[41];
            memcpy(dongleMem, saved, sizeof(saved));
        }
        fclose(f);
    }
}
static int haspRead(uint32_t h, uint32_t file, uint32_t off, uint32_t len, void *buf)
{
    (void)h;
    memset(buf, 0, len);
    if (file == 0xfff4)
    {
        if (off < sizeof(dongleMem))
            memcpy(buf, dongleMem + off, len < sizeof(dongleMem) - off ? len : sizeof(dongleMem) - off);
    }
    fprintf(stderr, "halo_rt: hasp_read file %#x off %u len %u -> %#x\n", file, off, len,
            *(const uint8_t *)buf);
    return 0;
}
static int haspWrite(uint32_t h, uint32_t file, uint32_t off, uint32_t len, const void *buf)
{
    FILE *f;
    uint32_t n = len < sizeof(dongleMem) - off ? len : sizeof(dongleMem) - off;
    (void)h;
    fprintf(stderr, "halo_rt: hasp_write file %#x off %u len %u\n", file, off, len);
    if (file != 0xfff4 || off >= sizeof(dongleMem) || !memcmp(dongleMem + off, buf, n))
        return 0;
    memcpy(dongleMem + off, buf, n);
    if (!(f = fopen(dongleMemPath, "wb")))
    {
        fprintf(stderr, "halo_rt: cannot save the dongle memory %s\n", dongleMemPath);
        return 0;
    }
    fwrite(dongleMem, 1, sizeof(dongleMem), f);
    fclose(f);
    return 0;
}
static unsigned char *(*sha256)(const unsigned char *, size_t, unsigned char *);
// The recorded answers: pm/g7/dump, ../dump from the game in the cabinet's
// layout (see init).
static const char *dumpDir = "../dump";
static int haspCrypt(const char *what, uint32_t h, uint8_t *buf, uint32_t len)
{
    unsigned char md[32];
    char name[256], hex[65];
    (void)h;
    if (!sha256)
    {
        void *c = dlopen("libcrypto.so.3", RTLD_NOW);
        if (c) *(void **)&sha256 = dlsym(c, "SHA256");
    }
    hex[0] = 0;
    if (sha256)
    {
        sha256(buf, len, md);
        for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", md[i]);
    }
    snprintf(name, sizeof(name), "%s/%s", dumpDir, len == 128 ? "93b321f9dbfb5ca174d6dc8974be46623100d97525e2441287c72a2e9884dad4" : len == 64 ? "9e6dac279ad0a5d8bcdd14fd81b4067767bafc26dfe3b68e11431b1bc1e355ce" : hex);
    FILE *f = fopen(name, "rb");
    size_t got = 0;
    if (f) { got = fread(buf, 1, len, f); fclose(f); }
    fprintf(stderr, "halo_rt: %s len %u sha256 %s -> %s\n", what, len, hex, f ? (got == len ? "answered" : "size mismatch") : "no answer");
    return 0;
}
static int haspEncrypt(uint32_t h, uint8_t *b, uint32_t l) { return haspCrypt("encrypt", h, b, l); }
static int haspDecrypt(uint32_t h, uint8_t *b, uint32_t l) { return haspCrypt("decrypt", h, b, l); }

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
// Cabinet paths: /pm is the game's pm folder, and the game lives in
// /pm/g7/halo. Either the install keeps that layout (the game started from
// pm/g7/halo, /pm two levels up), or it is flattened: the game's folder is
// both /pm/g7/halo and /pm (see init).
#define HALO_DIR "/pm/g7/halo"
static char root[PATH_MAX], halo[PATH_MAX];
static const char *mapPath(const char *p, char *buf)
{
    // Halo's game-specific shaders include the shared g7 shader headers
    // through gameshaders/include, while this dump installs them under
    // shaders/include (asked for by the cabinet path, or already mapped).
    static const char gameShaderIncludes[] = "/data/prod/gameshaders/include/";
    const char *include = p ? strstr(p, gameShaderIncludes) : NULL;
    if (include)
    {
        snprintf(buf, PATH_MAX, "%s/data/prod/shaders/include/%s", halo, include + sizeof(gameShaderIncludes) - 1);
        return buf;
    }
    // The file check hashes the executable: the original, not the dump.
    if (p && !strcmp(p, HALO_DIR "/game"))
    {
        snprintf(buf, PATH_MAX, "%s/game.orig", halo);
        return buf;
    }
    if (p && !strncmp(p, HALO_DIR, strlen(HALO_DIR)) && (p[strlen(HALO_DIR)] == '/' || !p[strlen(HALO_DIR)]))
    {
        snprintf(buf, PATH_MAX, "%s%s", halo, p + strlen(HALO_DIR));
        return buf;
    }
    if (p && !strncmp(p, "/pm", 3) && (p[3] == '/' || !p[3]))
    {
        snprintf(buf, PATH_MAX, "%s%s", root, p + 3);
        return buf;
    }
    return p;
}
static char *mapCommand(const char *cmd, char *buf, size_t size)
{
    size_t o = 0;
    for (const char *c = cmd; *c && o + 1 < size;)
    {
        if (!strncmp(c, "/pm/", 4) && (c == cmd || strchr(" \t\"'=", c[-1])))
        {
            int inHalo = !strncmp(c, HALO_DIR "/", strlen(HALO_DIR) + 1);
            o += snprintf(buf + o, size - o, "%s", inHalo ? halo : root);
            c += inHalo ? strlen(HALO_DIR) : 3;
            continue;
        }
        buf[o++] = *c++;
    }
    buf[o] = 0;
    return buf;
}
#define NEXT(name) static __typeof__(name) *real; if (!real) real = dlsym(RTLD_NEXT, #name)
static FILE *gamePopen(const char *cmd, const char *type)
{
    char b[PATH_MAX * 2];
    if (strstr(cmd, "/dev/video"))  // the room camera (player photos): none
        return popen("true", type);
    return popen(mapCommand(cmd, b, sizeof(b)), type);
}
static int gameSystem(const char *cmd) { char b[PATH_MAX * 2]; return system(mapCommand(cmd, b, sizeof(b))); }
static int gameOpen(const char *path, int flags, ...)
{
    char b[PATH_MAX];
    va_list ap; va_start(ap, flags); int mode = va_arg(ap, int); va_end(ap);
    if (!strncmp(path, "/dev/video", 10)) { errno = ENOENT; return -1; }
    return open(mapPath(path, b), flags, mode);
}
static int gameOpen2(const char *path, int flags) { return gameOpen(path, flags, 0); }
static FILE *gameFopen(const char *path, const char *m) { char b[PATH_MAX]; return fopen(mapPath(path, b), m); }
static DIR *gameOpendir(const char *path) { char b[PATH_MAX]; return opendir(mapPath(path, b)); }
static int gameAccess(const char *path, int m) { char b[PATH_MAX]; return access(mapPath(path, b), m); }
static int gameChdir(const char *path) { char b[PATH_MAX]; return chdir(mapPath(path, b)); }
static int gameMkdir(const char *path, mode_t m) { char b[PATH_MAX]; return mkdir(mapPath(path, b), m); }
static int gameRmdir(const char *path) { char b[PATH_MAX]; return rmdir(mapPath(path, b)); }
static int gameUnlink(const char *path) { char b[PATH_MAX]; return unlink(mapPath(path, b)); }
static int gameRename(const char *a, const char *z) { char b[PATH_MAX], c[PATH_MAX]; return rename(mapPath(a, b), mapPath(z, c)); }
static ssize_t gameReadlink(const char *path, char *o, size_t n) { char b[PATH_MAX]; return readlink(mapPath(path, b), o, n); }
static int gameSymlink(const char *t, const char *path) { char b[PATH_MAX]; return symlink(t, mapPath(path, b)); }
static char *gameRealpath(const char *path, char *o, size_t n) { char b[PATH_MAX]; (void)n; return realpath(mapPath(path, b), o); }
// FMOD Studio opens the sound banks itself, on its loading thread, from the
// path the game gives it (FMOD::Studio::System::loadBankFile(path, flags,
// bank)): mapped here, since FMOD's own opens are not the game's.
#define FMOD_LOAD_BANK_FILE "_ZN4FMOD6Studio6System12loadBankFileEPKcjPPNS0_4BankE"
static int gameLoadBankFile(void *system, const char *path, unsigned flags, void **bank)
{
    static int (*real)(void *, const char *, unsigned, void **);
    char b[PATH_MAX];
    if (!real) *(void **)&real = dlsym(RTLD_DEFAULT, FMOD_LOAD_BANK_FILE);
    return real(system, mapPath(path, b), flags, bank);
}
static int (*xstat)(int, const char *, struct stat *), (*lxstat)(int, const char *, struct stat *);
static int gameXstat(int v, const char *path, struct stat *st) { char b[PATH_MAX]; return xstat(v, mapPath(path, b), st); }
static int gameLxstat(int v, const char *path, struct stat *st) { char b[PATH_MAX]; return lxstat(v, mapPath(path, b), st); }


// The loader's configuration: the linuxloader.ini the 32-bit loader reads for
// the other games, named by LINUXLOADER_CONFIG (else linuxloader.ini in the
// game directory, if there is one).  Halo takes from it:
//   [Display] WIDTH, HEIGHT, FULLSCREEN, KEEP_ASPECT_RATIO, BORDER_ENABLED,
//             WHITE_BORDER_PERCENTAGE, BLACK_BORDER_PERCENTAGE
//   [Input]   INPUT_MODE (2: the guns and buttons come from [EVDEV])
//   [EVDEV]   see the evdev input below
static IniConfig *ini;
static struct
{
    int width, height, fullscreen, borderEnabled, keepAspectRatio, inputMode;
    float whiteBorder, blackBorder;
} cfg = {0, 0, 1, 0, 1, 1, 0.02f, 0.0f};

static const char *iniValue(const char *section, const char *key)
{
    const char *v = ini ? iniGetValue(ini, section, key) : NULL;
    return v && *v ? v : NULL;
}
static int iniInt(const char *section, const char *key, int def)
{
    const char *v = iniValue(section, key);
    if (!v) return def;
    if (!strcasecmp(v, "true")) return 1;
    if (!strcasecmp(v, "false")) return 0;
    if (!strcasecmp(v, "auto")) return def;
    return atoi(v);
}
static float iniFloat(const char *section, const char *key, float def)
{
    const char *v = iniValue(section, key);
    return v ? (float)atof(v) : def;
}

int logGeneric(int level, const char *file, int line, const char *message, ...)
{
    va_list ap;
    (void)level; (void)file; (void)line;
    fprintf(stderr, "halo_rt: ");
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    return 0;
}

static void loadConfig(void)
{
    const char *path = getenv("LINUXLOADER_CONFIG");
    if (!path || !*path)
    {
        if (access("linuxloader.ini", R_OK) != 0)
            return;
        path = "linuxloader.ini";
    }
    if (!(ini = iniLoad(path)))
        return;
    cfg.width = iniInt("Display", "WIDTH", cfg.width);
    cfg.height = iniInt("Display", "HEIGHT", cfg.height);
    cfg.fullscreen = iniInt("Display", "FULLSCREEN", cfg.fullscreen);
    cfg.borderEnabled = iniInt("Display", "BORDER_ENABLED", cfg.borderEnabled);
    cfg.whiteBorder = iniFloat("Display", "WHITE_BORDER_PERCENTAGE", cfg.whiteBorder * 100.f) / 100.f;
    cfg.blackBorder = iniFloat("Display", "BLACK_BORDER_PERCENTAGE", cfg.blackBorder * 100.f) / 100.f;
    cfg.keepAspectRatio = iniInt("Display", "KEEP_ASPECT_RATIO", cfg.keepAspectRatio);
    cfg.inputMode = iniInt("Input", "INPUT_MODE", cfg.inputMode);
    fprintf(stderr, "halo_rt: config %s: %dx%d%s, border %s, %s input\n", path, cfg.width, cfg.height,
            cfg.fullscreen ? " fullscreen" : "", cfg.borderEnabled ? "on" : "off",
            cfg.inputMode == 2 ? "evdev" : "keyboard/mouse");
}

// Video mode.  The game renders at the size the command-line parser at
// 0x435560 leaves in its video settings at 0x1749f80:
//   +0x0 window mode   0 windowed, 1 borderless, 2 fullscreen (SDL flags 2/0x12/3)
//   +0x4 width, +0x8 height, +0xc aspect (float)
// "-f<w>x<h>" sets them to a fullscreen w x h; without it the game falls back
// to a 1280x720 window (2586x720 on the dual-screen cabinet).  Set them to
// [Display] WIDTH/HEIGHT instead, which Batocera sets to the screen's, so the
// game renders at the monitor's own resolution and fills it.  FULLSCREEN 0
// gives a borderless window of that size, as the other games get.
#define VIDEO_MODE   (*(int32_t *)0x1749f80)
#define VIDEO_WIDTH  (*(int32_t *)0x1749f84)
#define VIDEO_HEIGHT (*(int32_t *)0x1749f88)
#define VIDEO_ASPECT (*(float *)0x1749f8c)

static void (*parseArgsOrig)(int, char **);
static void parseArgs(int argc, char **argv)
{
    parseArgsOrig(argc, argv);
    if (cfg.width <= 0 || cfg.height <= 0)
        return;
    VIDEO_MODE = cfg.fullscreen ? 2 : 1;
    VIDEO_WIDTH = cfg.width;
    VIDEO_HEIGHT = cfg.height;
    VIDEO_ASPECT = (float)cfg.width / (float)cfg.height;
    fprintf(stderr, "halo_rt: video mode %dx%d%s\n", cfg.width, cfg.height,
            cfg.fullscreen ? " fullscreen" : " borderless");
}

// The frame on its way to the screen: scaled to the window once the window
// no longer matches the game's size (see frameScale.h), and the light gun
// border ([Display] BORDER_ENABLED) drawn around it.  The game's statically
// linked SDL looks glXSwapBuffers up through glXGetProcAddressARB (and could
// through dlsym), and each X function through dlsym: both hand it the
// loader's swap instead, and the GL entry points the scaler redirects.
static void *(*realGetProcAddress)(const char *);
static void (*realSwap)(void *, unsigned long);
static int (*swapGetGeometry)(void *, unsigned long, unsigned long *, int *, int *,
                              unsigned int *, unsigned int *, unsigned int *, unsigned int *);
static unsigned long gameWindow;   // the drawable last presented

static void gameSwap(void *display, unsigned long drawable)
{
    static int glLoaded;
    unsigned long root;
    int x, y, bordered = 0;
    unsigned int w, h, bw, depth;

    if (!glLoaded)
    {
        void *x11 = dlopen("libX11.so.6", RTLD_NOW);
        *(void **)&swapGetGeometry = x11 ? dlsym(x11, "XGetGeometry") : NULL;
        glLoaded = realGetProcAddress && swapGetGeometry && gladLoadGL((GLADloadfunc)realGetProcAddress) ? 1 : -1;
        if (glLoaded < 0)
            fprintf(stderr, "halo_rt: cannot load the GL entry points, no border or scaling\n");
    }
    gameWindow = drawable;
    if (glLoaded > 0 && swapGetGeometry(display, drawable, &root, &x, &y, &w, &h, &bw, &depth) &&
        !frameScalePresent(0, 0, (int)w, (int)h, cfg.borderEnabled, cfg.whiteBorder, cfg.blackBorder) &&
        cfg.borderEnabled)
    {
        borderFrameBegin((int)w, (int)h, 1, cfg.whiteBorder, cfg.blackBorder);
        bordered = 1;
    }
    realSwap(display, drawable);
    if (bordered)
        borderFrameEnd();
}

static void *glEntry(const char *name, void *real)
{
    void *wrapper;
    if (!real || !name)
        return real;
    if (!strcmp(name, "glXSwapBuffers"))
    {
        realSwap = real;
        return (void *)gameSwap;
    }
    wrapper = frameScaleWrapper(name);
    return wrapper ? wrapper : real;
}

static void *gameGetProcAddress(const char *name)
{
    return glEntry(name, realGetProcAddress(name));
}

// SDL pins a window it was not asked to make resizable at its size, with
// equal minimum and maximum size hints (XSetWMProperties, then
// XSetWMNormalHints): drop them, so that the window can be resized and the
// frame scales with it.
static int (*realSetWMNormalHints)(void *, unsigned long, long *);
static int setWMNormalHints(void *display, unsigned long window, long *hints)
{
    if (hints)
        hints[0] &= ~((1L << 4) | (1L << 5));   // XSizeHints.flags: PMinSize, PMaxSize
    return realSetWMNormalHints(display, window, hints);
}
// SDL sets them first with the other properties.
static void (*realSetWMProperties)(void *, unsigned long, void *, void *, char **, int, long *, void *, void *);
static void setWMProperties(void *display, unsigned long window, void *name, void *iconName, char **argv, int argc,
                            long *hints, void *wmHints, void *classHints)
{
    if (hints)
        hints[0] &= ~((1L << 4) | (1L << 5));
    realSetWMProperties(display, window, name, iconName, argv, argc, hints, wmHints, classHints);
}

static void *gameDlsym(void *handle, const char *name)
{
    void *p = dlsym(handle, name);
    if (p && name && !strcmp(name, "XSetWMNormalHints"))
    {
        realSetWMNormalHints = p;
        return (void *)setWMNormalHints;
    }
    if (p && name && !strcmp(name, "XSetWMProperties"))
    {
        realSetWMProperties = p;
        return (void *)setWMProperties;
    }
    if (p && name && (!strcmp(name, "glXGetProcAddressARB") || !strcmp(name, "glXGetProcAddress")))
    {
        realGetProcAddress = p;
        return (void *)gameGetProcAddress;
    }
    return glEntry(name, p);
}

static const struct { const char *name; void *fn; } wrappers[] = {
    {"dlsym", gameDlsym},
    {"popen", gamePopen}, {"system", gameSystem}, {"open", gameOpen}, {"open64", gameOpen}, {"__open_2", gameOpen2},
    {"fopen", gameFopen}, {"fopen64", gameFopen}, {"opendir", gameOpendir}, {"access", gameAccess},
    {"chdir", gameChdir}, {"mkdir", gameMkdir}, {"rmdir", gameRmdir}, {"unlink", gameUnlink},
    {"rename", gameRename}, {"readlink", gameReadlink}, {"symlink", gameSymlink}, {"__realpath_chk", gameRealpath},
    {"__xstat", gameXstat}, {"__xstat64", gameXstat}, {"__lxstat", gameLxstat}, {"__lxstat64", gameLxstat},
    {FMOD_LOAD_BANK_FILE, gameLoadBankFile},
};

// Hooks reach the game through a page mapped near it (rel32 jumps).
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
static uint8_t *near;
static size_t nearUsed;
static void *nearAlloc(size_t n)
{
    if (!near)
    {
        near = mmap((void *)0x10000000, 0x10000, PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (near == MAP_FAILED) { fprintf(stderr, "halo_rt: no near page\n"); near = NULL; return NULL; }
    }
    void *p = near + nearUsed;
    nearUsed += (n + 15) & ~15u;
    return p;
}
static void absJump(uint8_t *at, void *to)
{
    at[0] = 0xff; at[1] = 0x25; memset(at + 2, 0, 4);
    memcpy(at + 6, &to, 8);
}
static void relJump(uintptr_t at, void *to)
{
    uint8_t *p = (uint8_t *)at;
    int32_t rel = (int32_t)((intptr_t)to - (intptr_t)(at + 5));
    p[0] = 0xe9; memcpy(p + 1, &rel, 4);
}
// Detour through a near thunk; the original's first `len` bytes (whole
// instructions, none RIP-relative) moved into a trampoline.
static void *hookNear(uintptr_t at, size_t len, void *to)
{
    uint8_t *tramp = nearAlloc(len + 14), *thunk = nearAlloc(14);
    memcpy(tramp, (void *)at, len);
    absJump(tramp + len, (void *)(at + len));
    absJump(thunk, to);
    relJump(at, thunk);
    return tramp;
}

// Keyboard and mouse input.
//
// The RIO board only carries the cabinet panel -- the coin door, start
// buttons and guns come from other boards -- so rather than emulate any of
// them the loader writes the game's own switch records.
//
// Timing is the whole trick.  Once a frame the input update at 0x54cdc0
// (called from the main loop at 0x413eb3) clears every switch record, then
// rebuilds it from the device layer and propagates aliases.  Anything
// written before that is wiped; anything written after it survives for the
// rest of the frame, which is when every consumer runs.  So the loader hooks
// that function and fills the records in on the way out.
//
// Record layout is the one the RIO's own switch setter uses at 0x61a810:
// +0x04 presses seen this frame, +0x08 releases, +0x0c how long the switch
// has been held (0 while it is up).  The other fields, +0x20 among them,
// hold pointers the game dereferences, so only these three are touched.
//
// A raw cabinet input drives several logical switches at once -- the game
// registers the fan-out through io_sw_alias (0x4acdb0) -- and the menus read
// the action switch at the end of each chain (0x2a1 select, 0x2a2 service,
// 0x29f/0x2a0 volume) rather than the per-cabinet one, so each key raises
// the whole chain.
//
// Switch ids come from the dump's diagnostics switch-test table (id/name
// pairs at 0x16f7c20).  Cabinets are numbered 0..3 and so are the gun
// stations; a standard (2 player) cabinet wires stations 0 and 3, the
// 4 player cabinets wire 0..3 in order.
#define SW_GUN_TRIGGER(station, n) (0x208 + (station) * 0xc + (n))
#define SW_START(station)          (0x24e + (station))
#define SW_VOL_UP                  0x254
#define SW_VOL_DOWN                0x255
#define SW_DIAG                    0x256
#define SW_COIN(cab, n)            (0x257 + (cab) * 8 + (n))
#define SW_CAB_DIAG(cab)           (0x287 + (cab) * 4)
#define SW_CAB_SERVICE(cab)        (0x288 + (cab) * 4)
#define SW_CAB_VOL_UP(cab)         (0x289 + (cab) * 4)
#define SW_CAB_VOL_DOWN(cab)       (0x28a + (cab) * 4)
// The menus read the action switches at the end of each alias chain --
// 0x2a1 select, 0x2a2 service, 0x29f/0x2a0 volume -- but the loader never
// drives them directly: the game raises them itself from the raw inputs.

// The IR gun manager reports no guns without the real hardware, and the
// game refuses to start with a gun missing: the attract loop checks each
// station through 0x417880, which maps the station to an IR gun index (on a
// standard cabinet station 3 is gun 1) and asks the manager at 0xdd4b10.
// Answer for as many guns as the cabinet seats (HALO_GUNS to change).
static int gunConnected(int gun)
{
    long guns = envNum("HALO_GUNS", cabinetPlayers());
    return gun >= 0 && gun < (int)guns;
}

// The device layer.  0x61e890 hands out the raw input record for a hardware
// id (0x239..0x298 for the cabinet), and the alias propagation at 0x4ad764
// adds that record's counters into every logical switch the id drives:
//     dst[0x0c] = max(dst[0x0c], src[0x0c])   held
//     dst[0x04] += src[0x04]                  presses
//     dst[0x08] += src[0x08]                  releases
// Driving the raw record is therefore how real hardware feeds the game, and
// the fan-out to 0x2a1/0x287/0x256 and friends comes for free.  It is also
// safe to shadow: the propagation only ever reads those five integers from
// it, unlike a logical record, whose +0x20 pointer the game dereferences.
static uint32_t *(*devGetOrig)(int id);
static struct { int raw; uint32_t rec[20]; } devs[64];
static int devCount;

static uint32_t *devGet(int id)
{
    for (int i = 0; i < devCount; i++)
        if (devs[i].raw == id)
            return devs[i].rec;
    return devGetOrig(id);
}

// Which raw ids drive a logical switch, read out of the game's own alias
// table (count at 0x1771850, {raw, logical} pairs from 0x1766f40 + 0x1122*8)
// so the mapping is whatever this cabinet actually registered.
// Ids 0x24e..0x2ff are logical switches, and some of them appear in the
// table as sources: a cabinet 0 coin input doubles as cabinet 1's diag or
// service button, for instance.  Driving one of those presses the coin as
// well, so prefer the dedicated raw inputs (0x239..0x24d for the panel,
// 0x31b and up for the guns) and only fall back to a logical source if a
// switch has no raw one at all.
static int rawInput(int id) { return id < 0x24e || id > 0x2ff; }

static int aliasSources(int logical, int *out, int max)
{
    const int *table = (const int *)(0x1766f40 + 0x1122 * 8);
    int count = *(const int *)0x1771850, n = 0;
    for (int pass = 0; pass < 2 && n == 0; pass++)
        for (int i = 0; i < count && n < max; i++)
            if (table[i * 2 + 1] == logical && rawInput(table[i * 2]) == !pass)
                out[n++] = table[i * 2];
    return n;
}

// One switch per key.  Each names the raw input to drive; the game then
// propagates it to the rest of its alias chain by itself, so the action
// switches (0x2a1 select, 0x2a2 service, 0x29f/0x2a0 volume) must NOT be
// listed here -- they have many raw sources, and binding one would press
// diag and both start buttons at once.
// Operator entries name cabinet 0's switch, moved to the operator's cabinet.
static const struct { unsigned long keysym; int sw, operator; } keymap[] = {
    {0xffbf, SW_CAB_DIAG(0), 1},                                         // F2: test
    {0xffbe, SW_CAB_SERVICE(0), 1}, {0x70, SW_CAB_SERVICE(0), 1},        // F1, p: service
    {0xff52, SW_CAB_VOL_UP(0), 1}, {0xff54, SW_CAB_VOL_DOWN(0), 1},      // up/down: volume
    {0x35, SW_COIN(0, 0)}, {0x75, SW_COIN(0, 0)},                        // 5, u: coin 1
    {0x36, SW_COIN(0, 1)},                                               // 6:    coin 2
    // A 4 player cabinet is two cabinets side by side, the right one (P3/P4)
    // with coin slots of its own: its credits start players 3 and 4.
    {0x37, SW_COIN(1, 0)}, {0x38, SW_COIN(1, 1)},                        // 7, 8: coins 3, 4
    // Start buttons: the station depends on the cabinet, so these are
    // placeholders for players 1..4, resolved by startSwitch().
    {0x31, -1}, {0x69, -1}, {0x32, -2}, {0x6f, -2}, {0x33, -3}, {0x34, -4},
};

// The gun's three buttons, on the pointer.  Station 0 is player 1; the
// trigger also raises 0x2a5, the action the alias table pairs with it.
static const struct { unsigned int mask; int sw; } pointerMap[] = {
    {1u << 8, SW_GUN_TRIGGER(0, 0)},                     // left:   trigger
    {1u << 10, SW_GUN_TRIGGER(0, 1)},                    // right:  reload
    {1u << 9, SW_GUN_TRIGGER(0, 2)},                     // middle: action
};

static struct { int id; int held, wasHeld; uint32_t heldFor; } switches[64];
static int switchCount;
// A bind drives a slot from a key (BIND_KEY, X keycode) or from an evdev
// source (BIND_EVDEV, index into evSources).
enum { BIND_KEY, BIND_EVDEV };
static struct { int kind, code, slot; } binds[128];
static int bindCount;
static int pointerSlots[4];

// Which station each player (0-based) plays at, -1 if the cabinet has no
// such player.
static int playerStation(int player)
{
    static const int stations2p[] = {0, 3}, stations4p[] = {0, 1, 2, 3};
    int twoPlayer = cabinetPlayers() == 2;
    if (player < 0 || player >= (twoPlayer ? 2 : 4))
        return -1;
    return twoPlayer ? stations2p[player] : stations4p[player];
}

static int startSwitch(int player)
{
    int station = playerStation(player);
    return station < 0 ? 0 : SW_START(station);
}

// A slot per raw input id, with a shadow device record to go with it.
static int slotFor(int raw)
{
    int slot;
    for (slot = 0; slot < switchCount; slot++)
        if (switches[slot].id == raw)
            return slot;
    if (switchCount == (int)(sizeof(switches) / sizeof(switches[0])) ||
        devCount == (int)(sizeof(devs) / sizeof(devs[0])))
        return -1;
    switches[switchCount].id = raw;
    devs[devCount++].raw = raw;
    return switchCount++;
}

// Bind a key or evdev source to every raw id that drives this logical switch.
static int bindLogical(int kind, int code, int logical)
{
    int raws[8], n = aliasSources(logical, raws, 8), bound = 0;
    for (int i = 0; i < n; i++)
    {
        int slot = slotFor(raws[i]);
        if (slot < 0 || bindCount == (int)(sizeof(binds) / sizeof(binds[0])))
            continue;
        binds[bindCount].kind = kind;
        binds[bindCount].code = code;
        binds[bindCount].slot = slot;
        bindCount++;
        bound++;
    }
    return bound;
}

static void *xdpy;
static unsigned long xroot;
static int (*queryKeymap)(void *, char[32]);
static unsigned char (*keysymToKeycode)(void *, unsigned long);
static int (*queryPointer)(void *, unsigned long, unsigned long *, unsigned long *,
                           int *, int *, int *, int *, unsigned int *);
static unsigned long (*defaultRootWindow)(void *);
static int (*getGeometry)(void *, unsigned long, unsigned long *, int *, int *,
                          unsigned int *, unsigned int *, unsigned int *, unsigned int *);

static void bindKeys(void)
{
    int unmapped = 0;
    for (size_t i = 0; i < sizeof(keymap) / sizeof(keymap[0]); i++)
    {
        int sw = keymap[i].sw;
        unsigned char code = keysymToKeycode(xdpy, keymap[i].keysym);
        if (sw < 0)
            sw = startSwitch(-sw - 1);
        else if (keymap[i].operator)
            sw += operatorCabinet() * 4;
        if (sw <= 0 || !code)
            continue;
        if (!bindLogical(BIND_KEY, code, sw))
            unmapped++;
    }
    for (size_t i = 0; i < sizeof(pointerMap) / sizeof(pointerMap[0]); i++)
        pointerSlots[i] = -1;
    for (size_t i = 0; i < sizeof(pointerMap) / sizeof(pointerMap[0]); i++)
    {
        int raws[8], n = aliasSources(pointerMap[i].sw, raws, 8);
        if (n > 0)
            pointerSlots[i] = slotFor(raws[0]);
    }
    fprintf(stderr, "halo_rt: %d bindings over %d raw inputs (%d switches had no raw id)\n",
            bindCount, switchCount, unmapped);
    if (getenv("HALO_INPUT_DEBUG"))
    {
        const int *table = (const int *)(0x1766f40 + 0x1122 * 8);
        int count = *(const int *)0x1771850;
        fprintf(stderr, "halo_rt: alias table has %d entries\n", count);
        for (int i = 0; i < count; i++)
            fprintf(stderr, "halo_rt:   alias %#x -> %#x\n", table[i * 2], table[i * 2 + 1]);
        for (int i = 0; i < bindCount; i++)
            if (binds[i].kind == BIND_KEY)
                fprintf(stderr, "halo_rt:   key code %d -> raw %#x\n",
                        binds[i].code, switches[binds[i].slot].id);
    }
}

static int openX(void)
{
    void *x11 = dlopen("libX11.so.6", RTLD_NOW);
    void *(*openDisplay)(const char *);
    if (!x11)
    {
        fprintf(stderr, "halo_rt: dlopen libX11.so.6 failed: %s\n", dlerror());
        return 0;
    }
    openDisplay = dlsym(x11, "XOpenDisplay");
    queryKeymap = dlsym(x11, "XQueryKeymap");
    keysymToKeycode = dlsym(x11, "XKeysymToKeycode");
    queryPointer = dlsym(x11, "XQueryPointer");
    defaultRootWindow = dlsym(x11, "XDefaultRootWindow");
    getGeometry = dlsym(x11, "XGetGeometry");
    if (!openDisplay || !queryKeymap || !keysymToKeycode)
    {
        fprintf(stderr, "halo_rt: libX11 is missing the entry points needed\n");
        return 0;
    }
    if (!(xdpy = openDisplay(NULL)))
    {
        static int said;
        if (!said++)
            fprintf(stderr, "halo_rt: XOpenDisplay failed (DISPLAY=%s)\n",
                    getenv("DISPLAY") ? getenv("DISPLAY") : "unset");
        return 0;
    }
    if (defaultRootWindow)
        xroot = defaultRootWindow(xdpy);
    bindKeys();
    return 1;
}


// Evdev input ([Input] INPUT_MODE 2), configured as for the loader's other
// games: each [EVDEV] entry names an arcade input and the device input that
// drives it, in the loader's technical form:
//   /dev/input/event5:KEY:272      a key or button
//   /dev/input/event5:ABS:0        an axis (ABS_NEG: reversed)
//   /dev/input/event5:ABS:16:MIN   an axis pushed to one end, as a button
//   /dev/input/event7:REL:0        a mouse axis: its moves make a position
//                                  on the screen, a count per pixel
// The arcade inputs Halo has, for player n (the n-th station of the cabinet):
//   PLAYER_n_BUTTON_1/2/3   gun trigger, reload, action
//   PLAYER_n_BUTTON_START, PLAYER_n_COIN, PLAYER_n_BUTTON_SERVICE, TEST_BUTTON
//   ANALOGUE_2n-1/2n        gun n aim, X and Y
// The keyboard keeps working alongside it, as for the other games; the
// pointer stops driving a gun once evdev aims that gun.
#include <linux/input.h>

#define EV_BITS (8 * sizeof(unsigned long))
static struct
{
    char path[128];
    int fd;
    struct timespec retry;
    unsigned long keys[KEY_CNT / EV_BITS + 1];
    struct input_absinfo abs[ABS_CNT];
    float rel[REL_Y + 1]; // mouse position, 0..1
} evDevices[8];
static int evDeviceCount;
static struct { int device, type, code, reversed, end; } evSources[64];
static int evSourceCount;
static int evAim[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};   // gun -> X, Y source

static int evDevice(const char *path)
{
    for (int i = 0; i < evDeviceCount; i++)
        if (!strcmp(evDevices[i].path, path))
            return i;
    if (evDeviceCount == (int)(sizeof(evDevices) / sizeof(evDevices[0])))
        return -1;
    snprintf(evDevices[evDeviceCount].path, sizeof(evDevices[0].path), "%s", path);
    evDevices[evDeviceCount].fd = -1;
    evDevices[evDeviceCount].rel[REL_X] = evDevices[evDeviceCount].rel[REL_Y] = 0.5f;
    return evDeviceCount++;
}

static int evSource(const char *spec)
{
    char path[128], type[16], end[8] = "";
    int code, n = sscanf(spec, "%127[^:]:%15[^:]:%d:%7s", path, type, &code, end);
    int kind = n >= 3 && !strcmp(type, "KEY") ? EV_KEY
             : n >= 3 && (!strcmp(type, "ABS") || !strcmp(type, "ABS_NEG")) ? EV_ABS
             : n >= 3 && !strcmp(type, "REL") ? EV_REL : -1;
    int device;

    if (kind < 0 || code < 0 || code >= (kind == EV_KEY ? KEY_CNT : kind == EV_REL ? REL_Y + 1 : ABS_CNT) ||
        evSourceCount == (int)(sizeof(evSources) / sizeof(evSources[0])) || (device = evDevice(path)) < 0)
        return -1;
    evSources[evSourceCount].device = device;
    evSources[evSourceCount].type = kind;
    evSources[evSourceCount].code = code;
    evSources[evSourceCount].reversed = !strcmp(type, "ABS_NEG");
    evSources[evSourceCount].end = !strcmp(end, "MIN") ? -1 : !strcmp(end, "MAX") ? 1 : 0;
    return evSourceCount++;
}

static void evOpen(int i, const struct timespec *now)
{
    int fd;
    if (now->tv_sec < evDevices[i].retry.tv_sec)
        return;
    evDevices[i].retry.tv_sec = now->tv_sec + 2;
    if ((fd = open(evDevices[i].path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
        return;
    memset(evDevices[i].keys, 0, sizeof(evDevices[i].keys));
    ioctl(fd, EVIOCGKEY(sizeof(evDevices[i].keys)), evDevices[i].keys);
    for (int code = 0; code < ABS_CNT; code++)
        if (ioctl(fd, EVIOCGABS(code), &evDevices[i].abs[code]) < 0)
            memset(&evDevices[i].abs[code], 0, sizeof(evDevices[i].abs[code]));
    evDevices[i].fd = fd;
    fprintf(stderr, "halo_rt: evdev %s open\n", evDevices[i].path);
}

static void evPoll(const struct timespec *now)
{
    for (int i = 0; i < evDeviceCount; i++)
    {
        struct input_event ev[64];
        ssize_t got;
        if (evDevices[i].fd < 0)
            evOpen(i, now);
        if (evDevices[i].fd < 0)
            continue;
        while ((got = read(evDevices[i].fd, ev, sizeof(ev))) > 0)
            for (size_t e = 0; e < (size_t)got / sizeof(ev[0]); e++)
            {
                if (ev[e].type == EV_KEY && ev[e].code < KEY_CNT)
                {
                    unsigned long bit = 1ul << (ev[e].code % EV_BITS);
                    if (ev[e].value)
                        evDevices[i].keys[ev[e].code / EV_BITS] |= bit;
                    else
                        evDevices[i].keys[ev[e].code / EV_BITS] &= ~bit;
                }
                else if (ev[e].type == EV_ABS && ev[e].code < ABS_CNT)
                    evDevices[i].abs[ev[e].code].value = ev[e].value;
                else if (ev[e].type == EV_REL && ev[e].code <= REL_Y)
                {
                    int span = ev[e].code == REL_X ? cfg.width : cfg.height;
                    float *p = &evDevices[i].rel[ev[e].code];
                    if (span <= 0)
                        span = ev[e].code == REL_X ? 1920 : 1080;
                    *p += (float)ev[e].value / (float)span;
                    *p = *p < 0.f ? 0.f : *p > 1.f ? 1.f : *p;
                }
            }
        if (got < 0 && errno != EAGAIN && errno != EINTR)
        {
            // Unplugged: forget its state, and look for it again later.
            fprintf(stderr, "halo_rt: evdev %s lost\n", evDevices[i].path);
            close(evDevices[i].fd);
            evDevices[i].fd = -1;
            memset(evDevices[i].keys, 0, sizeof(evDevices[i].keys));
        }
    }
}

// An axis as 0..1 over its range (reversed for ABS_NEG), -1 if unknown.
static float evAxis(int s)
{
    const struct input_absinfo *a = &evDevices[evSources[s].device].abs[evSources[s].code];
    float t;
    if (evSources[s].type == EV_REL)
        return evDevices[evSources[s].device].fd < 0 ? -1.f : evDevices[evSources[s].device].rel[evSources[s].code];
    if (evDevices[evSources[s].device].fd < 0 || a->maximum <= a->minimum)
        return -1.f;
    t = (float)(a->value - a->minimum) / (float)(a->maximum - a->minimum);
    return evSources[s].reversed ? 1.f - t : t;
}

static int evHeld(int s)
{
    const unsigned long *keys = evDevices[evSources[s].device].keys;
    float t;
    if (evSources[s].type == EV_KEY)
        return (keys[evSources[s].code / EV_BITS] >> (evSources[s].code % EV_BITS)) & 1;
    if ((t = evAxis(s)) < 0.f)
        return 0;
    return evSources[s].end < 0 ? t < 0.25f : evSources[s].end > 0 ? t > 0.75f : t > 0.5f;
}

// The logical switch an [EVDEV] arcade input drives, 0 if none; aim axes
// are recorded in evAim instead.
static int evArcadeInput(const char *name, int source)
{
    int player, n, station;
    char what[32];

    if (!strcmp(name, "TEST_BUTTON"))
        return SW_CAB_DIAG(operatorCabinet());
    if (sscanf(name, "ANALOGUE_%d", &n) == 1)
    {
        if (n >= 1 && n <= 8)
            evAim[(n - 1) / 2][(n - 1) % 2] = source;
        return 0;
    }
    if (sscanf(name, "PLAYER_%d_%31s", &player, what) != 2 || (station = playerStation(player - 1)) < 0)
        return 0;
    if (sscanf(what, "BUTTON_%d", &n) == 1 && n >= 1 && n <= 3)
        return SW_GUN_TRIGGER(station, n - 1);
    if (!strcmp(what, "BUTTON_START"))
        return SW_START(station);
    // Players 1/2 coin the left cabinet, 3/4 the right one (see keymap).
    if (!strcmp(what, "COIN"))
        return SW_COIN((player - 1) / 2, (player - 1) % 2);
    if (!strcmp(what, "BUTTON_SERVICE"))
        return SW_CAB_SERVICE(operatorCabinet());
    return 0;
}

static void bindEvdev(void)
{
    IniSection *section = ini ? iniGetSection(ini, "EVDEV") : NULL;
    int bound = 0;
    if (!section)
    {
        fprintf(stderr, "halo_rt: evdev input asked, but there is no [EVDEV] section\n");
        return;
    }
    for (int i = 0; i < section->numPairs; i++)
    {
        const char *name = section->pairs[i].key, *spec = section->pairs[i].value;
        int source, sw;
        if (!spec || !*spec)
            continue;
        if ((source = evSource(spec)) < 0)
        {
            fprintf(stderr, "halo_rt: evdev %s = %s: not a device input (/dev/input/eventN:KEY|ABS|REL:code)\n", name, spec);
            continue;
        }
        sw = evArcadeInput(name, source);
        if (sw > 0 && bindLogical(BIND_EVDEV, source, sw))
            bound++;
        else if (sw > 0)
            fprintf(stderr, "halo_rt: evdev %s: switch %#x has no raw input\n", name, sw);
    }
    fprintf(stderr, "halo_rt: evdev: %d inputs over %d devices\n", bound, evDeviceCount);
    for (int gun = 0; gun < 4; gun++)
        if (evAim[gun][0] >= 0 && evAim[gun][1] >= 0)
            fprintf(stderr, "halo_rt: evdev: gun %d aimed by %s\n", gun,
                    evDevices[evSources[evAim[gun][0]].device].path);
}

// Gun aim.  Once per frame and gun the game asks the irt layer for the gun
// position through 0xdd6c60 (gun, &x, &y), expecting normalized screen
// coordinates: 0..1 between the axis bounds it reads from its own objects,
// (-1, -1) for offscreen.  The pipeline that feeds it (cmgr serial link to
// the gun boards, LED pose solve at 0xdd8350, live array at 0x1e94680) never
// runs without the hardware, so the loader answers from evdev or the X
// pointer instead: the whole calibration transform downstream is bypassed.
// The game's Y axis points up; HALO_INVERT_X/Y override the directions.
static float gunAimX = -1.f, gunAimY = -1.f;   // pointer, normalized, -1 = unknown/offscreen

static int evAims(int gun)
{
    return gun >= 0 && gun < 4 && evAim[gun][0] >= 0 && evAim[gun][1] >= 0;
}

// A point of the game window (0..1 from its top left) to the game's
// coordinates, through the picture's place in the window when it is scaled.
// 0 when the point is off the picture.
static int windowToGame(float *x, float *y)
{
    static int invertX = -1, invertY = -1;
    if (invertX < 0)
    {
        invertX = envNum("HALO_INVERT_X", 0) != 0;
        invertY = envNum("HALO_INVERT_Y", 1) != 0;
    }
    if (!frameScaleWindowToGame(x, y))
        return 0;
    if (invertX) *x = 1.f - *x;
    if (invertY) *y = 1.f - *y;
    return 1;
}

static void gunGetPos(int gun, float *x, float *y)
{
    if (evAims(gun))
    {
        // A light gun pointed off the screen reports the edge of its range.
        float ex = evAxis(evAim[gun][0]), ey = evAxis(evAim[gun][1]);
        if (ex > 0.f && ex < 1.f && ey > 0.f && ey < 1.f && windowToGame(&ex, &ey))
        {
            *x = ex;
            *y = ey;
            return;
        }
    }
    else if (gun == (int)envNum("HALO_MOUSE_GUN", 0) && gunAimX >= 0.f)
    {
        *x = gunAimX;
        *y = gunAimY;
        return;
    }
    *x = -1.f;
    *y = -1.f;
}

// The game's per-frame input update.  Everything below runs after it has
// rebuilt the switch records, so the presses survive to be read.
static int (*inputOrig)(int);
static int inputUpdate(int arg)
{
    static int debug = -1;
    static struct timespec last;
    struct timespec now;
    char km[32];
    int dt, pointerGun;

    if (debug < 0)
    {
        debug = getenv("HALO_INPUT_DEBUG") != NULL;
        fprintf(stderr, "halo_rt: input update hooked, loader live\n");
        if (cfg.inputMode == 2)
            bindEvdev();
    }
    if (!xdpy)
        openX();
    clock_gettime(CLOCK_MONOTONIC, &now);
    dt = last.tv_sec ? (int)((now.tv_sec - last.tv_sec) * 1000 + (now.tv_nsec - last.tv_nsec) / 1000000) : 0;
    last = now;

    for (int i = 0; i < switchCount; i++)
        switches[i].held = 0;
    if (evSourceCount)
        evPoll(&now);
    if (xdpy)
        queryKeymap(xdpy, km);
    for (int i = 0; i < bindCount; i++)
        if (binds[i].kind == BIND_EVDEV ? evHeld(binds[i].code)
                                        : xdpy && (km[binds[i].code / 8] & (1 << (binds[i].code % 8))))
            switches[binds[i].slot].held = 1;
    // The pointer is player 1's gun (station 0 buttons) unless evdev aims it.
    pointerGun = (int)envNum("HALO_MOUSE_GUN", 0);
    if (xdpy && queryPointer && xroot && !evAims(pointerGun))
    {
        unsigned long r, c;
        int rx, ry, wx, wy;
        unsigned int mask = 0;
        // Relative to the game's window once it has one, else to the screen.
        unsigned long window = gameWindow ? gameWindow : xroot;
        queryPointer(xdpy, window, &r, &c, &rx, &ry, &wx, &wy, &mask);
        for (size_t i = 0; i < sizeof(pointerMap) / sizeof(pointerMap[0]); i++)
            if ((mask & pointerMap[i].mask) && pointerSlots[i] >= 0)
                switches[pointerSlots[i]].held = 1;
        // Gun aim: normalize the pointer into the game's 0..1 coordinates.
        if (getGeometry)
        {
            unsigned long rr;
            int gx, gy;
            unsigned int ww = 0, wh = 0, bw, bd;
            float x, y;
            if (!getGeometry(xdpy, window, &rr, &gx, &gy, &ww, &wh, &bw, &bd))
                ww = wh = 0;
            x = ww ? (float)wx / (float)ww : -1.f;
            y = wh ? (float)wy / (float)wh : -1.f;
            if (ww && wh && windowToGame(&x, &y))
            {
                gunAimX = x;
                gunAimY = y;
            }
            else
                gunAimX = gunAimY = -1.f;
        }
        if (debug)
        {
            static unsigned int wasMask;
            if (mask != wasMask)
                fprintf(stderr, "halo_rt: pointer buttons %#x at %d,%d\n", mask, rx, ry);
            wasMask = mask;
        }
    }
    for (int i = 0; i < switchCount; i++)
    {
        uint32_t *rec = devGet(switches[i].id);
        int held = switches[i].held;
        if (!rec)
            continue;
        // A press and a release each last one frame, which is what the RIO's
        // own switch setter produces.
        rec[1] = held && !switches[i].wasHeld;
        rec[2] = !held && switches[i].wasHeld;
        switches[i].heldFor = held ? switches[i].heldFor + (uint32_t)dt + 1 : 0;
        rec[3] = switches[i].heldFor;
        if (debug && (rec[1] || rec[2]))
            fprintf(stderr, "halo_rt: raw %#x %s\n", switches[i].id,
                    rec[1] ? "pressed" : "released");
        switches[i].wasHeld = held;
    }
    if (debug && evSourceCount)
    {
        static int frames;
        if (frames++ % 120 == 0)
            for (int gun = 0; gun < 4; gun++)
                if (evAims(gun))
                    fprintf(stderr, "halo_rt: evdev gun %d at %.3f,%.3f\n", gun,
                            evAxis(evAim[gun][0]), evAxis(evAim[gun][1]));
    }
    // Only now let the game run its update: it clears the logical records,
    // then propagates these raw ones into them.
    return inputOrig(arg);
}

// Esc or Alt+F4 quits, as in the other games: a thread watches the
// keyboard with its own X connection.  The X server reports keys pressed in
// any window, so only while the focused window, or one of its parents,
// belongs to the game (_NET_WM_PID, which SDL sets).
static struct
{
    void *(*openDisplay)(const char *);
    int (*queryKeymap)(void *, char[32]);
    unsigned char (*keysymToKeycode)(void *, unsigned long);
    int (*getInputFocus)(void *, unsigned long *, int *);
    int (*queryTree)(void *, unsigned long, unsigned long *, unsigned long *, unsigned long **, unsigned int *);
    unsigned long (*internAtom)(void *, const char *, int);
    int (*getWindowProperty)(void *, unsigned long, unsigned long, long, long, int, unsigned long,
                             unsigned long *, int *, unsigned long *, unsigned long *, unsigned char **);
    int (*freeData)(void *);
} qx;

static int gameFocused(void *display, unsigned long pidAtom)
{
    unsigned long window, root, parent, *children;
    unsigned int nchildren;
    int revert;

    qx.getInputFocus(display, &window, &revert);
    // None or PointerRoot (gamescope): no other X window has the keyboard.
    if (window <= 1)
        return 1;
    for (int depth = 0; window > 1 && depth < 16; depth++)
    {
        unsigned long type, nitems, after;
        unsigned char *data = NULL;
        int format;

        if (qx.getWindowProperty(display, window, pidAtom, 0, 1, 0, 6 /* XA_CARDINAL */, &type, &format,
                                 &nitems, &after, &data) == 0 && data)
        {
            // Xlib returns 32-bit properties as longs.
            int ours = nitems == 1 && (pid_t)*(unsigned long *)data == getpid();
            qx.freeData(data);
            return ours;
        }
        if (!qx.queryTree(display, window, &root, &parent, &children, &nchildren))
            break;
        if (children)
            qx.freeData(children);
        if (parent == root)
            break;
        window = parent;
    }
    return 0;
}

static int keyDown(const char keys[32], unsigned char code)
{
    return code && (keys[code / 8] & (1 << (code % 8)));
}

static void *quitWatch(void *arg)
{
    void *x11 = dlopen("libX11.so.6", RTLD_NOW), *display;
    (void)arg;
    if (x11)
    {
        *(void **)&qx.openDisplay = dlsym(x11, "XOpenDisplay");
        *(void **)&qx.queryKeymap = dlsym(x11, "XQueryKeymap");
        *(void **)&qx.keysymToKeycode = dlsym(x11, "XKeysymToKeycode");
        *(void **)&qx.getInputFocus = dlsym(x11, "XGetInputFocus");
        *(void **)&qx.queryTree = dlsym(x11, "XQueryTree");
        *(void **)&qx.internAtom = dlsym(x11, "XInternAtom");
        *(void **)&qx.getWindowProperty = dlsym(x11, "XGetWindowProperty");
        *(void **)&qx.freeData = dlsym(x11, "XFree");
    }
    if (!qx.openDisplay || !qx.queryKeymap || !qx.keysymToKeycode || !qx.getInputFocus || !qx.queryTree ||
        !qx.internAtom || !qx.getWindowProperty || !qx.freeData || !(display = qx.openDisplay(NULL)))
    {
        fprintf(stderr, "halo_rt: no X display, Esc/Alt+F4 quit disabled\n");
        return NULL;
    }
    unsigned char escape = qx.keysymToKeycode(display, 0xff1b), f4 = qx.keysymToKeycode(display, 0xffc1),
                  altL = qx.keysymToKeycode(display, 0xffe9), altR = qx.keysymToKeycode(display, 0xffea);
    unsigned long pidAtom = qx.internAtom(display, "_NET_WM_PID", 0);

    for (;;)
    {
        char keys[32];

        usleep(50000);
        qx.queryKeymap(display, keys);
        if (!keyDown(keys, escape) && !(keyDown(keys, f4) && (keyDown(keys, altL) || keyDown(keys, altR))))
            continue;
        if (!gameFocused(display, pidAtom))
            continue;
        fprintf(stderr, "halo_rt: quit from the keyboard\n");
        _exit(0);
    }
    return NULL;
}

__attribute__((constructor)) static void init(void)
{
    int missing = 0;
    // The cabinet's layout: the game is in g7/halo two levels under /pm.
    // Otherwise the game's folder stands for all of /pm, with the recorded
    // answers in dump/ beside the game (or left in pm/g7/dump).
    struct stat here, there;
    if (stat("game", &here) == 0 && stat("../../g7/halo/game", &there) == 0 && here.st_ino == there.st_ino &&
        here.st_dev == there.st_dev)
    {
        if (!realpath("../..", root))
            strcpy(root, "../..");
        snprintf(halo, sizeof(halo), "%s/g7/halo", root);
    }
    else
    {
        if (!realpath(".", root))
            strcpy(root, ".");
        strcpy(halo, root);
        dumpDir = access("dump", F_OK) == 0 || access("pm/g7/dump", F_OK) != 0 ? "dump" : "pm/g7/dump";
    }
    *(void **)&xstat = dlvsym(RTLD_DEFAULT, "__xstat", "GLIBC_2.2.5");
    *(void **)&lxstat = dlvsym(RTLD_DEFAULT, "__lxstat", "GLIBC_2.2.5");
    snprintf(dongleMemPath, sizeof(dongleMemPath), "%s/dongle_memory.dmp", halo);
    dongleMemLoad();
    fprintf(stderr, "halo_rt: /pm is %s, the game's folder %s, answers in %s\n", root, halo, dumpDir);
    loadConfig();
    for (size_t i = 0; i < sizeof(imports) / sizeof(imports[0]); i++)
    {
        void *p = imports[i].version[0] ? dlvsym(RTLD_DEFAULT, imports[i].name, imports[i].version) : NULL;
        for (size_t w = 0; w < sizeof(wrappers) / sizeof(wrappers[0]); w++)
            if (!strcmp(imports[i].name, wrappers[w].name))
                p = wrappers[w].fn;
        if (!p)
            p = dlsym(RTLD_DEFAULT, imports[i].name);
        if (p && !strcmp(imports[i].name, "glXGetProcAddressARB"))
        {
            realGetProcAddress = p;
            p = gameGetProcAddress;
        }
        else if (p && frameScaleWrapper(imports[i].name))
            p = frameScaleWrapper(imports[i].name);
        if (!p) { missing++; fprintf(stderr, "halo_rt: missing import %s@%s (slot %#lx)\n", imports[i].name, imports[i].version, imports[i].slot); continue; }
        *(void **)imports[i].slot = p;
    }
    fprintf(stderr, "halo_rt: imports rebuilt, %d missing\n", missing);
    frameScaleInit(realGetProcAddress, cfg.keepAspectRatio);
    detour(0xb2d630, haspLogin);
    detour(0xb2dd48, haspOk);
    detour(0xb2dfd1, haspEncrypt);
    detour(0xb2e1aa, haspDecrypt);
    detour(0xb2e391, haspFree);
    detour(0xb2f5ac, haspSessionInfo);
    detour(0xb2fd63, haspRead);
    detour(0xb2fe60, haspWrite);
    detour(0x6278d0, haspOk);  // RIO_Connected: 0
    // A 4 player cabinet has a RIO2 board per player pair, told apart by a
    // jumper, and the boot check at 0x4adac0 waits on "P1/P2 RIO2: 0 (ERROR -
    // Should be 1)" when they are missing.  It is the only caller of the
    // expected board count getter: answering 1 skips it, as on a 2 player
    // cabinet, while the RIO layer keeps its own (zero) board count.
    detour(0x6213f0, one);
    // Per-frame input update: "push %r12; push %rbp; push %rbx; mov %edi,%ebx"
    *(void **)&inputOrig = hookNear(0x54cdc0, 6, inputUpdate);
    // Raw input record accessor: "test %edi,%edi; js ..."
    *(void **)&devGetOrig = hookNear(0x61e890, 8, devGet);
    detour(0xdd4b10, gunConnected);  // IR gun manager: is gun N connected
    detour(0xdd6c60, gunGetPos);     // IR gun manager: gun N aim, 0..1 screen coords
    // Command-line parser: "push %rbp; push %rbx; sub $0x18,%rsp"
    *(void **)&parseArgsOrig = hookNear(0x435560, 6, parseArgs);

    pthread_t quitThread;
    if (pthread_create(&quitThread, NULL, quitWatch, NULL) == 0)
        pthread_detach(quitThread);
}
