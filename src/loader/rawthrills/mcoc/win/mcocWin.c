// Marvel Contest of Champions on Windows: the game's Linux data run by Unity's
// own Windows player of the same version (2021.3.23f1, Mono, x86-64), from
// the same dump folder the Linux player runs it from (see
// docs/marvel-contest-of-champions.md):
//
//   mcoc2.exe          this launcher (mcocLauncher.c): mcocWinMain below
//   mcocwin.dll        this library: the cabinet's plugins and the hooks
//   UnityPlayer.dll    Unity's (fetched: scripts/windows/fetch-unity-mcoc.ps1)
//   MonoBleedingEdge\  Unity's Windows Mono (EmbedRuntime\mono-2.0-bdwgc.dll)
//   mcoc2_Data\Managed_win32\  the Windows builds of Mono's class libraries
//                      the game ships (mcoc2_Data\Managed holds Linux's,
//                      which call System.Native for their files)
//
// The Linux player's files beside them (mcoc2.x86_64, UnityPlayer.so,
// mcoc2_Data\MonoBleedingEdge, the .so plugins) are left alone, as Linux
// leaves these.
//
// Before Unity starts, Mono is loaded and hooked: Managed_win32 goes first in
// its assembly path, and the game's seven plugins (RIO2, UnityNatives,
// UnitySCard, SCard, WebCamRT, WeChatCURL, the HASP library) are mapped to
// this library, so that one state serves them all as linuxloader64.so does
// on Linux. The plugins and the Mono helpers are the Linux ones
// (mcocPlugins.c, unity/unityRio2.c, unityMono.c); the input (mcocWinInput.c), the
// cameras (none, mcocWinStubs.c) and the picture's hook here are Windows'.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <stdarg.h>
#include <stdint.h>
#include <wctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glad/gl.h>
#include "MinHook.h"
#include "../mcoc.h"
#include "../mcocBinder.h"
#include "../../../config/iniParser.h"

extern const UnityGame mcocGame;
const UnityGame *unityGame = &mcocGame;
void *unityMonoHandle;
static IniConfig *ini;
static char gameDir[MAX_PATH], dataDir[MAX_PATH], selfPath[MAX_PATH];

// The settings parser's log (iniParser.c): our log.
int logGeneric(int level, const char *file, int line, const char *message, ...)
{
    va_list ap;
    (void)level; (void)file; (void)line;
    va_start(ap, message);
    vfprintf(stderr, message, ap);
    va_end(ap);
    fputc('\n', stderr);
    return 0;
}

const char *unityIniValue(const char *section, const char *key)
{
    const char *v = ini ? iniGetValue(ini, section, key) : NULL;
    return v && *v ? v : NULL;
}

int unityIniInt(const char *section, const char *key, int def)
{
    const char *v = unityIniValue(section, key);
    if (!v)
        return def;
    if (!_stricmp(v, "true"))
        return 1;
    if (!_stricmp(v, "false"))
        return 0;
    return atoi(v);
}

// ---------------------------------------------------------------------------
// Mono.

#define MONO_DLL "MonoBleedingEdge\\EmbedRuntime\\mono-2.0-bdwgc.dll"

static const char *const plugins[] = {"RIO2",      "UnityNatives", "UnitySCard", "SCard",
                                      "WebCamRT",  "WeChatCURL",   "hasp_linux_x86_64_25328"};

static void (*realSetAssembliesPath)(const char *);
static void (*realSetAssembliesPathNull)(const char *);
static void (*dllmapInsert)(void *, const char *, const char *, const char *, const char *);
static void *(*realJitInitVersion)(const char *, const char *);

// The Windows class libraries before the game's folder.
static void setAssembliesPath(const char *path)
{
    static char both[MAX_PATH * 4];
    snprintf(both, sizeof(both), "%s/Managed_win32;%s", dataDir, path ? path : "");
    for (char *c = both; *c; c++)
        if (*c == '\\')
            *c = '/';
    unityLog("Mono's assemblies: %s\n", both);
    realSetAssembliesPath(both);
}

// Unity's own call: the paths one after another, each NUL-terminated, an
// empty one last.
static void setAssembliesPathNull(const char *paths)
{
    static char both[MAX_PATH * 4];
    size_t o = 0;
    o += (size_t)snprintf(both, sizeof(both), "%s/Managed_win32", dataDir) + 1;
    for (char *c = both; *c; c++)
        if (*c == '\\')
            *c = '/';
    for (const char *p = paths; p && *p && o < sizeof(both) - MAX_PATH; p += strlen(p) + 1)
    {
        memcpy(both + o, p, strlen(p) + 1);
        o += strlen(p) + 1;
    }
    both[o] = 0;
    unityLog("Mono's assemblies: %s first\n", both);
    realSetAssembliesPathNull(both);
}

// Once Mono is up: the plugins' names mapped to this library.
static void *jitInitVersion(const char *domain, const char *version)
{
    void *d = realJitInitVersion(domain, version);
    for (size_t i = 0; i < sizeof(plugins) / sizeof(plugins[0]); i++)
        dllmapInsert(NULL, plugins[i], NULL, selfPath, NULL);
    unityLog("Mono %s up: the game's plugins are mcocwin.dll's\n", version ? version : "");
    return d;
}

// The game's free disk space (EB.DriveInfo) is the drive whose name its
// path starts with: Unity's paths have forward slashes ("C:/Users/..."),
// .NET's drive names a backslash ("C:\\"), so none matches and the game finds
// no room for its card data ("DownloadExtractor does not have sufficient
// disk space"). Mono's drives are named "C:/" (GetDiskFreeSpaceEx takes it).
static DWORD WINAPI monoLogicalDrives(DWORD size, LPWSTR out)
{
    DWORD n = GetLogicalDriveStringsW(size, out);
    if (n && n <= size && out)
        for (DWORD i = 0; i + 2 < n; i++)
            if (out[i + 1] == L':' && out[i + 2] == L'\\')
                out[i + 2] = L'/';
    return n;
}

// The import of that module from kernel32 pointed at to, if it has one.
static int patchImport(HMODULE module, const char *name, void *to)
{
    uint8_t *base = (uint8_t *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir->VirtualAddress)
        return 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir->VirtualAddress); d->Name; d++)
    {
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        IMAGE_THUNK_DATA *slots = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++)
        {
            DWORD old;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal) ||
                strcmp((const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, name))
                continue;
            VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old);
            slots->u1.Function = (ULONG_PTR)to;
            VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
            return 1;
        }
    }
    return 0;
}

static int hookMono(void)
{
    char path[MAX_PATH];
    HMODULE mono;
    void *set, *setNull, *init;
    snprintf(path, sizeof(path), "%s\\%s", gameDir, MONO_DLL);
    if (!(mono = LoadLibraryA(path)))
    {
        unityLog("cannot load %s (error %lu): Unity's Windows Mono is missing\n", path, GetLastError());
        return 0;
    }
    unityMonoHandle = mono;
    if (!patchImport(mono, "GetLogicalDriveStringsW", (void *)monoLogicalDrives))
        unityLog("Mono imports no GetLogicalDriveStringsW: the game may find no disk space\n");
    set = (void *)GetProcAddress(mono, "mono_set_assemblies_path");
    setNull = (void *)GetProcAddress(mono, "mono_set_assemblies_path_null_separated");
    init = (void *)GetProcAddress(mono, "mono_jit_init_version");
    *(void **)&dllmapInsert = (void *)GetProcAddress(mono, "mono_dllmap_insert");
    if (!set || !init || !dllmapInsert || MH_CreateHook(set, (void *)setAssembliesPath, (void **)&realSetAssembliesPath) ||
        MH_CreateHook(init, (void *)jitInitVersion, (void **)&realJitInitVersion) ||
        (setNull && MH_CreateHook(setNull, (void *)setAssembliesPathNull, (void **)&realSetAssembliesPathNull)))
    {
        unityLog("Mono's entry points not found or not hooked\n");
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// The cabinet's paths (see unityRt.c): the game keeps its files under /pm,
// the install's folder, which Windows makes the drive's \pm; its offline
// cache, copied at its first boot to root's Unity data
// (/root/.config/unity3d/Kabam/Champions), Unity's LocalLow here, where the
// player reads it (persistentDataPath); and the udev
// rule and script it writes at boot unless they exist, kept in
// LocalData\system. Mapped in the file calls Mono makes.

static wchar_t gameDirW[MAX_PATH], unityDataW[MAX_PATH];

static const struct { const wchar_t *from, *to; } cabinetFiles[] = {
    {L"\\etc\\udev\\rules.d\\10-usb-automount.rules", L"LocalData\\system\\10-usb-automount.rules"},
    {L"\\pm\\FlushBuffers.sh", L"LocalData\\system\\FlushBuffers.sh"},
};

static DWORD(WINAPI *realGetFileAttributesW)(LPCWSTR);

// The game's files (EB.FileSystem.FileDriver) are names under <cwd>/data,
// looked for as "/<name>": its leading separator is stripped when it is the
// system's, which on Windows it is not, and Path.Combine keeps the rooted
// name. So a path at the game drive's root that is not there, but is under
// data, is data's.
static const wchar_t *dataFallback(const wchar_t *path, const wchar_t *norm, wchar_t *buf, size_t size)
{
    // "X:\\name" on the game's drive, or "\\name" (its current one)
    if (!realGetFileAttributesW || !norm[1] || (path[1] == L':' && (gameDirW[0] | 0x20) != (path[0] | 0x20)))
        return NULL;
    // Only outside the game's folder: its relative names (the driver's first
    // try, "packs.txt") are its own, and must stay unfound there.
    size_t n = wcslen(gameDirW);
    if (path[1] == L':' && !_wcsnicmp(path, gameDirW, n) && (path[n] == L'\\' || path[n] == L'/' || !path[n]))
        return NULL;
    if (path[1] != L':' && gameDirW[2] != L'\\')
        return NULL; // the game at a drive's root: "\\name" is its own
    if (realGetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return NULL;
    swprintf(buf, size, L"%ls\\data%ls", gameDirW, norm);
    if (realGetFileAttributesW(buf) == INVALID_FILE_ATTRIBUTES)
        return NULL;
    if (getenv("MCOC_TRACE_FILES"))
        unityLog("files: %ls -> %ls\n", path, buf);
    return buf;
}

// path as the game's: NULL, or buf.
static const wchar_t *mapPlainPath(const wchar_t *path, wchar_t *buf, size_t size);

// The card data the game unpacks (bcg, gacha, in <Unity's data>\Kabam\Champions,
// <kind>\out\v1) is named by the SHA-1 of its download's path joined with
// Path.Combine: "/" on the cabinet, "\" here. The cabinet's copy (the offline
// cache, one file a kind) is so asked for under another name, and the game
// downloads it again, which it cannot offline. When the name asked for is
// missing, the one cached file of that folder, else of the game's own
// OfflineCache\Cache, is copied to that name (never redirected: the game
// deletes and rewrites its files by name); the game checks it by its CRC.
static int isHexName(const wchar_t *name)
{
    for (int i = 0; i < 40; i++)
        if (!iswxdigit(name[i]))
            return 0;
    return !_wcsicmp(name + 40, L".bin");
}

static BOOL(WINAPI *realCopyFileExW)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, LPBOOL, DWORD);

// The one card data file of dir (ending with a separator), into out.
static int onlyCacheFile(const wchar_t *dir, wchar_t *out, size_t size)
{
    wchar_t pattern[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int found = 0;
    swprintf(pattern, sizeof(pattern) / sizeof(pattern[0]), L"%ls*.bin", dir);
    if ((h = FindFirstFileW(pattern, &fd)) == INVALID_HANDLE_VALUE)
        return 0;
    do
        if (isHexName(fd.cFileName) && !found++)
            swprintf(out, size, L"%ls%ls", dir, fd.cFileName);
    while (FindNextFileW(h, &fd));
    FindClose(h);
    return found == 1;
}

static void cacheFallback(const wchar_t *asked)
{
    static const wchar_t champions[] = L"\\Kabam\\Champions\\";
    wchar_t path[MAX_PATH * 2], dir[MAX_PATH * 2], from[MAX_PATH * 2];
    size_t n = wcslen(unityDataW), len = wcslen(asked);
    const wchar_t *name, *rel;
    if (!*unityDataW || len < n + 50 || len >= sizeof(path) / sizeof(path[0]) || !realGetFileAttributesW ||
        !realCopyFileExW)
        return;
    for (size_t i = 0; i <= len; i++)
        path[i] = asked[i] == L'/' ? L'\\' : asked[i];
    name = path + len - 44;
    if (_wcsnicmp(path, unityDataW, n) || name[-1] != L'\\' || !isHexName(name) ||
        _wcsnicmp(name - 8, L"\\out\\v1", 7) || !(rel = wcsstr(path + n, champions)) ||
        realGetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES)
        return;
    swprintf(dir, sizeof(dir) / sizeof(dir[0]), L"%.*ls", (int)(name - path), path);
    if (!onlyCacheFile(dir, from, sizeof(from) / sizeof(from[0])))
    {
        // <game>\OfflineCache\Cache\<kind>\out\v1\ (the cabinet's)
        swprintf(dir, sizeof(dir) / sizeof(dir[0]), L"%ls\\OfflineCache\\Cache\\%.*ls", gameDirW,
                 (int)(name - rel - wcslen(champions)), rel + wcslen(champions));
        if (!onlyCacheFile(dir, from, sizeof(from) / sizeof(from[0])))
            return;
    }
    if (realCopyFileExW(from, path, NULL, NULL, NULL, 0))
        unityLog("card data: %ls copied as %ls\n", from, name);
    else
        unityLog("card data: cannot copy %ls to %ls (error %lu)\n", from, path, GetLastError());
}

// .NET's own extended paths ("\\\\?\\M:\\..."): mapped without the prefix, which
// is put back.
static const wchar_t *mapPath(const wchar_t *path, wchar_t *buf, size_t size)
{
    wchar_t plain[MAX_PATH * 2];
    if (!path || wcsncmp(path, L"\\\\?\\", 4))
        return mapPlainPath(path, buf, size);
    if (!mapPlainPath(path + 4, plain, sizeof(plain) / sizeof(plain[0])))
        return NULL;
    swprintf(buf, size, L"\\\\?\\%ls", plain);
    return buf;
}

static const wchar_t *mapPlainPath(const wchar_t *path, wchar_t *buf, size_t size)
{
    if (path && path[1] == L':')
        cacheFallback(path);
    wchar_t norm[MAX_PATH * 2];
    const wchar_t *p = path;
    size_t i;
    if (!p)
        return NULL;
    if (((p[0] | 0x20) >= L'a' && (p[0] | 0x20) <= L'z') && p[1] == L':')
        p += 2; // the drive Mono puts in front of a rooted path
    if (p[0] != L'\\' && p[0] != L'/')
        return NULL;
    for (i = 0; p[i] && i < sizeof(norm) / sizeof(norm[0]) - 1; i++)
        norm[i] = p[i] == L'/' ? L'\\' : p[i];
    norm[i] = 0;
    for (size_t f = 0; f < sizeof(cabinetFiles) / sizeof(cabinetFiles[0]); f++)
        if (!_wcsicmp(norm, cabinetFiles[f].from))
        {
            swprintf(buf, size, L"%ls\\%ls", gameDirW, cabinetFiles[f].to);
            return buf;
        }
    if (*unityDataW && !_wcsnicmp(norm, L"\\root\\.config\\unity3d", 21) && (!norm[21] || norm[21] == L'\\'))
    {
        swprintf(buf, size, L"%ls%ls", unityDataW, norm + 21);
        if (getenv("MCOC_TRACE_FILES"))
            unityLog("files: %ls -> %ls\n", path, buf);
        return buf;
    }
    if (_wcsnicmp(norm, L"\\pm", 3) || (norm[3] && norm[3] != L'\\'))
        return dataFallback(path, norm, buf, size);
    swprintf(buf, size, L"%ls%ls", gameDirW, norm + 3);
    return buf;
}

#define MAPPED(path, buf) (mapPath(path, buf, sizeof(buf) / sizeof(buf[0])) ? buf : (path))

// MCOC_TRACE_FILES: the paths the game asks for and does not find, logged.
static int traceFiles = -1;
static void traceMiss(const char *call, LPCWSTR asked, LPCWSTR used)
{
    DWORD e = GetLastError();
    if (traceFiles < 0)
        traceFiles = getenv("MCOC_TRACE_FILES") != NULL;
    if (traceFiles && asked)
        unityLog("files: %s %ls%s%ls: error %lu\n", call, asked, asked != used ? " -> " : "", asked != used ? used : L"", e);
    SetLastError(e);
}

static HANDLE(WINAPI *realCreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE WINAPI hookCreateFileW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD c, DWORD f, HANDLE t)
{
    wchar_t b[MAX_PATH * 2];
    LPCWSTR m = MAPPED(p, b);
    HANDLE h = realCreateFileW(m, a, s, sa, c, f, t);
    if (h == INVALID_HANDLE_VALUE)
        traceMiss("open", p, m);
    return h;
}
static BOOL(WINAPI *realCreateDirectoryW)(LPCWSTR, LPSECURITY_ATTRIBUTES);
static BOOL WINAPI hookCreateDirectoryW(LPCWSTR p, LPSECURITY_ATTRIBUTES sa)
{
    wchar_t b[MAX_PATH * 2];
    LPCWSTR m = MAPPED(p, b);
    BOOL ok = realCreateDirectoryW(m, sa);
    if (!ok)
        traceMiss("mkdir", p, m);
    return ok;
}
static BOOL(WINAPI *realRemoveDirectoryW)(LPCWSTR);
static BOOL WINAPI hookRemoveDirectoryW(LPCWSTR p)
{
    wchar_t b[MAX_PATH * 2];
    return realRemoveDirectoryW(MAPPED(p, b));
}
static BOOL(WINAPI *realDeleteFileW)(LPCWSTR);
static BOOL WINAPI hookDeleteFileW(LPCWSTR p)
{
    wchar_t b[MAX_PATH * 2];
    return realDeleteFileW(MAPPED(p, b));
}
static DWORD WINAPI hookGetFileAttributesW(LPCWSTR p)
{
    wchar_t b[MAX_PATH * 2];
    return realGetFileAttributesW(MAPPED(p, b));
}
static BOOL(WINAPI *realGetFileAttributesExW)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
static BOOL WINAPI hookGetFileAttributesExW(LPCWSTR p, GET_FILEEX_INFO_LEVELS l, LPVOID i)
{
    wchar_t b[MAX_PATH * 2];
    LPCWSTR m = MAPPED(p, b);
    BOOL ok = realGetFileAttributesExW(m, l, i);
    if (!ok)
        traceMiss("stat", p, m);
    return ok;
}
static BOOL(WINAPI *realSetFileAttributesW)(LPCWSTR, DWORD);
static BOOL WINAPI hookSetFileAttributesW(LPCWSTR p, DWORD a)
{
    wchar_t b[MAX_PATH * 2];
    return realSetFileAttributesW(MAPPED(p, b), a);
}
static HANDLE(WINAPI *realFindFirstFileExW)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
static HANDLE WINAPI hookFindFirstFileExW(LPCWSTR p, FINDEX_INFO_LEVELS l, LPVOID d, FINDEX_SEARCH_OPS o, LPVOID f, DWORD x)
{
    wchar_t b[MAX_PATH * 2];
    LPCWSTR m = MAPPED(p, b);
    HANDLE h = realFindFirstFileExW(m, l, d, o, f, x);
    if (h == INVALID_HANDLE_VALUE)
        traceMiss("find", p, m);
    return h;
}
static HANDLE(WINAPI *realFindFirstFileW)(LPCWSTR, LPWIN32_FIND_DATAW);
static HANDLE WINAPI hookFindFirstFileW(LPCWSTR p, LPWIN32_FIND_DATAW d)
{
    wchar_t b[MAX_PATH * 2];
    return realFindFirstFileW(MAPPED(p, b), d);
}
static BOOL(WINAPI *realMoveFileExW)(LPCWSTR, LPCWSTR, DWORD);
static BOOL WINAPI hookMoveFileExW(LPCWSTR a, LPCWSTR z, DWORD f)
{
    wchar_t b[MAX_PATH * 2], c[MAX_PATH * 2];
    return realMoveFileExW(MAPPED(a, b), MAPPED(z, c), f);
}
static BOOL WINAPI hookCopyFileExW(LPCWSTR a, LPCWSTR z, LPPROGRESS_ROUTINE r, LPVOID d, LPBOOL x, DWORD f)
{
    wchar_t b[MAX_PATH * 2], c[MAX_PATH * 2];
    return realCopyFileExW(MAPPED(a, b), MAPPED(z, c), r, d, x, f);
}
static BOOL(WINAPI *realReplaceFileW)(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPVOID, LPVOID);
static BOOL WINAPI hookReplaceFileW(LPCWSTR a, LPCWSTR z, LPCWSTR k, DWORD f, LPVOID e, LPVOID r)
{
    wchar_t b[MAX_PATH * 2], c[MAX_PATH * 2], d[MAX_PATH * 2];
    return realReplaceFileW(MAPPED(a, b), MAPPED(z, c), k ? MAPPED(k, d) : NULL, f, e, r);
}

static void hookPaths(void)
{
    static const struct { const char *name; void *hook; void **real; } calls[] = {
        {"CreateFileW", (void *)hookCreateFileW, (void **)&realCreateFileW},
        {"CreateDirectoryW", (void *)hookCreateDirectoryW, (void **)&realCreateDirectoryW},
        {"RemoveDirectoryW", (void *)hookRemoveDirectoryW, (void **)&realRemoveDirectoryW},
        {"DeleteFileW", (void *)hookDeleteFileW, (void **)&realDeleteFileW},
        {"GetFileAttributesW", (void *)hookGetFileAttributesW, (void **)&realGetFileAttributesW},
        {"GetFileAttributesExW", (void *)hookGetFileAttributesExW, (void **)&realGetFileAttributesExW},
        {"SetFileAttributesW", (void *)hookSetFileAttributesW, (void **)&realSetFileAttributesW},
        {"FindFirstFileExW", (void *)hookFindFirstFileExW, (void **)&realFindFirstFileExW},
        {"FindFirstFileW", (void *)hookFindFirstFileW, (void **)&realFindFirstFileW},
        {"MoveFileExW", (void *)hookMoveFileExW, (void **)&realMoveFileExW},
        {"CopyFileExW", (void *)hookCopyFileExW, (void **)&realCopyFileExW},
        {"ReplaceFileW", (void *)hookReplaceFileW, (void **)&realReplaceFileW},
    };
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    int hooked = 0;
    MultiByteToWideChar(CP_UTF8, 0, gameDir, -1, gameDirW, MAX_PATH);
    {
        wchar_t *low = NULL;
        if (SHGetKnownFolderPath(&FOLDERID_LocalAppDataLow, 0, NULL, &low) == S_OK && low)
            swprintf(unityDataW, MAX_PATH, L"%ls", low);
        CoTaskMemFree(low);
    }
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); i++)
    {
        void *fn = (void *)GetProcAddress(k32, calls[i].name);
        if (fn && !MH_CreateHook(fn, calls[i].hook, calls[i].real))
            hooked++;
    }
    unityLog("/pm is %s (%d file calls mapped)\n", gameDir, hooked);
}

// ---------------------------------------------------------------------------
// Offline: the game never reaches the network (Kabam's servers, its CDN, its
// chat): no name is resolved but the local host's, no connection made but to
// it (127.0.0.0/8, ::1). Unity's curl and Mono's sockets go through these.

static int localName(const char *n)
{
    return n && (!_stricmp(n, "localhost") || !strncmp(n, "127.", 4) || !strcmp(n, "::1"));
}

static int localNameW(const wchar_t *n)
{
    return n && (!_wcsicmp(n, L"localhost") || !wcsncmp(n, L"127.", 4) || !wcscmp(n, L"::1"));
}

static int localAddress(const struct sockaddr *a)
{
    if (!a)
        return 0;
    if (a->sa_family == AF_INET)
        return (ntohl(((const struct sockaddr_in *)a)->sin_addr.s_addr) >> 24) == 127;
    if (a->sa_family == AF_INET6)
    {
        const struct sockaddr_in6 *a6 = (const struct sockaddr_in6 *)a;
        static const uint8_t loop[16] = {[15] = 1};
        return !memcmp(&a6->sin6_addr, loop, 16) ||
               (IN6_IS_ADDR_V4MAPPED(&a6->sin6_addr) && a6->sin6_addr.s6_addr[12] == 127);
    }
    return 1; // not an Internet address
}

static void offlineLog(const char *what, const char *name)
{
    static volatile LONG said;
    if (InterlockedIncrement(&said) <= 20)
        unityLog("offline: %s %s refused\n", what, name ? name : "");
}

static INT(WSAAPI *realGetaddrinfo)(PCSTR, PCSTR, const ADDRINFOA *, PADDRINFOA *);
static INT WSAAPI hookGetaddrinfo(PCSTR node, PCSTR service, const ADDRINFOA *hints, PADDRINFOA *res)
{
    if (node && !localName(node))
    {
        offlineLog("lookup", node);
        return WSAHOST_NOT_FOUND;
    }
    return realGetaddrinfo(node, service, hints, res);
}
static INT(WSAAPI *realGetAddrInfoW)(PCWSTR, PCWSTR, const ADDRINFOW *, PADDRINFOW *);
static INT WSAAPI hookGetAddrInfoW(PCWSTR node, PCWSTR service, const ADDRINFOW *hints, PADDRINFOW *res)
{
    if (node && !localNameW(node))
    {
        offlineLog("lookup", "(wide)");
        return WSAHOST_NOT_FOUND;
    }
    return realGetAddrInfoW(node, service, hints, res);
}
static INT(WSAAPI *realGetAddrInfoExW)(PCWSTR, PCWSTR, DWORD, LPGUID, const ADDRINFOEXW *, PADDRINFOEXW *,
                                       struct timeval *, LPOVERLAPPED, LPLOOKUPSERVICE_COMPLETION_ROUTINE, LPHANDLE);
static INT WSAAPI hookGetAddrInfoExW(PCWSTR node, PCWSTR service, DWORD ns, LPGUID id, const ADDRINFOEXW *hints,
                                     PADDRINFOEXW *res, struct timeval *t, LPOVERLAPPED o,
                                     LPLOOKUPSERVICE_COMPLETION_ROUTINE cb, LPHANDLE h)
{
    if (node && !localNameW(node))
    {
        offlineLog("lookup", "(ex)");
        return WSAHOST_NOT_FOUND;
    }
    return realGetAddrInfoExW(node, service, ns, id, hints, res, t, o, cb, h);
}
static struct hostent *(WSAAPI *realGethostbyname)(const char *);
static struct hostent *WSAAPI hookGethostbyname(const char *name)
{
    if (!localName(name))
    {
        offlineLog("lookup", name);
        WSASetLastError(WSAHOST_NOT_FOUND);
        return NULL;
    }
    return realGethostbyname(name);
}
static int(WSAAPI *realConnect)(SOCKET, const struct sockaddr *, int);
static int WSAAPI hookConnect(SOCKET s, const struct sockaddr *a, int n)
{
    if (!localAddress(a))
    {
        offlineLog("connection", "");
        WSASetLastError(WSAENETUNREACH);
        return SOCKET_ERROR;
    }
    return realConnect(s, a, n);
}
static int(WSAAPI *realWSAConnect)(SOCKET, const struct sockaddr *, int, LPWSABUF, LPWSABUF, LPQOS, LPQOS);
static int WSAAPI hookWSAConnect(SOCKET s, const struct sockaddr *a, int n, LPWSABUF i, LPWSABUF o, LPQOS q, LPQOS g)
{
    if (!localAddress(a))
    {
        offlineLog("connection", "");
        WSASetLastError(WSAENETUNREACH);
        return SOCKET_ERROR;
    }
    return realWSAConnect(s, a, n, i, o, q, g);
}

static void hookNetwork(void)
{
    static const struct { const char *name; void *hook; void **real; } calls[] = {
        {"getaddrinfo", (void *)hookGetaddrinfo, (void **)&realGetaddrinfo},
        {"GetAddrInfoW", (void *)hookGetAddrInfoW, (void **)&realGetAddrInfoW},
        {"GetAddrInfoExW", (void *)hookGetAddrInfoExW, (void **)&realGetAddrInfoExW},
        {"gethostbyname", (void *)hookGethostbyname, (void **)&realGethostbyname},
        {"connect", (void *)hookConnect, (void **)&realConnect},
        {"WSAConnect", (void *)hookWSAConnect, (void **)&realWSAConnect},
    };
    HMODULE ws = LoadLibraryA("ws2_32.dll");
    int hooked = 0;
    for (size_t i = 0; ws && i < sizeof(calls) / sizeof(calls[0]); i++)
    {
        void *fn = (void *)GetProcAddress(ws, calls[i].name);
        if (fn && !MH_CreateHook(fn, calls[i].hook, calls[i].real))
            hooked++;
    }
    unityLog("offline: %d network calls held to the local host\n", hooked);
}

// ---------------------------------------------------------------------------
// The cabinet's commands (Process.Start, see unityRt.c's mcocCommand): sync
// (its front end's loop flushes the disks, and stops if that throws),
// udevadm, systemctl, mount... Windows has none: one it cannot find, named
// without an extension, runs as a command that does nothing and succeeds;
// reboot, shutdown and poweroff end the game.

static BOOL(WINAPI *realCreateProcessW)(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD,
                                       LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION);

// The program's name, without its folder (and quotes), into out.
static void programName(LPCWSTR app, LPCWSTR cmd, wchar_t *out, size_t size)
{
    const wchar_t *p = app ? app : cmd, *end, *base;
    *out = 0;
    if (!p)
        return;
    while (*p == L' ')
        p++;
    if (!app && *p == L'"')
        for (end = ++p; *end && *end != L'"'; end++)
            ;
    else
        for (end = p; *end && (app || *end != L' '); end++)
            ;
    for (base = end; base > p && base[-1] != L'\\' && base[-1] != L'/'; base--)
        ;
    swprintf(out, size, L"%.*ls", (int)(end - base), base);
}

static BOOL WINAPI hookCreateProcessW(LPCWSTR app, LPWSTR cmd, LPSECURITY_ATTRIBUTES pa, LPSECURITY_ATTRIBUTES ta,
                                      BOOL inherit, DWORD flags, LPVOID env, LPCWSTR dir, LPSTARTUPINFOW si,
                                      LPPROCESS_INFORMATION pi)
{
    wchar_t name[MAX_PATH], nothing[MAX_PATH + 32];
    DWORD e;
    BOOL ok;
    programName(app, cmd, name, sizeof(name) / sizeof(name[0]));
    if (!_wcsicmp(name, L"reboot") || !_wcsicmp(name, L"shutdown") || !_wcsicmp(name, L"poweroff"))
    {
        unityLog("%ls: the game ends\n", name);
        fflush(stderr);
        TerminateProcess(GetCurrentProcess(), 0);
    }
    if ((ok = realCreateProcessW(app, cmd, pa, ta, inherit, flags, env, dir, si, pi)))
        return ok;
    e = GetLastError();
    if ((e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) || !*name || wcschr(name, L'.'))
    {
        SetLastError(e);
        return ok;
    }
    unityLog("not run: %ls (a cabinet command)\n", name);
    GetSystemDirectoryW(nothing, MAX_PATH);
    wcscat(nothing, L"\\cmd.exe");
    {
        wchar_t line[] = L"cmd.exe /c exit 0";
        return realCreateProcessW(nothing, line, pa, ta, FALSE, flags | CREATE_NO_WINDOW, NULL, NULL, si, pi);
    }
}

static void hookProcesses(void)
{
    void *fn = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateProcessW");
    if (!fn || MH_CreateHook(fn, (void *)hookCreateProcessW, (void **)&realCreateProcessW))
        unityLog("CreateProcessW not hooked: the cabinet's commands fail\n");
}

// ---------------------------------------------------------------------------
// The picture: the card binder drawn over each frame, on Unity's GL thread,
// just before it is presented.

static BOOL(WINAPI *realSwapBuffers)(HDC);
static HWND gameWindow;

static void *glProc(const char *name)
{
    static HMODULE gl;
    void *p = (void *)wglGetProcAddress(name);
    if ((uintptr_t)p <= 3 || p == (void *)-1)
    {
        if (!gl)
            gl = LoadLibraryA("opengl32.dll");
        p = (void *)GetProcAddress(gl, name);
    }
    return p;
}

static BOOL WINAPI swapBuffers(HDC dc)
{
    static int glLoaded;
    RECT r;
    HWND w = WindowFromDC(dc);
    if (w)
        gameWindow = w;
    if (!glLoaded)
        glLoaded = wglGetCurrentContext() && gladLoadGL((GLADloadfunc)glProc) ? 1 : -1;
    if (glLoaded > 0 && w && GetClientRect(w, &r) && r.right > 0 && r.bottom > 0)
        mcocBinderDraw(NULL, 0, r.right, r.bottom);
    return realSwapBuffers(dc);
}

static int gameFocused(void)
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg)
        GetWindowThreadProcessId(fg, &pid);
    return fg && pid == GetCurrentProcessId();
}

int mcocWinFocused(void)
{
    return gameFocused();
}

// The pointer over the game's window, in its pixels, for the binder.
int mcocPointer(int width, int height, float *x, float *y, int *button)
{
    POINT p;
    RECT r;
    if (!gameWindow || !gameFocused() || !GetCursorPos(&p) || !ScreenToClient(gameWindow, &p) ||
        !GetClientRect(gameWindow, &r) || r.right <= 0 || r.bottom <= 0)
        return 0;
    *x = (float)p.x * width / r.right;
    *y = (float)p.y * height / r.bottom;
    *button = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    return 1;
}

static int hookSwap(void)
{
    HMODULE gdi = LoadLibraryA("gdi32.dll");
    void *swap = gdi ? (void *)GetProcAddress(gdi, "SwapBuffers") : NULL;
    if (!swap || MH_CreateHook(swap, (void *)swapBuffers, (void **)&realSwapBuffers))
    {
        unityLog("SwapBuffers not hooked: no card binder\n");
        return 0;
    }
    return 1;
}

// Esc or Alt+F4 quits, while the game's window has the keyboard (the game's
// own quit is the cabinet's power switch).
static DWORD WINAPI quitWatch(void *arg)
{
    (void)arg;
    for (;;)
    {
        Sleep(50);
        if (gameFocused() && ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) ||
                              ((GetAsyncKeyState(VK_F4) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000))))
        {
            unityLog("quit from the keyboard\n");
            fflush(stderr);
            TerminateProcess(GetCurrentProcess(), 0);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// MCOC_EB_LOG: the game's own log (EB.Debug, kept in memory) copied to ours,
// once a frame from the main thread (mcocFrame).

void mcocWinFrame(void)
{
    static int on = -1;
    static MonoObject *buffer;
    static char *(*toUtf8)(MonoObject *);
    static void (*monoFree)(void *);
    if (on < 0)
        on = getenv("MCOC_EB_LOG") != NULL;
    if (!on)
        return;
    if (!buffer)
    {
        MonoImage *fuse = unityMonoImage("Fuse.NoUnityDeps");
        buffer = fuse ? unityMonoStaticField(fuse, "EB", "Debug", "_buffer") : NULL;
        *(void **)&toUtf8 = (void *)GetProcAddress((HMODULE)unityMonoHandle, "mono_string_to_utf8");
        *(void **)&monoFree = (void *)GetProcAddress((HMODULE)unityMonoHandle, "mono_free");
        if (!buffer || !toUtf8)
            return;
    }
    for (int i = 0; i < 200; i++)
    {
        int ok = 0;
        MonoObject *line = unityMonoCall(buffer, "StringArrayQueue:Dequeue()", NULL, &ok);
        char *text;
        if (!ok || !line || !(text = toUtf8(line)))
            break;
        unityLog("game: %s\n", text);
        monoFree(text);
    }
}

// ---------------------------------------------------------------------------

typedef int(WINAPI *UnityMainFn)(HINSTANCE, HINSTANCE, LPWSTR, int);

// The launcher's entry: args are its command line after the program's name
// ("-c <settings.ini>" first, the rest handed to Unity).
__declspec(dllexport) int mcocWinMain(HINSTANCE instance, const wchar_t *args, int show)
{
    wchar_t unityArgs[4096], extra[512];
    char iniPath[MAX_PATH] = "", logPath[MAX_PATH], *slash;
    const wchar_t *rest = args ? args : L"";
    HMODULE player;
    UnityMainFn unityMain;
    int w, h, fs;

    GetModuleFileNameA(NULL, gameDir, sizeof(gameDir));
    if ((slash = strrchr(gameDir, '\\')))
        *slash = 0;
    GetModuleFileNameA(GetModuleHandleA("mcocwin.dll"), selfPath, sizeof(selfPath));
    SetCurrentDirectoryA(gameDir);
    snprintf(dataDir, sizeof(dataDir), "%s\\mcoc2_Data", gameDir);

    // -c <settings.ini>
    while (*rest == L' ')
        rest++;
    if (!wcsncmp(rest, L"-c ", 3))
    {
        const wchar_t *p = rest + 3, *end;
        wchar_t quote = 0;
        while (*p == L' ')
            p++;
        if (*p == L'"')
            quote = *p++;
        for (end = p; *end && (quote ? *end != quote : *end != L' '); end++)
            ;
        WideCharToMultiByte(CP_UTF8, 0, p, (int)(end - p), iniPath, sizeof(iniPath) - 1, NULL, NULL);
        iniPath[end - p < (int)sizeof(iniPath) ? end - p : (int)sizeof(iniPath) - 1] = 0;
        rest = *end ? end + 1 : end;
    }
    if (!iniPath[0])
        snprintf(iniPath, sizeof(iniPath), "%s\\linuxloader.ini", gameDir);
    ini = iniLoad(iniPath);

    // Our log, the game's own beside it (rt_data\gamelog.txt).
    snprintf(logPath, sizeof(logPath), "%s\\mcocwin.log", gameDir);
    freopen(logPath, "w", stderr);
    setvbuf(stderr, NULL, _IONBF, 0);
    unityLog("Marvel Contest of Champions on Windows, settings %s%s\n", iniPath, ini ? "" : " (none)");

    if (MH_Initialize() || !hookMono())
    {
        MessageBoxA(NULL, "Unity's Windows player is missing or incomplete (MonoBleedingEdge).\n"
                    "Run fetch-unity-mcoc.ps1 in the game's folder first.", "Marvel Contest of Champions",
                    MB_ICONERROR);
        return 1;
    }
    hookSwap();
    hookPaths();
    hookNetwork();
    hookProcesses();
    MH_EnableHook(MH_ALL_HOOKS);
    CloseHandle(CreateThread(NULL, 0, quitWatch, NULL, 0, NULL));

    // Unity's command line: OpenGL Core (the dump has only its shaders) at
    // [Display]'s size (mcocPlugins.c holds the game to it afterwards).
    w = unityIniInt("Display", "WIDTH", 0);
    h = unityIniInt("Display", "HEIGHT", 0);
    fs = unityIniInt("Display", "FULLSCREEN", 0) != 0;
    if (w <= 0 || h <= 0)
    {
        w = fs ? GetSystemMetrics(SM_CXSCREEN) : 1280;
        h = fs ? GetSystemMetrics(SM_CYSCREEN) : 720;
    }
    swprintf(extra, sizeof(extra) / sizeof(extra[0]),
             L"-force-glcore -screen-width %d -screen-height %d -screen-fullscreen %d -logFile \"%hs\\rt_data\\unity.log\"",
             w, h, fs, gameDir);
    swprintf(unityArgs, sizeof(unityArgs) / sizeof(unityArgs[0]), L"%ls %ls", extra, rest);

    if (!(player = LoadLibraryA("UnityPlayer.dll")) ||
        !(unityMain = (UnityMainFn)(void *)GetProcAddress(player, "UnityMain")))
    {
        unityLog("cannot load UnityPlayer.dll (error %lu)\n", GetLastError());
        MessageBoxA(NULL, "UnityPlayer.dll is missing.\nRun fetch-unity-mcoc.ps1 in the game's folder first.",
                    "Marvel Contest of Champions", MB_ICONERROR);
        return 1;
    }
    unityLog("Unity: %ls\n", unityArgs);
    return unityMain(instance, NULL, unityArgs, show);
}
