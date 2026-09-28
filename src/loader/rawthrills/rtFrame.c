// Video mode and frame hook for Raw Thrills games (kept apart from rawthrills.c, whose GL
// shader wrapper conflicts with glad's gl.h macros).

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <glad/gl.h>

#include "rawthrills.h"
#include "../config/config.h"
#include "../graphics/border.h"
#include "../log/log.h"

// Frame hook: the games present frames with SDL 1.2's SDL_GL_SwapBuffers.
// Draw the light gun border ([Display] BORDER_ENABLED) there, as the loader
// does in its GLX swap hook for Lindbergh games.

void SDL_GL_SwapBuffers(void)
{
    static void (*real)(void);
    static int *(*getVideoSurface)(void);
    static int glLoaded;

    if (!real)
    {
        real = dlsym(RTLD_NEXT, "SDL_GL_SwapBuffers");
        getVideoSurface = dlsym(RTLD_DEFAULT, "SDL_GetVideoSurface");
    }

    if (isRawThrillsGame() && getConfig()->borderEnabled)
    {
        // The GL entry points are loaded once a context is current.
        if (!glLoaded)
        {
            GLADloadfunc getProcAddress = (GLADloadfunc)dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
            glLoaded = getProcAddress && gladLoadGL(getProcAddress) ? 1 : -1;
            if (glLoaded < 0)
                log_warn("Raw Thrills: cannot load GL entry points, border disabled");
        }
        // SDL 1.2 SDL_Surface: flags, format, w, h.
        int *surface = getVideoSurface ? getVideoSurface() : NULL;
        if (glLoaded > 0 && surface && surface[2] > 0 && surface[3] > 0)
            drawGameBorder(surface[2], surface[3], getConfig()->whiteBorderPercentage,
                           getConfig()->blackBorderPercentage);
    }
    if (real)
        real();
}

// Video mode: the games render at the cabinet resolution (g5 engine: gCLArgs,
// "-f<w>x<h>" on the command line), set up by their command-line parser. Use
// [Display] WIDTH/HEIGHT instead, which default to that same resolution, and
// [Display] ROTATE_VERTICAL for vertical games.

static void (*parseArgsOrig)(int, char **);
static const RtGame *videoGame;
static void (*orthoOrig)(int w, int h);
static void (*realViewport)(int x, int y, int w, int h);

// The game draws in its monitor's pixels, upright or rotated: keep that
// projection and fit it, centred, in the window.
static void setOrtho(int w, int h)
{
    const int32_t *monitor = (const int32_t *)(uintptr_t)videoGame->monitorSize;
    int rotated = videoGame->rotateFlag && *(const int32_t *)(uintptr_t)videoGame->rotateFlag;
    int gw = rotated ? monitor[0] : monitor[1], gh = rotated ? monitor[1] : monitor[0];
    int vw = w, vh = h;

    orthoOrig(gw, gh);
    if ((double)w * gh > (double)h * gw)
        vw = (int)((double)h * gw / gh);
    else
        vh = (int)((double)w * gh / gw);
    realViewport((w - vw) / 2, (h - vh) / 2, vw, vh);
}

static void parseCommandLineArgs(int argc, char **argv)
{
    int width = getConfig()->width, height = getConfig()->height;
    int32_t *mode = (int32_t *)videoGame->resolution;

    parseArgsOrig(argc, argv);
    if (width <= 0 || height <= 0)
        return;
    mode[0] = width;
    mode[1] = height;
    if (videoGame->aspect)
        *(float *)(uintptr_t)videoGame->aspect = (float)width / height;
    if (videoGame->rotateFlag)
        *(int32_t *)(uintptr_t)videoGame->rotateFlag = getConfig()->rotateVertical != 0;
    printf("Raw Thrills: video mode %dx%d%s\n", width, height,
           videoGame->rotateFlag && getConfig()->rotateVertical ? ", rotated" : "");
}

// g3 engine: mode table entry and glut window.

typedef struct
{
    uint32_t flags;
    uint16_t width;
    uint16_t height;
} RtModeEntry;

static int (*setModeOrig)(int mode);
static int (*windowOpenOrig)(int width, int height, int fullscreen);

static int setMode(int mode)
{
    int ret = setModeOrig(mode);
    RtModeEntry *entry = *(RtModeEntry **)(uintptr_t)videoGame->modePointer;
    int width = getConfig()->width, height = getConfig()->height;

    if (ret >= 0 && entry && width > 0 && height > 0)
    {
        entry->width = width;
        entry->height = height;
        printf("Raw Thrills: video mode %dx%d\n", width, height);
    }
    return ret;
}

static int windowOpen(int width, int height, int fullscreen)
{
    static void (*glutFullScreen)(void);
    int ret = windowOpenOrig(width, height, 0);

    (void)fullscreen;
    if (!glutFullScreen)
        glutFullScreen = dlsym(RTLD_NEXT, "glutFullScreen");
    if (ret >= 0 && getConfig()->fullscreen && glutFullScreen)
        glutFullScreen();
    return ret;
}

static void installG3Video(const RtGame *game)
{
    videoGame = game;
    if (game->setModeSymbol)
    {
        setModeOrig = rtTrampoline(game->setModeSymbol, game->setModePrologue);
        if (!setModeOrig || rtDetour(game->setModeSymbol, setMode) != 0)
            log_warn("Raw Thrills: cannot hook %s, keeping the game's resolution", game->setModeSymbol);
    }
    if (!game->windowOpenSymbol)
        return;
    windowOpenOrig = rtTrampoline(game->windowOpenSymbol, game->windowOpenPrologue);
    if (!windowOpenOrig || rtDetour(game->windowOpenSymbol, windowOpen) != 0)
        log_warn("Raw Thrills: cannot hook %s, the display mode may not be set", game->windowOpenSymbol);
}

void rtInstallVideo(const RtGame *game)
{
    const char *symbol = game->parseArgsSymbol ? game->parseArgsSymbol : "ParseCommandLineArgs";

    if (game->setModeSymbol || game->windowOpenSymbol)
        installG3Video(game);
    if (!game->resolution || !game->parseArgsPrologue)
        return;
    videoGame = game;
    if (game->monitorSize && game->orthoSymbol)
    {
        realViewport = dlsym(RTLD_NEXT, "glViewport");
        orthoOrig = rtTrampoline(game->orthoSymbol, game->orthoPrologue);
        if (realViewport && orthoOrig)
            rtDetour(game->orthoSymbol, setOrtho);
    }
    parseArgsOrig = rtTrampoline(symbol, game->parseArgsPrologue);
    if (!parseArgsOrig || rtDetour(symbol, parseCommandLineArgs) != 0)
        log_warn("Raw Thrills: cannot hook ParseCommandLineArgs, keeping the game's resolution");
}
