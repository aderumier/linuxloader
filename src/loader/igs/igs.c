// IGS games (see igs.h): their cabinet's paths, the BIOS check and the
// start-up they need.

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <glad/gl.h>

#include "igs.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../log/log.h"
#include "../../minhook/include/MinHook.h"
#include "../patching/flowControl.h"

extern uint32_t gId;

static char gameDir[PATH_MAX];
static const IgsGame *current;

const IgsGame *igsCurrentGame(void)
{
    static uint32_t cachedId;
    static const IgsGame *game;

    if (cachedId != gId)
    {
        cachedId = gId;
        game = igsGetGame(gId);
    }
    return game;
}

int isIgsGame(void)
{
    return igsCurrentGame() != NULL;
}

static int underDir(const char *path, const char *dir)
{
    size_t n = strlen(dir);

    return !strncmp(path, dir, n) && (path[n] == '/' || path[n] == '\0');
}

const char *igsRedirectPath(const char *path, char *buf, size_t size)
{
    const IgsGame *g = igsCurrentGame();

    if (!g || !path || !gameDir[0])
        return path;
    // The BIOS reader and the security device: there, never read (the BIOS
    // check is the loader's, the device only opened). The OSS mixer is
    // common/ossDsp.c's.
    if (!strcmp(path, "/dev/roio") || !strcmp(path, "/dev/pccard0"))
        return "/dev/null";
    if (underDir(path, g->rootPath))
    {
        snprintf(buf, size, "%s%s", gameDir, path + strlen(g->rootPath));
        return buf;
    }
    for (const char *const *dir = g->dataDirs; *dir; dir++)
        if (underDir(path, *dir))
        {
            snprintf(buf, size, "%s%s", gameDir, path);
            return buf;
        }
    return path;
}

// The BIOS check reads the motherboard's BIOS through /dev/roio and finds it
// in its table of the cabinets' boards: it is the one the descriptor names.
__attribute__((force_align_arg_pointer)) static int biosCheck(void *table, int board)
{
    (void)table;
    (void)board;
    return current->biosBoard;
}

// The game asks for full screen at its size (frameWidth x frameHeight, its
// cabinet's monitor): SDL 1.2 would switch the X server to it, which today's
// drivers refuse. It gets a window of that size, or, with [Display]
// FULLSCREEN or a larger configured size, full screen at the screen's
// ([Display] WIDTH x HEIGHT when larger than the game's, else the X
// screen's), the game's frame scaled to it at each swap
// (graphics/frameScale.h).
#define SDL12_FULLSCREEN 0x80000000u

static void *(*realSetVideoMode)(int, int, int, unsigned int);
static int screenWidth, screenHeight; // scaled to: 0, none

__attribute__((force_align_arg_pointer)) static void *setVideoMode(int width, int height, int bpp, unsigned int flags)
{
    EmulatorConfig *config = getConfig();
    int larger = config->width > width || config->height > height;
    int w = config->width, h = config->height;

    if (!config->fullscreen && !larger)
        return realSetVideoMode(width, height, bpp, flags & ~SDL12_FULLSCREEN);
    if (!larger && !igsScreenSize(&w, &h))
    {
        w = width;
        h = height;
    }
    if (w != width || h != height)
    {
        frameScaleSetFrame(current->frameWidth, current->frameHeight, NULL);
        screenWidth = w;
        screenHeight = h;
    }
    printf("IGS: %dx%d asked, full screen at %dx%d\n", width, height, w, h);
    return realSetVideoMode(w, h, bpp, flags | SDL12_FULLSCREEN);
}

// The game's SDL is static: it loads libGL itself and takes glXMakeCurrent
// from it with dlsym, past the loader's. The loader's GL wrappers call
// through glad's entry points, loaded once the context is current.
static void *(*realDlsym)(void *, const char *);
static int (*realMakeCurrent)(void *, unsigned long, void *);

__attribute__((force_align_arg_pointer)) static int makeCurrent(void *dpy, unsigned long drawable, void *ctx)
{
    static int glLoaded;
    int result = realMakeCurrent(dpy, drawable, ctx);

    if (result && ctx && !glLoaded)
    {
        GLADloadfunc getProcAddress = (GLADloadfunc)realDlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
        glLoaded = 1;
        if (!getProcAddress || !gladLoadGL(getProcAddress))
            log_error("IGS: cannot load the GL entry points");
    }
    return result;
}

// The game's SDL carries its own copies of Xlib extensions (VidMode, DGA,
// Xinerama), built without Xlib's thread locks: they call _XReply with the
// display unlocked. Today's libX11 always locks (XInitThreads is implicit),
// and _XReply's unlock and relock around its wait leaves the display locked
// for good: the next Xlib call (SDL's XCreateColormap) waits forever. The game
// draws from its main thread only: its display goes unlocked, as in its day.
static void *(*realXOpenDisplay)(const char *);

__attribute__((force_align_arg_pointer)) static void *gameXOpenDisplay(const char *name)
{
    void *dpy = realXOpenDisplay(name);

    if (dpy)
        igsUnlockDisplay(dpy);
    return dpy;
}

// IGS_SHOT=<directory> (debugging): a frame every 4 s (IGS_SHOT_PERIOD, in
// seconds, fractions too) from the first one on (IGS_SHOT_FROM seconds later)
// saved there as a PPM,
// read back before the swap, scaled (a capture of the window comes out black under
// gamescope; Xvfb's GLX refuses the game's SDL).
static void (*realSwapBuffers)(void *, unsigned long);
static unsigned int frames;

unsigned int igsFrames(void)
{
    return frames;
}

__attribute__((force_align_arg_pointer)) static void swapBuffers(void *dpy, unsigned long drawable)
{
    static double next = -1;
    static int count;
    const char *dir = getenv("IGS_SHOT");
    struct timespec ts;
    double now;
    GLint viewport[4];

    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = ts.tv_sec + ts.tv_nsec / 1e9;
    if (next < 0)
        next = now + (getenv("IGS_SHOT_FROM") ? atof(getenv("IGS_SHOT_FROM")) : 0);
    if (screenWidth)
        frameScalePresent(0, 0, screenWidth, screenHeight, getConfig()->borderEnabled,
                          getConfig()->whiteBorderPercentage, getConfig()->blackBorderPercentage);
    if (dir && now >= next && glad_glReadPixels)
    {
        char path[PATH_MAX];
        unsigned char *pixels;
        FILE *f;

        next = now + (getenv("IGS_SHOT_PERIOD") ? atof(getenv("IGS_SHOT_PERIOD")) : 4);
        glad_glGetIntegerv(GL_VIEWPORT, viewport);
        if (screenWidth)
        {
            viewport[0] = viewport[1] = 0;
            viewport[2] = screenWidth;
            viewport[3] = screenHeight;
        }
        snprintf(path, sizeof(path), "%s/igs_%04d.ppm", dir, count++);
        pixels = malloc((size_t)viewport[2] * viewport[3] * 3);
        if (pixels && (f = fopen(path, "wb")))
        {
            GLint read = 0;

            // The window's own (the scaling keeps the game's frame bound).
            glad_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glad_glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glad_glReadBuffer(GL_BACK);
            glad_glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3], GL_RGB, GL_UNSIGNED_BYTE, pixels);
            glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
            fprintf(f, "P6\n%d %d\n255\n", viewport[2], viewport[3]);
            for (int y = viewport[3]; y-- > 0;)
                fwrite(pixels + (size_t)y * viewport[2] * 3, 1, (size_t)viewport[2] * 3, f);
            fclose(f);
        }
        free(pixels);
    }
    realSwapBuffers(dpy, drawable);
    frames++;
}

__attribute__((force_align_arg_pointer)) static void *gameDlsym(void *handle, const char *name)
{
    void *symbol = realDlsym(handle, name);

    if (symbol && name && !strcmp(name, "glXMakeCurrent"))
    {
        *(void **)&realMakeCurrent = symbol;
        return (void *)makeCurrent;
    }
    if (symbol && name && !strcmp(name, "glXSwapBuffers"))
    {
        *(void **)&realSwapBuffers = symbol;
        return (void *)swapBuffers;
    }
    return symbol;
}

// The frame scaling's GL entry points in place of the game's imports; they
// change nothing until setVideoMode asks for scaling.
static void *glProcAddress(const char *name)
{
    static void *(*get)(const char *);

    if (!get)
        *(void **)&get = dlsym(RTLD_DEFAULT, "glXGetProcAddressARB");
    return get ? get(name) : NULL;
}

static void frameScaleInstall(void)
{
    static const char *const wrapped[] = {"glViewport", "glDrawBuffer", "glReadBuffer", "glGetIntegerv"};

    frameScaleInit(glProcAddress, getConfig()->keepAspectRatio);
    for (size_t i = 0; i < sizeof(wrapped) / sizeof(wrapped[0]); i++)
    {
        void *wrapper = frameScaleWrapper(wrapped[i]);
        if (wrapper)
            hookExecutableImport(wrapped[i], wrapper);
    }
}

// The game's keyboard reads: no key pressed.
__attribute__((force_align_arg_pointer)) static int noKey(int key)
{
    (void)key;
    return 0;
}

int igsInit(void)
{
    char path[PATH_MAX];

    current = igsCurrentGame();
    // Started in the game's directory, which the cabinet runs it from too.
    if (!getcwd(gameDir, sizeof(gameDir)))
        gameDir[0] = '\0';
    for (const char *const *dir = current->dataDirs; *dir; dir++)
    {
        snprintf(path, sizeof(path), "%s%s", gameDir, *dir);
        mkdir(path, 0755);
    }

    *(void **)&realDlsym = hookExecutableImport("dlsym", (void *)gameDlsym);
    if (!realDlsym)
        log_warn("IGS: no dlsym import, GL is not loaded");
    *(void **)&realXOpenDisplay = hookExecutableImport("XOpenDisplay", (void *)gameXOpenDisplay);
    if (!realXOpenDisplay)
        log_warn("IGS: no XOpenDisplay import");
    detourFunction(current->biosCheck, biosCheck);
    for (int i = 0; i < 3; i++)
        if (current->keyboardReads[i])
            detourFunction(current->keyboardReads[i], noKey);
    if (MH_CreateHook((void *)(uintptr_t)current->setVideoMode, (void *)setVideoMode, (void **)&realSetVideoMode) != MH_OK)
        log_error("IGS: cannot hook the video mode");
    frameScaleInstall();
    igsPaletteInstall();
    igsIoInstall(current);
    return 0;
}
