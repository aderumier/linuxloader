// The g7 games' runtime on Windows (see ../rawthrills/g7/g7.h, whose
// descriptors it shares): the dump's import table, the dongle, the cabinet's
// /pm paths, and each game's switches from the keyboard and XInput pads.
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ll64.h"
#include "../rawthrills/g7/g7.h"

static const G7Game *game;
char g7Root[MAX_PATH], g7GameDir[MAX_PATH];
static char dumpDir[MAX_PATH], dongleMemPath[MAX_PATH], tmpDir[MAX_PATH];
struct G7Config g7Config = {0, 0, 1, 0, 1, 1, 0.02f, 0.0f};
void *(*g7GameDlsymHook)(const char *name);
extern void *(*lxDlsymHook)(const char *name);

#undef g7Log
#define g7Log(...) lxLog("g7: " __VA_ARGS__)

// ---------------------------------------------------------------------------
// The executable.

static int fileExists(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static const char *const exeNames[] = {"game2", "gamedump.exec"};

static int findIn(const char *dir, char *exe, size_t size)
{
    for (size_t i = 0; i < LX_COUNT(exeNames); i++)
    {
        snprintf(exe, size, "%s\\%s", dir, exeNames[i]);
        if (fileExists(exe))
            return 1;
    }
    return 0;
}

const char *lxG7FindExecutable(const char *gameDir, char *exe, size_t size)
{
    static const char *const parents[] = {"g7", "pm\\g7"};
    const char *named = lxIniValue("General", "EXECUTABLE");
    if (named)
    {
        snprintf(exe, size, "%s\\%s", gameDir, named);
        return fileExists(exe) ? exe : NULL;
    }
    if (findIn(gameDir, exe, size))
        return exe;
    for (size_t p = 0; p < LX_COUNT(parents); p++)
    {
        char pattern[MAX_PATH];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        snprintf(pattern, sizeof(pattern), "%s\\%s\\*", gameDir, parents[p]);
        if ((h = FindFirstFileA(pattern, &fd)) == INVALID_HANDLE_VALUE)
            continue;
        do
        {
            char dir[MAX_PATH];
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.')
                continue;
            snprintf(dir, sizeof(dir), "%s\\%s\\%s", gameDir, parents[p], fd.cFileName);
            if (findIn(dir, exe, size))
            {
                FindClose(h);
                return exe;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Paths: /pm is the game's pm folder, the game in /pm/g7/<game> (see
// g7_rt.c), /tmp the user's temporary folder; what else the cabinet has is
// looked for under <pm>/rootfs.

static void tmpInit(void)
{
    char t[MAX_PATH];
    GetTempPathA(sizeof(t), t);
    snprintf(tmpDir, sizeof(tmpDir), "%sll64", t);
    CreateDirectoryA(tmpDir, NULL);
}

static const char *procFile(const char *name, const char *content)
{
    static char path[MAX_PATH];
    FILE *f;
    snprintf(path, sizeof(path), "%s\\%s", tmpDir, name);
    if ((f = fopen(path, "wb")))
    {
        fputs(content, f);
        fclose(f);
    }
    return path;
}

const char *lxMapPath(const char *p, char *buf, size_t size)
{
    size_t n;
    if (!p)
        return NULL;
    if (p[0] != '/')
        return p;
    if (game && game->mapPath)
    {
        // Halo's own layout (Linux paths): not on Windows yet.
    }
    if (game && (n = strlen(game->cabinetDir)) && !strncmp(p, game->cabinetDir, n) && (p[n] == '/' || !p[n]))
    {
        snprintf(buf, size, "%s%s", g7GameDir, p + n);
        return buf;
    }
    if (!strncmp(p, "/pm", 3) && (p[3] == '/' || !p[3]))
    {
        snprintf(buf, size, "%s%s", g7Root, p + 3);
        return buf;
    }
    if (!strncmp(p, "/tmp", 4) && (p[4] == '/' || !p[4]))
    {
        snprintf(buf, size, "%s%s", tmpDir, p + 4);
        return buf;
    }
    if (!strcmp(p, "/dev/null"))
        return "NUL";
    if (!strncmp(p, "/sys/class/dmi/id/", 18))
    {
        // The BIOS's identity, as Windows has it.
        static const struct { const char *file, *value; } dmi[] = {
            {"bios_vendor", "BIOSVendor"}, {"bios_version", "BIOSVersion"}, {"bios_date", "BIOSReleaseDate"},
            {"board_vendor", "BaseBoardManufacturer"}, {"board_name", "BaseBoardProduct"},
            {"sys_vendor", "SystemManufacturer"}, {"product_name", "SystemProductName"}};
        for (size_t i = 0; i < LX_COUNT(dmi); i++)
            if (!strcmp(p + 18, dmi[i].file))
            {
                char v[256] = "Unknown", line[300];
                DWORD n = sizeof(v);
                RegGetValueA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", dmi[i].value, RRF_RT_REG_SZ, NULL,
                             v, &n);
                snprintf(line, sizeof(line), "%s\n", v);
                return procFile(dmi[i].file, line);
            }
    }
    if (!strcmp(p, "/proc/cpuinfo"))
    {
        char content[4096];
        SYSTEM_INFO si;
        size_t o = 0;
        GetSystemInfo(&si);
        for (DWORD i = 0; i < si.dwNumberOfProcessors && o + 256 < sizeof(content); i++)
            o += (size_t)snprintf(content + o, sizeof(content) - o,
                                  "processor\t: %lu\nvendor_id\t: GenuineIntel\nmodel name\t: Intel(R) Core(TM) i5 CPU\n"
                                  "cpu MHz\t\t: 3000.000\nflags\t\t: fpu sse sse2 ssse3 sse4_1 sse4_2\n\n", i);
        return procFile("cpuinfo", content);
    }
    if (!strcmp(p, "/proc/meminfo"))
    {
        char content[256];
        MEMORYSTATUSEX ms = {sizeof(ms)};
        GlobalMemoryStatusEx(&ms);
        snprintf(content, sizeof(content), "MemTotal:       %llu kB\nMemFree:        %llu kB\nMemAvailable:   %llu kB\n",
                 ms.ullTotalPhys / 1024, ms.ullAvailPhys / 1024, ms.ullAvailPhys / 1024);
        return procFile("meminfo", content);
    }
    snprintf(buf, size, "%s/rootfs%s", g7Root[0] ? g7Root : lxGameDir, p);
    return buf;
}

// ---------------------------------------------------------------------------
// Code patches (see g7_rt.c), the hooks System V functions.

void g7Detour(uintptr_t at, void *to)
{
    uint8_t *p = (uint8_t *)at;
    if (!at)
        return;
    p[0] = 0xff; p[1] = 0x25; memset(p + 2, 0, 4);
    memcpy(p + 6, &to, 8);
}

static uint8_t *nearPage;
static size_t nearUsed;
static void *nearAlloc(size_t n)
{
    if (!nearPage)
    {
        for (uintptr_t a = 0x10000000; a < 0x70000000 && !nearPage; a += 0x1000000)
            nearPage = VirtualAlloc((void *)a, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!nearPage)
        {
            g7Log("no page near the game for its hooks\n");
            return NULL;
        }
    }
    void *p = nearPage + nearUsed;
    nearUsed += (n + 15) & ~(size_t)15;
    return p;
}

void *g7HookNear(uintptr_t at, size_t len, void *to)
{
    uint8_t *tramp = nearAlloc(len + 14), *thunk = nearAlloc(14), *p = (uint8_t *)at;
    int32_t rel;
    memcpy(tramp, (void *)at, len);
    g7Detour((uintptr_t)(tramp + len), (void *)(at + len));
    g7Detour((uintptr_t)thunk, to);
    rel = (int32_t)((intptr_t)thunk - (intptr_t)(at + 5));
    p[0] = 0xe9;
    memcpy(p + 1, &rel, 4);
    return tramp;
}

// ---------------------------------------------------------------------------
// Settings.

long g7EnvNum(const char *name, long def)
{
    const char *v = getenv(name);
    return v && *v ? strtol(v, NULL, 0) : def;
}

long g7GameEnv(const char *name, long def)
{
    char full[128];
    snprintf(full, sizeof(full), "%s_%s", game->envPrefix, name);
    return g7EnvNum(full, def);
}

const char *g7IniValue(const char *section, const char *key) { return lxIniValue(section, key); }
void *g7Ini(void) { return lxIni(); }

static void loadConfig(void)
{
    const char *v;
    g7Config.width = lxIniInt("Display", "WIDTH", g7Config.width);
    g7Config.height = lxIniInt("Display", "HEIGHT", g7Config.height);
    g7Config.fullscreen = lxIniInt("Display", "FULLSCREEN", 0);
    g7Config.keepAspectRatio = lxIniInt("Display", "KEEP_ASPECT_RATIO", g7Config.keepAspectRatio);
    g7Config.borderEnabled = lxIniInt("Display", "BORDER_ENABLED", 0);
    if ((v = lxIniValue("Display", "WHITE_BORDER_PERCENTAGE")))
        g7Config.whiteBorder = (float)atof(v) / 100.f;
    if ((v = lxIniValue("Display", "BLACK_BORDER_PERCENTAGE")))
        g7Config.blackBorder = (float)atof(v) / 100.f;
}

// ---------------------------------------------------------------------------
// The dongle (see g7_rt.c).

static uint8_t dongleMem[64];

int g7CabinetType(void) { return dongleMem[41]; }

static void dongleMemLoad(void)
{
    uint32_t serial = (uint32_t)g7GameEnv("SERIAL_NO", 1234567);
    FILE *f;
    dongleMem[40] = (uint8_t)g7GameEnv("FACTORY_SETUP", game->factorySetupDone);
    dongleMem[41] = (uint8_t)g7GameEnv("CABINET_TYPE", game->defaultCabinet);
    dongleMem[42] = (uint8_t)g7GameEnv("REGION", 0);
    memcpy(dongleMem + 43, &serial, sizeof(serial));
    if ((f = fopen(dongleMemPath, "rb")))
    {
        uint8_t saved[sizeof(dongleMem)];
        if (fread(saved, 1, sizeof(saved), f) == sizeof(saved))
        {
            saved[41] = dongleMem[41];
            memcpy(dongleMem, saved, sizeof(saved));
        }
        fclose(f);
    }
}

SYSV void *lxStrdup(const char *s);
SYSV void lxFree(void *p);

static SYSV int haspOk(void) { return 0; }
static SYSV int haspLogin(uint32_t feature, const void *vc, uint32_t *handle)
{
    (void)vc;
    g7Log("hasp_login feature %#x\n", feature);
    *handle = 1;
    return 0;
}
static SYSV int haspSessionInfo(uint32_t h, const char *fmt, char **info)
{
    (void)h;
    g7Log("hasp_get_sessioninfo %.60s\n", fmt);
    *info = lxStrdup("<?xml version=\"1.0\" encoding=\"UTF-8\" ?><hasp_info><hasp id=\"1234567\"><haspid>1234567</haspid></hasp></hasp_info>");
    return 0;
}
static SYSV int haspFree(void *p) { lxFree(p); return 0; }
static SYSV int haspRead(uint32_t h, uint32_t file, uint32_t off, uint32_t len, void *buf)
{
    (void)h;
    memset(buf, 0, len);
    if (file == 0xfff4 && off < sizeof(dongleMem))
        memcpy(buf, dongleMem + off, len < sizeof(dongleMem) - off ? len : sizeof(dongleMem) - off);
    g7Log("hasp_read file %#x off %u len %u -> %#x\n", file, off, len, len ? *(const uint8_t *)buf : 0);
    return 0;
}
static SYSV int haspWrite(uint32_t h, uint32_t file, uint32_t off, uint32_t len, const void *buf)
{
    FILE *f;
    uint32_t n;
    (void)h;
    g7Log("hasp_write file %#x off %u len %u\n", file, off, len);
    if (file != 0xfff4 || off >= sizeof(dongleMem))
        return 0;
    n = len < sizeof(dongleMem) - off ? len : (uint32_t)sizeof(dongleMem) - off;
    if (!memcmp(dongleMem + off, buf, n))
        return 0;
    memcpy(dongleMem + off, buf, n);
    if ((f = fopen(dongleMemPath, "wb")))
    {
        fwrite(dongleMem, 1, sizeof(dongleMem), f);
        fclose(f);
    }
    return 0;
}

static int sha256(const void *data, size_t len, unsigned char md[32])
{
    BCRYPT_ALG_HANDLE alg;
    int ok = 0;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0))
        return 0;
    ok = !BCryptHash(alg, NULL, 0, (PUCHAR)data, (ULONG)len, md, 32);
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

// The recorded AES answers (see g7_rt.c): by the input's SHA-256, else the
// only one of the size.
static int answerBySize(uint32_t len, char *name, size_t size)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int found = 0;
    snprintf(pattern, sizeof(pattern), "%s\\*", dumpDir);
    if ((h = FindFirstFileA(pattern, &fd)) == INVALID_HANDLE_VALUE)
        return 0;
    do
    {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.nFileSizeHigh || fd.nFileSizeLow != len)
            continue;
        if (found++)
            break;
        snprintf(name, size, "%s\\%s", dumpDir, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return found == 1;
}

static int haspCrypt(const char *what, uint8_t *buf, uint32_t len)
{
    unsigned char md[32];
    char name[MAX_PATH], hex[65] = "";
    FILE *f;
    size_t got = 0;
    if (sha256(buf, len, md))
        for (int i = 0; i < 32; i++)
            sprintf(hex + 2 * i, "%02x", md[i]);
    snprintf(name, sizeof(name), "%s\\%s", dumpDir, hex);
    if (!hex[0] || !fileExists(name))
        answerBySize(len, name, sizeof(name));
    if ((f = fopen(name, "rb")))
    {
        got = fread(buf, 1, len, f);
        fclose(f);
    }
    g7Log("%s len %u sha256 %s -> %s\n", what, len, hex, f ? (got == len ? "answered" : "size mismatch") : "no answer");
    return 0;
}
static SYSV int haspEncrypt(uint32_t h, uint8_t *b, uint32_t l) { (void)h; return haspCrypt("encrypt", b, l); }
static SYSV int haspDecrypt(uint32_t h, uint8_t *b, uint32_t l) { (void)h; return haspCrypt("decrypt", b, l); }

// ---------------------------------------------------------------------------
// Video mode (see g7_rt.c).

static SYSV void (*parseArgsOrig)(int, char **);
static SYSV void parseArgs(int argc, char **argv)
{
    uint8_t *video = (uint8_t *)game->videoSettings;
    parseArgsOrig(argc, argv);
    if (g7Config.width <= 0 || g7Config.height <= 0)
        return;
    *(int32_t *)video = g7Config.fullscreen ? 2 : 1;
    *(int32_t *)(video + 4) = g7Config.width;
    *(int32_t *)(video + 8) = g7Config.height;
    *(float *)(video + 12) = (float)g7Config.width / (float)g7Config.height;
}

static void *dlsymHook(const char *name)
{
    return g7GameDlsymHook ? g7GameDlsymHook(name) : NULL;
}

// ---------------------------------------------------------------------------

static int sameFile(const char *a, const char *b)
{
    char fa[MAX_PATH], fb[MAX_PATH];
    return _fullpath(fa, a, sizeof(fa)) && _fullpath(fb, b, sizeof(fb)) && !_stricmp(fa, fb) && fileExists(fa);
}

void lxG7Setup(LxModule *exe)
{
    char cwd[MAX_PATH], cabinetGame[MAX_PATH], here[MAX_PATH];
    int missing = 0;

    tmpInit();
    if (!(game = g7Find(lxModuleDynamic(exe))))
    {
        lxLog("%s is not a known g7 game (dynamic section %#llx)\n", lxModuleName(exe),
              (unsigned long long)lxModuleDynamic(exe));
        snprintf(g7Root, sizeof(g7Root), "%s", lxGameDir);
        return;
    }
    // The cabinet's layout: the game in g7/<game> two levels under /pm.
    GetCurrentDirectoryA(sizeof(cwd), cwd);
    snprintf(cabinetGame, sizeof(cabinetGame), "..\\..%s\\%s", game->cabinetDir + 3, lxModuleName(exe));
    for (char *c = cabinetGame; *c; c++)
        if (*c == '/')
            *c = '\\';
    snprintf(here, sizeof(here), "%s", lxModuleName(exe));
    if (sameFile(here, cabinetGame))
    {
        _fullpath(g7Root, "..\\..", sizeof(g7Root));
        snprintf(g7GameDir, sizeof(g7GameDir), "%s%s", g7Root, game->cabinetDir + 3);
        snprintf(dumpDir, sizeof(dumpDir), "%s\\g7\\dump", g7Root);
    }
    else
    {
        snprintf(g7Root, sizeof(g7Root), "%s", cwd);
        snprintf(g7GameDir, sizeof(g7GameDir), "%s", cwd);
        snprintf(dumpDir, sizeof(dumpDir), "%s\\dump", cwd);
    }
    for (char *c = g7GameDir; *c; c++)
        if (*c == '/')
            *c = '\\';
    snprintf(dongleMemPath, sizeof(dongleMemPath), "%s\\dongle_memory.dmp", g7GameDir);
    dongleMemLoad();
    loadConfig();
    g7Log("%s: /pm is %s, the game's folder %s, answers in %s\n", game->name, g7Root, g7GameDir, dumpDir);

    for (size_t i = 0; i < game->importCount; i++)
    {
        const G7Import *imp = &game->imports[i];
        void *p;
        if (!imp->name[0])
            continue;
        if (!(p = lxGlobalSymbol(imp->name)))
        {
            missing++;
            lxTraceLog("missing import %s@%s (slot %#lx)\n", imp->name, imp->version, imp->slot);
            p = lxUnresolvedStub(imp->name);
        }
        *(void **)(uintptr_t)imp->slot = p;
    }
    g7Log("imports rebuilt, %d missing\n", missing);
    g7Detour(game->hasp.login, haspLogin);
    g7Detour(game->hasp.logout, haspOk);
    g7Detour(game->hasp.encrypt, haspEncrypt);
    g7Detour(game->hasp.decrypt, haspDecrypt);
    g7Detour(game->hasp.free, haspFree);
    g7Detour(game->hasp.sessionInfo, haspSessionInfo);
    g7Detour(game->hasp.read, haspRead);
    g7Detour(game->hasp.write, haspWrite);
    if (game->parseArgs && game->videoSettings)
        *(void **)&parseArgsOrig = g7HookNear(game->parseArgs, game->parseArgsPrologue, parseArgs);
    lxDlsymHook = dlsymHook;
    if (game->install)
        game->install(game);
}

// The game's command line after its name: Centipede takes its full screen
// size there (-f<w>x<h>, as the Linux launcher gives it), [Display] WIDTH and
// HEIGHT, else the screen's.
int lxG7Args(char **argv, int max)
{
    static char size[32];
    int w, h;
    if (!game || strcmp(game->envPrefix, "CENT") || max < 1 || !lxIniInt("Display", "FULLSCREEN", 0))
        return 0;
    w = lxIniInt("Display", "WIDTH", 0);
    h = lxIniInt("Display", "HEIGHT", 0);
    // At most the screen's: the frame is drawn in the back buffer, scaled
    // up from there.
    if (w <= 0 || h <= 0 || w > GetSystemMetrics(SM_CXSCREEN) || h > GetSystemMetrics(SM_CYSCREEN))
    {
        w = GetSystemMetrics(SM_CXSCREEN);
        h = GetSystemMetrics(SM_CYSCREEN);
    }
    snprintf(size, sizeof(size), "-f%dx%d", w, h);
    argv[0] = size;
    g7Log("full screen %dx%d\n", w, h);
    return 1;
}

// Halo and Minecraft Dungeons: their hooks are Linux-only so far.
void g7HaloInstall(const G7Game *g) { g7Log("%s: its own hooks are not ported to Windows\n", g->name); }
void g7MinecraftInstall(const G7Game *g) { g7Log("%s: its own hooks are not ported to Windows\n", g->name); }
const char *g7HaloMapPath(const char *path, char *buf) { (void)path; (void)buf; return NULL; }
