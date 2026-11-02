// Roxor games (see roxor.h): their cabinet's paths and the start-up they
// need.

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glad/gl.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#ifndef PATH_MAX
#define PATH_MAX 260
#endif
#else
#include <dlfcn.h>
#include <unistd.h>

#include "../common/importHook.h"
#include "../namco/namcoEs1.h"
#endif

#include "roxor.h"
#include "../common/alignedCall.h"
#include "../common/elfImports.h"
#include "../config/config.h"
#include "../log/log.h"
#include "../elfLoader/glHooks.hpp"

extern uint32_t gId;

static char gameDir[PATH_MAX];

const RoxorGame *roxorCurrentGame(void)
{
    static uint32_t cachedId;
    static const RoxorGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = roxorGetGame(gId);
    }
    return game;
}

int isRoxorGame(void)
{
    return roxorCurrentGame() != NULL;
}

const char *roxorRedirectPath(const char *path, char *buf, size_t size)
{
    const RoxorGame *g = roxorCurrentGame();
    const char *settings;
    size_t n;

    if (!g || !path || !gameDir[0])
        return path;
    n = strlen(g->rootPath);
    if (!strncmp(path, g->rootPath, n) && (path[n] == '/' || path[n] == '\0'))
    {
        snprintf(buf, size, "%s%s", gameDir, path + n);
        return buf;
    }
    // $HOME/<settings>[/...], wherever HOME is.
    n = strlen(g->settings);
    for (settings = strstr(path, g->settings); settings; settings = strstr(settings + 1, g->settings))
        if (settings > path && settings[-1] == '/' && (settings[n] == '/' || settings[n] == '\0'))
        {
            snprintf(buf, size, "%s/%s%s", gameDir, g->saveDir, settings + n);
            return buf;
        }
    return path;
}

// Emit path as a double-quoted Tcl string. Backslashes are normalized to
// forward slashes (the Windows C runtime opens "/" paths too) so the value
// cannot be read as a Tcl escape, and the remaining in-quote specials
// ($, `, ") are backslash-escaped.
static void tclPutQuotedPath(FILE *out, const char *path)
{
    fputc('"', out);
    for (; *path; path++)
    {
        char c = *path;
        if (c == '\\')
            c = '/';
        if (c == '$' || c == '`' || c == '"')
            fputc('\\', out);
        fputc(c, out);
    }
    fputc('"', out);
}

// The options file (Tcl, "set <name> <value>" lines): data_dir is the
// cabinet's data directory (the dumps' is TeknoParrot's Windows one), and
// the game is fullscreen (sdl12-compat scales its 640x480 to the screen) as
// the loader's config says, or when it asks for a picture larger than the
// game's (Batocera's: the screen's size, not fullscreen).
static void setOptions(const RoxorGame *g)
{
    char path[PATH_MAX], tmp[PATH_MAX + 8], line[1024];
    int fullscreen = getConfig()->fullscreen || getConfig()->width > 640 || getConfig()->height > 480;
    FILE *in, *out;

    snprintf(path, sizeof(path), "%s/%s/options", gameDir, g->saveDir);
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    if (!(in = fopen(path, "r")))
        return;
    if (!(out = fopen(tmp, "w")))
    {
        fclose(in);
        return;
    }
    while (fgets(line, sizeof(line), in))
    {
        if (!strncmp(line, "set data_dir ", 13))
        {
            char dataPath[PATH_MAX];
#if defined(_WIN32)
            // The game's SDL is a Windows DLL: its data files' opens go
            // through the Windows C runtime, where the loader's
            // redirections do not reach, so the directory's real path.
            snprintf(dataPath, sizeof(dataPath), "%s/%s", gameDir, g->dataDir);
#else
            snprintf(dataPath, sizeof(dataPath), "%s/%s", g->rootPath, g->dataDir);
#endif
            fputs("set data_dir ", out);
            tclPutQuotedPath(out, dataPath);
            fputc('\n', out);
        }
        else if (!strncmp(line, "set fullscreen ", 15))
            fprintf(out, "set fullscreen %s\n", fullscreen ? "true" : "false");
        else
            fputs(line, out);
    }
    fclose(in);
    if (fclose(out) != 0)
    {
        unlink(tmp);
        return;
    }
#if defined(_WIN32)
    // path already exists (it was opened for reading): the C runtime's
    // rename() refuses to replace an existing destination, so overwrite it
    // explicitly, and check the result.
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING))
        log_error("Roxor: cannot replace the options file: %lu", GetLastError());
#else
    if (rename(tmp, path) != 0)
        unlink(tmp);
#endif
}

// The game links libGL: its GL calls (and libGLU's) reach the loader's GL
// wrappers, which call through glad's entry points. The Lindbergh path loads
// them once its window's context is current; this game's is SDL's, made by
// SDL_SetVideoMode: loaded right after it.
static void *(*realSetVideoMode)(int, int, int, unsigned int);

#ifdef _WIN32
// From the system's OpenGL: wglGetProcAddress, with the system's
// opengl32's entry points as the fallback (which alone has the core's).
static void *glGetProcAddressRoxor(const char *name)
{
    static HMODULE hGl = NULL;
    // wglGetProcAddress is WINAPI (stdcall): match its prototype.
    static void *WINAPI (*wglGetProcAddressPC)(const char *) = NULL;
    void *p;

    if (!hGl)
    {
        hGl = LoadLibraryA("opengl32.dll");
        if (!hGl)
            return NULL;
        *(void **)&wglGetProcAddressPC = (void *WINAPI (*)(const char *))GetProcAddress(hGl, "wglGetProcAddress");
    }
    p = wglGetProcAddressPC ? wglGetProcAddressPC(name) : NULL;
    if (!p)
        p = (void *)GetProcAddress(hGl, name);
    return p;
}
#endif

__attribute__((force_align_arg_pointer)) static void *setVideoMode(int width, int height, int bpp, unsigned int flags)
{
    static int glLoaded;
    void *surface;

    log_info("Roxor: SDL_SetVideoMode(%d, %d, %d, 0x%x)", width, height, bpp, flags);
    surface = realSetVideoMode(width, height, bpp, flags);
    log_info("Roxor: SDL_SetVideoMode returned %p", surface);

    if (surface && !glLoaded)
    {
        GLADloadfunc getProcAddress;
#ifdef _WIN32
        getProcAddress = (GLADloadfunc)glGetProcAddressRoxor;
#else
        getProcAddress = (GLADloadfunc)dlsym(RTLD_NEXT, "glXGetProcAddressARB");
#endif
        glLoaded = 1;
        if (!getProcAddress || !gladLoadGL(getProcAddress))
            log_error("Roxor: cannot load the GL entry points");
    }
    return surface;
}

#ifdef _WIN32
// The boot trace, only with ROXOR_TRACE set.
static void roxorTrace(char c)
{
    static int enabled = -1;

    if (enabled < 0)
        enabled = getenv("ROXOR_TRACE") != 0;
    if (!enabled)
        return;
    FILE *f = fopen("roxor_trace.log", "a");
    if (f)
    {
        fputc(c, f);
        fclose(f);
    }
}

// Temporary (diagnostic): stand between the game and the first Tcl call.
static void *(*realTclCreateInterp)(void);
__attribute__((force_align_arg_pointer)) static void *tclCreateInterp(void)
{
    roxorTrace('t');
    void *r = realTclCreateInterp();
    roxorTrace('u');
    return r;
}

// The game's imports are resolved when the loader loads it (its GOT is
// written then, see elfLoader.cpp): the loader recorded them (see
// common/elfImports.h) and stands between the game's calls and their
// targets here, on the slots that hold the resolved addresses.
//
// Its GL's (the loader's own is never loaded: the game's window and
// context are its SDL's, made by SDL_SetVideoMode) is the system's,
// before the aligning stubs' (the stub's copies the slot's target's,
// see alignImports'): the core's (opengl32.dll's), the extensions'
// (wglGetProcAddress's).
static void hookSetVideoMode(void)
{
    const char *names[512];
    uint32_t *slots[512];
    int count = loaderImportSlots(names, slots, 512);

    for (int i = 0; i < count; i++)
    {
        if (!strcmp(names[i], "SDL_SetVideoMode"))
        {
            realSetVideoMode = (void *(*)(int, int, int, unsigned int))(uintptr_t)*slots[i];
            *slots[i] = (uint32_t)(uintptr_t)setVideoMode;
        }
        // Temporary (diagnostic): stand between the game and the first
        // Tcl call.
        else if (!realTclCreateInterp && !strcmp(names[i], "Tcl_CreateInterp"))
        {
            realTclCreateInterp = (void *(*)(void))(uintptr_t)*slots[i];
            *slots[i] = (uint32_t)(uintptr_t)tclCreateInterp;
        }
        else if (names[i][0] == 'g' && names[i][1] == 'l' && names[i][2] != 'u')
        {
            // The games call GL cdecl, the system opengl32 is stdcall: the
            // loader's pure-GL passthroughs (the __cdecl front onto glad,
            // loaded once SDL_SetVideoMode has made a context) match, the
            // raw entry points only when there is no adapter.
            void *gl = GLHooks_RealGlProc(names[i]);
            if (!gl)
                gl = glGetProcAddressRoxor(names[i]);
            if (gl)
                *slots[i] = (uint32_t)(uintptr_t)gl;
        }
    }
    if (!realSetVideoMode)
        log_warn("Roxor: no SDL_SetVideoMode import, GL is not loaded");
}

// Built by gcc 2.95, for a 4-byte-aligned stack: the host's libraries
// fault on it (libvorbisfile's SSE moves in the music's Mix_LoadMUS). Its
// imports go through stubs aligning it (see common/alignedCall.c), after
// the hook above.
static void alignImports(void)
{
    const char *names[512];
    uint32_t *slots[512];
    int count = loaderImportSlots(names, slots, 512);

    for (int i = 0; i < count; i++)
    {
        void *stub;
        if (alignedCallDirect(names[i]))
            continue;
        stub = alignedCallStub((uintptr_t)*slots[i]);
        if (!stub)
        {
            log_error("Roxor: no memory for the import stubs");
            return;
        }
        *slots[i] = (uint32_t)(uintptr_t)stub;
    }
}
#endif

int roxorInit(void)
{
    const RoxorGame *g = roxorCurrentGame();
    char data[PATH_MAX];

#ifdef _WIN32
    if (!_getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
#else
    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
#endif
    setOptions(g);
    // The game runs from its data directory (its Tcl scripts are "./").
    snprintf(data, sizeof(data), "%s/%s", gameDir, g->dataDir);
#ifdef _WIN32
    if (_chdir(data) != 0)
#else
    if (chdir(data) != 0)
#endif
        fprintf(stderr, "Roxor: cannot enter %s\n", data);
#ifdef _WIN32
    hookSetVideoMode();
    roxorIoInstall(g);
    if (!getenv("ROXOR_NOALIGN"))
        alignImports();
#else
    *(void **)&realSetVideoMode = hookExecutableImport("SDL_SetVideoMode", (void *)setVideoMode);
    if (!realSetVideoMode)
        log_warn("Roxor: no SDL_SetVideoMode import, GL is not loaded");
    roxorIoInstall(g);
    // Built by gcc 2.95, for a 4-byte-aligned stack: the host's libraries
    // fault on it (libvorbisfile's SSE moves in the music's Mix_LoadMUS). Its
    // imports go through stubs aligning it, as Dead Heat's (see
    // namco/namcoEs1Align.c); after the hooks on them above.
    if (!getenv("ROXOR_NOALIGN"))
        namcoEs1AlignImports();
#endif
    return 0;
}
