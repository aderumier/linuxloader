// SDL 1.2 linked into the game (Big Buck HD Wild, The Walking Dead), on
// Windows.
//
// Its X11 video driver dlopens Xlib and its extensions, which the loader's
// X11 bridge does not have: the window never opens. So its video, window and
// event entry points go to the loader's sdl12-compat (SDL.dll, the one
// Cruis'n Blast's dynamically linked SDL 1.2 gets) instead. Its audio stays
// on the linked-in SDL (SDL_mixer's): SDL_Init, SDL_WasInit, SDL_Quit and
// SDL_GetError serve both. As found in douggle/linuxloader-windows.

#include <stdint.h>

#include "rawthrills.h"
#include "../log/log.h"

#ifdef _WIN32

#include <windows.h>

void bridgeLoadNeededLibrary(const char *filename);
void *bridgeGetLoadedLibrary(const char *dllName);

#define SDL_INIT_TIMER 0x1
#define SDL_INIT_AUDIO 0x10
#define SDL_INIT_NOPARACHUTE 0x100000
// What the linked-in SDL keeps: its audio, and the timer SDL_mixer uses.
#define SDL_LINKED_FLAGS (SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_NOPARACHUTE)

// Sent to SDL.dll as they are: they only concern the video, the window and
// its events. The swap goes through the frame hook (rtSdlSwapBuffers).
static const char *const compatNames[] = {
    "SDL_SetVideoMode",    "SDL_PollEvent",     "SDL_EnableKeyRepeat", "SDL_GetVideoSurface",
    "SDL_GetVideoInfo",    "SDL_WM_SetCaption", "SDL_GL_SetAttribute", NULL,
};

static HMODULE compat;
static int (*compatInit)(uint32_t);
static uint32_t (*compatWasInit)(uint32_t);
static void (*compatQuit)(void);
static const char *(*compatGetError)(void);
static int (*linkedInit)(uint32_t);
static uint32_t (*linkedWasInit)(uint32_t);
static void (*linkedQuit)(void);
static const char *(*linkedGetError)(void);
static void *(*compatGetVideoSurface)(void);
static void (*compatFreeSurface)(void *);
static void (*linkedFreeSurface)(void *);
static int (*sdl2ShowCursor)(int);

static int sdlInit(uint32_t flags)
{
    uint32_t linked = flags & SDL_LINKED_FLAGS, rest = flags & ~(uint32_t)SDL_INIT_AUDIO;
    int ret;

    // The linked-in audio asking for itself: only the linked-in SDL.
    if (!(rest & ~(uint32_t)(SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE)))
        return linkedInit(linked);
    if (linked & (SDL_INIT_TIMER | SDL_INIT_AUDIO) && linkedInit(linked) < 0)
        log_warn("Raw Thrills: the game's SDL audio did not start: %s", linkedGetError());
    ret = compatInit(rest);
    if (ret < 0)
        log_error("Raw Thrills: SDL.dll SDL_Init(%#x) failed: %s", rest, compatGetError());
    return ret;
}

static uint32_t sdlWasInit(uint32_t flags)
{
    return compatWasInit(flags) | linkedWasInit(flags);
}

static void sdlQuit(void)
{
    compatQuit();
    linkedQuit();
}

// SDL.dll's error, else the linked-in SDL's (its audio's).
static const char *sdlGetError(void)
{
    const char *error = compatGetError();
    return error && error[0] ? error : linkedGetError();
}

// No X11 display or window to hand out: the game then leaves out the GLX
// extension checks it makes with them.
static int sdlGetWMInfo(void *info)
{
    (void)info;
    return 0;
}

static void *compatSymbol(const char *name);

// SDL2's, under SDL.dll: sdl12-compat's own makes a hidden cursor with the
// input grabbed (a full screen window) SDL2's relative mouse mode, which
// pins the cursor the guns are read from (desktopInput.c).
static int sdlShowCursor(int toggle)
{
    static int (*compatShowCursor)(int);

    if (!sdl2ShowCursor)
        *(void **)&sdl2ShowCursor = (void *)GetProcAddress(GetModuleHandleA("SDL2.dll"), "SDL_ShowCursor");
    if (sdl2ShowCursor)
        return sdl2ShowCursor(toggle);
    if (!compatShowCursor)
        *(void **)&compatShowCursor = compatSymbol("SDL_ShowCursor");
    return compatShowCursor ? compatShowCursor(toggle) : 0;
}

// SDL.dll's video surface to SDL.dll, the surfaces of the game's own SDL
// (its images) to that SDL.
static void sdlFreeSurface(void *surface)
{
    if (surface && surface == compatGetVideoSurface())
        compatFreeSurface(surface);
    else
        linkedFreeSurface(surface);
}

static void *compatSymbol(const char *name)
{
    void *p = (void *)GetProcAddress(compat, name);
    if (!p)
        log_error("Raw Thrills: SDL.dll has no %s", name);
    return p;
}

void rtInstallStaticSdl(const RtGame *game)
{
    if (!game->staticSdl)
        return;
    bridgeLoadNeededLibrary("libSDL-1.2.so.0");
    if (!(compat = (HMODULE)bridgeGetLoadedLibrary("SDL.dll")))
    {
        log_error("Raw Thrills: no SDL.dll (sdl12-compat) in ll-deps: the game's SDL cannot open a window");
        return;
    }
    if (!(*(void **)&compatInit = compatSymbol("SDL_Init")) ||
        !(*(void **)&compatWasInit = compatSymbol("SDL_WasInit")) ||
        !(*(void **)&compatQuit = compatSymbol("SDL_Quit")) ||
        !(*(void **)&compatGetError = compatSymbol("SDL_GetError")) ||
        !(*(void **)&compatGetVideoSurface = compatSymbol("SDL_GetVideoSurface")) ||
        !(*(void **)&compatFreeSurface = compatSymbol("SDL_FreeSurface")))
    {
        compat = NULL;
        return;
    }
    *(void **)&linkedInit = rtTrampoline("SDL_Init", game->staticSdlPrologues[0]);
    *(void **)&linkedWasInit = rtTrampoline("SDL_WasInit", game->staticSdlPrologues[1]);
    *(void **)&linkedQuit = rtTrampoline("SDL_Quit", game->staticSdlPrologues[2]);
    *(void **)&linkedGetError = rtTrampoline("SDL_GetError", game->staticSdlPrologues[3]);
    *(void **)&linkedFreeSurface = rtTrampoline("SDL_FreeSurface", game->staticSdlPrologues[4]);
    if (!linkedInit || !linkedWasInit || !linkedQuit || !linkedGetError || !linkedFreeSurface)
    {
        log_error("Raw Thrills: the game's SDL entry points are not listed");
        compat = NULL;
        return;
    }
    rtDetour("SDL_Init", (void *)sdlInit);
    rtDetour("SDL_WasInit", (void *)sdlWasInit);
    rtDetour("SDL_Quit", (void *)sdlQuit);
    rtDetour("SDL_GetError", (void *)sdlGetError);
    rtDetour("SDL_GetWMInfo", (void *)sdlGetWMInfo);
    rtDetour("SDL_GL_SwapBuffers", (void *)rtSdlSwapBuffers);
    rtDetour("SDL_FreeSurface", (void *)sdlFreeSurface);
    rtDetour("SDL_ShowCursor", (void *)sdlShowCursor);
    for (const char *const *name = compatNames; *name; name++)
    {
        void *p = compatSymbol(*name);
        if (p)
            rtDetour(*name, p);
    }
    log_info("Raw Thrills: the game's SDL 1.2 runs on SDL.dll");
}

void *rtStaticSdlTarget(const char *name)
{
    return compat && name ? (void *)GetProcAddress(compat, name) : NULL;
}

#else

void rtInstallStaticSdl(const RtGame *game)
{
    (void)game;
}

void *rtStaticSdlTarget(const char *name)
{
    (void)name;
    return NULL;
}

#endif
