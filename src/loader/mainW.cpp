#if defined(_WIN32) || defined(__MINGW32__)
#include <cstdlib>

#include <filesystem>
#include <winsock2.h>
#include <windows.h>
#include <psapi.h>
#include <libgen.h>

#include "glad/gl.h"
#include "elfLoader/alsa2sdlBridge.hpp"
#include "elfLoader/segaapiBridge.hpp"
#include "elfLoader/gccBridge.hpp"
#include "elfLoader/ipcBridge.hpp"
#include "elfLoader/elfLoader.hpp"
#include "elfLoader/filesystemBridge.hpp"
#include "elfLoader/libcBridge.hpp"
#include "elfLoader/gccBridge.hpp"
#include "elfLoader/graphicsBridge.hpp"
#include "elfLoader/pthreadBridge.hpp"
#include "elfLoader/rtBridge.hpp"
#include "elfLoader/regexBridge.hpp"
#include "elfLoader/termiosBridge.hpp"
#include "elfLoader/networkBridge.hpp"
#include "elfLoader/mathBridge.hpp"
#include "elfLoader/symbolResolver.hpp"
#include "elfLoader/pthread/pthreadEmu.hpp"
#include "loader/config/config.h"
#include "log/log.h"
#include "graphics/gpuVendor.h"
#include "init.h"
#include "patching/patch.h"
#include "input/sdlInput.h"
#include "mainShared.h"
#include "rawthrills/rawthrills.h"

std::string g_absoluteElfPath;
uint32_t partialElfCrc = 0;

extern SDLControllers sdlJoysticks;

extern int gGrp;

// Deal or No Deal's parallel port panel (rawthrills/rtDond.c): 1 when the
// in/out is the panel's port (0x378..0x37a), else 0.
extern "C" int rtDondPortIo(int in, uint16_t port, uint32_t *eax);

HMODULE hGlDLL = NULL;

LONG CALLBACK myVectoredHandler(PEXCEPTION_POINTERS ExceptionInfo)
{
    DWORD exceptionCode = ExceptionInfo->ExceptionRecord->ExceptionCode;
    PCONTEXT ctx = ExceptionInfo->ContextRecord;

    if (exceptionCode == EXCEPTION_ACCESS_VIOLATION || exceptionCode == EXCEPTION_PRIV_INSTRUCTION) {
        uint8_t* pint = (uint8_t*)ctx->Eip;
        if (pint && !IsBadReadPtr(pint, 2)) {
            if (pint[0] == 0xCD && pint[1] == 0x80) {
                if (ctx->Eax == 0xE0) {
                    ctx->Eax = (DWORD)PthreadEmu::pthreadSelf();
                    ctx->Eip += 2;
                    return EXCEPTION_CONTINUE_EXECUTION;
                }
            }
            // Deal or No Deal's parallel port panel: in %dx,%al / out %al,%dx
            // (a privileged instruction) on 0x378..0x37a. Two bytes.
            else if ((pint[0] == 0xEC || pint[0] == 0xEE) &&
                     rtDondPortIo(pint[0] == 0xEC, (uint16_t)(ctx->Edx & 0xFFFF), (uint32_t *)&ctx->Eax)) {
                ctx->Eip += 2;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
    }

    // Temporary: the Roxor (Tux Racer) Windows boot trace, unbuffered:
    // only on, with ROXOR_TRACE set (it writes a line per exception).
    if (getenv("ROXOR_TRACE"))
    {
        char buf[256];
        int n = snprintf(buf, sizeof(buf), "X%08lX %08lX %08lX %08lX\n",
                         exceptionCode, ctx->Eip, ctx->Esp, ctx->Ebp);
        // The faulting module (its name and the offset into it).
        HMODULE modules[512];
        DWORD cb = 0, sz = 0;
        if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &cb))
        {
            for (DWORD m = 0; m < cb; m += sizeof(modules[0]))
            {
                MODULEINFO mi;
                if (!GetModuleInformation(GetCurrentProcess(), modules[m], &mi, sizeof(mi)))
                    continue;
                if (ctx->Eip >= (DWORD)(uintptr_t)mi.lpBaseOfDll &&
                    ctx->Eip < (DWORD)(uintptr_t)(char *)mi.lpBaseOfDll + mi.SizeOfImage)
                {
                    char name[MAX_PATH];
                    if (GetModuleBaseNameA(GetCurrentProcess(), modules[m], name, sizeof(name)))
                        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%s +%lX\n", name,
                                      ctx->Eip - (DWORD)(uintptr_t)mi.lpBaseOfDll);
                    break;
                }
            }
        }
        FILE *f = fopen("roxor_trace.log", "a");
        if (f)
        {
            fwrite(buf, 1, (size_t)n, f);
            fclose(f);
        }
        if (getenv("ROXOR_PAUSE"))
        {
            static int paused;
            if (!paused)
            {
                paused = 1;
                Sleep(30000);
            }
        }
    }

    return EXCEPTION_CONTINUE_SEARCH;
}   

void initBridges()
{
    FileSystemBridge::initBridges();
    GraphicsBridge::initBridges();
    LibcBridge::initBridges();
    PthreadBridge::initBridges();
    TermiosBridge::initBridges();
    GccBridge::initBridges();
    NetworkBridge::initBridges();
    IpcBridge::initBridges();
    SegaApiBridge::initBridges();
    Alsa2SdlBridge::initBridges();
    RegexBridge::initBridges();
    MathBridge::initBridges();
    // Last, so its popen/system win over the libc bridge's (see rtBridge.cpp).
    RtBridge::initBridges();
}

int main(int argc, char *argv[], char *envp[])
{
    logSetMinLevel(LOG_DEBUG);

    char command[MAX_PATH_LENGTH] = {0};
    char originalDir[MAX_PATH_LENGTH] = {0};
    char gameELF[MAX_PATH_LENGTH] = {0};
    char libraryPath[MAX_PATH_LENGTH] = {0};
    char gamePath[MAX_PATH_LENGTH] = {0};

    log_info("Parsing arguments...\n");
    if (parseArgs(argc, argv, command, originalDir, gameELF, libraryPath) != PARSE_ARGS_SUCCESS)
        return EXIT_SUCCESS;

    char elfPath[MAX_PATH_LENGTH];
    char elfArgs[MAX_PATH_LENGTH] = "";

    char *spacePos = strchr(gameELF, ' ');
    if (spacePos != NULL)
    {
        size_t elfPathLen = spacePos - gameELF;
        strncpy(elfPath, gameELF, elfPathLen);
        elfPath[elfPathLen] = '\0';
        strcpy(elfArgs, spacePos + 1);
    }
    else
    {
        strcpy(elfPath, gameELF);
    }

    // Raw Thrills dumps that the loader cannot run as is (MotoGP's rebuilt
    // ELF: see rawthrills/rtLaunch.c) load from a patched copy, in the
    // loader's cache directory; their libraries stay in the original game
    // directory, which the search paths below must keep pointing to.
    char originalElfPath[MAX_PATH_LENGTH];
    strncpy(originalElfPath, elfPath, sizeof(originalElfPath) - 1);
    originalElfPath[sizeof(originalElfPath) - 1] = '\0';
    char patchedElfPath[MAX_PATH_LENGTH] = "";
    if (rtPrepareElfCopy(elfPath, patchedElfPath, sizeof(patchedElfPath), elfCrc) == 0)
        strncpy(elfPath, patchedElfPath, sizeof(elfPath) - 1);

    ElfLoader::PreReserveAddressSpace(elfPath);

    log_info("Initializing library search paths...\n");
    // The game's directory, as the path of its executable (the search
    // paths are the directory's, and its lib\ subdirectory's; the game
    // runs from its directory, so its executable's absolute path is the
    // current directory's).
    GetFullPathNameA(originalElfPath, MAX_PATH_LENGTH, gamePath, NULL);
    SymbolResolver::GetInstance().InitSearchPaths(libraryPath, gamePath);

    // Install Exception Handler (BEFORE any C++ code runs)
    if (!AddVectoredExceptionHandler(1, myVectoredHandler))
        log_error("Failed to register MyVectoredHandler");

    log_info("Initializing bridges...\n");
    initBridges();

    if (!std::filesystem::exists("tmp"))
        std::filesystem::create_directory("tmp");
    if (!std::filesystem::exists("tmp\\segaboot"))
        std::filesystem::create_directory("tmp\\segaboot");

    log_info("Loading ELF file: %s\n", elfPath);
    ElfLoader loader;
    g_ElfMain = &loader;

    if (!loader.Load(elfPath))
    {
        log_fatal("Failed to load ELF file: %s", elfPath);
        return 1;
    }

    uint8_t *baseAddr = (uint8_t *)loader.GetBaseAddress();
    if (baseAddr)
        partialElfCrc = getCrc32Mem(baseAddr + 10, 0x4000);

    initConfig(configPath);

    if (partialElfCrc == PRIMEVAL_HUNT_SBPP && getConfig()->phUseZink)
    {
        SymbolResolver::GetInstance().AddDllSearchSubPath("zink");
        SetEnvironmentVariableA("GALLIUM_DRIVER", "zink");
        std::string zinkGlPath = SymbolResolver::GetInstance().GetDllSearchBase() + "\\zink\\opengl32.dll";
        hGlDLL = LoadLibraryA(zinkGlPath.c_str());
    }
    else
    {
        hGlDLL = LoadLibraryA("opengl32.dll");
    }

    if (!hGlDLL)
    {
        printf("Failed to load OpenGL!\n");
        return -1;
    }

    log_debug("Initializing main...");
    initMain(configPath, controlsPath);

    // The Raw Thrills games taking their video mode on the command line
    // (sizeArgument, as rtPrepareCommand gives it on Linux): the window's.
    char sizeArgument[64];
    if (rtSizeArgument(elfCrc, getConfig()->width, getConfig()->height, sizeArgument, sizeof(sizeArgument)) &&
        strlen(elfArgs) + strlen(sizeArgument) + 2 < sizeof(elfArgs))
    {
        if (elfArgs[0])
            strcat(elfArgs, " ");
        strcat(elfArgs, sizeArgument);
    }

    // Raw Thrills main threads want the room the Linux launcher gives them
    // (64 MB, see rawthrills/rtLaunch.c): the 8 MB default is not enough
    // for all of them.
    if (rtCurrentGame())
        ElfLoader::SetMainStackSize(64u << 20);

    int final_argc = 0;
    char *final_argv_arr[10];

    final_argv_arr[final_argc++] = elfPath;

    if (strlen(elfArgs) > 0)
    {
        char *token = strtok(elfArgs, " ");
        while (token != NULL && final_argc < 9)
        {
            final_argv_arr[final_argc++] = token;
            token = strtok(NULL, " ");
        }
    }
    final_argv_arr[final_argc] = NULL;

    if (!loader.Execute(final_argc, final_argv_arr, envp))
    {
        log_error("Failed to execute ELF file.");
        return 1;
    }

    log_info("ELF Execution Finished");
    return 0;
}

// The game's GL library (opengl32.dll, or its stand-in), for the code that
// resolves the GL entry points as the games get them (rawthrills/rtFrame.c).
extern "C" HMODULE rtGlDll(void)
{
    return hGlDLL;
}

#endif