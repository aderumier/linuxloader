// linuxloader64.exe (see ll64.h):
//   linuxloader64.exe [-c settings.ini] -g <game folder>
// The game's executable has fixed addresses (0x400000 up) where Windows puts
// the process heap and thread stacks: the loader starts itself again
// suspended, reserves them in that process first, and waits for it.
#include <windows.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ll64.h"
#include "../config/iniParser.h"

#define RESERVE_START 0x400000
#define RESERVE_END 0x8000000

char lxGameDir[MAX_PATH], lxExeDir[MAX_PATH];
LxModule *lxExe;
static IniConfig *ini;
static char exePath[MAX_PATH];

void lxBridgesInit(void);
void lxDiagInit(void);
const char *lxG7FindExecutable(const char *gameDir, char *exe, size_t size);
char **lxEnvironment(void);

void *lxIni(void) { return ini; }

const char *lxIniValue(const char *section, const char *key)
{
    const char *v = ini ? iniGetValue(ini, section, key) : NULL;
    return v && *v ? v : NULL;
}

int lxIniInt(const char *section, const char *key, int def)
{
    const char *v = lxIniValue(section, key);
    if (!v)
        return def;
    if (!_stricmp(v, "true"))
        return 1;
    if (!_stricmp(v, "false"))
        return 0;
    if (!_stricmp(v, "auto"))
        return def;
    return atoi(v);
}

static int relaunch(void)
{
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    void *reserved;

    SetEnvironmentVariableA("LL64_CHILD", "1");
    if (!CreateProcessW(NULL, GetCommandLineW(), NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
        fprintf(stderr, "ll64: cannot start the game process (error %lu)\n", GetLastError());
        return 1;
    }
    reserved = VirtualAllocEx(pi.hProcess, (void *)RESERVE_START, RESERVE_END - RESERVE_START, MEM_RESERVE,
                              PAGE_EXECUTE_READWRITE);
    if (!reserved)
    {
        MEMORY_BASIC_INFORMATION m;
        for (char *a = (char *)0x10000; (uintptr_t)a < RESERVE_END && VirtualQueryEx(pi.hProcess, a, &m, sizeof(m));
             a = (char *)m.BaseAddress + m.RegionSize)
            if (m.State != MEM_FREE)
                fprintf(stderr, "ll64:   %p %#llx state %#lx type %#lx\n", m.BaseAddress,
                        (unsigned long long)m.RegionSize, m.State, m.Type);
    }
    if (!reserved)
        fprintf(stderr, "ll64: cannot reserve %#x..%#x (error %lu)\n", RESERVE_START, RESERVE_END, GetLastError());
    ResumeThread(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

int lxG7Args(char **argv, int max);

static DWORD WINAPI gameThread(void *arg)
{
    char *argv[8] = {exePath};
    int argc = 1;
    (void)arg;
    argc += lxG7Args(argv + 1, 6);
    argv[argc] = NULL;
    lxTlsThreadInit();
    lxRunExecutable(lxExe, argc, argv, lxEnvironment());
}

int main(int argc, char **argv)
{
    const char *config = NULL, *gameDir = NULL;
    char winExe[MAX_PATH], *slash;
    HANDLE thread;

    if (!getenv("LL64_CHILD"))
        return relaunch();
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    lxTrace = getenv("LL64_TRACE") && atoi(getenv("LL64_TRACE")) > 0;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-c") && i + 1 < argc)
            config = argv[++i];
        else if (!strcmp(argv[i], "-g") && i + 1 < argc)
            gameDir = argv[++i];
        else if (argv[i][0] != '-')
            gameDir = argv[i];
    }
    GetModuleFileNameA(NULL, lxExeDir, sizeof(lxExeDir));
    if ((slash = strrchr(lxExeDir, '\\')))
        *slash = 0;
    if (!gameDir)
    {
        fprintf(stderr, "usage: linuxloader64.exe [-c settings.ini] -g <game folder>\n");
        return 1;
    }
    if (!_fullpath(lxGameDir, gameDir, sizeof(lxGameDir)))
        snprintf(lxGameDir, sizeof(lxGameDir), "%s", gameDir);
    if (config)
        ini = iniLoad(config);
    else
    {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\linuxloader.ini", lxGameDir);
        ini = iniLoad(path);
    }
    // The log: [System] DEBUG_MSGS=1 (or LL64_LOG=1, LL64_TRACE=1).
    lxLogEnabled = lxIniInt("System", "DEBUG_MSGS", 0) || getenv("LL64_LOG") || lxTrace;
    lxLog("linuxloader64, game folder %s, settings %s\n", lxGameDir, ini ? (config ? config : "the game's") : "none");

    if (!lxTlsInit())
        return 1;
    lxBridgesInit();
    lxDiagInit();

    // The game's executable, in the folder its runtime expects to start from.
    if (!lxG7FindExecutable(lxGameDir, winExe, sizeof(winExe)))
    {
        lxLog("no known game executable in %s\n", lxGameDir);
        return 1;
    }
    snprintf(exePath, sizeof(exePath), "%s", winExe);
    if ((slash = strrchr(winExe, '\\')))
    {
        *slash = 0;
        _chdir(winExe);
        {
            char lib[MAX_PATH];
            snprintf(lib, sizeof(lib), "%s\\lib", winExe);
            lxAddLibraryPath(lib);
        }
        *slash = '\\';
    }
    {
        char deps[MAX_PATH];
        snprintf(deps, sizeof(deps), "%s\\ll-deps64", lxExeDir);
        lxAddLibraryPath(deps);
        snprintf(deps, sizeof(deps), "%s\\ll-deps64", lxGameDir);
        lxAddLibraryPath(deps);
    }

    if (!(lxExe = lxLoadExecutable(winExe)))
        return 1;
    lxG7Setup(lxExe);

    // The Linux main thread's stack: 64 MiB, committed (the Linux code does
    // not probe its pages as Windows code does).
    {
        extern DWORD lxGameThreadId;
        thread = CreateThread(NULL, 64 << 20, gameThread, NULL, 0, &lxGameThreadId);
    }
    if (!thread)
    {
        lxLog("cannot start the game thread (error %lu)\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(thread, INFINITE);
    return 0;
}
