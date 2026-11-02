#define _CRT_SECURE_NO_WARNINGS

#include "sdlCalls.h"
#include "x11Bridge.h"
#include "../log/log.h"
#include "../config/config.h"
#ifdef __linux__
#include "../teamplay/teamplay.h"
#include "../namco/namcoN2.h"
#endif

#include <stdlib.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_stdinc.h>


// Windows API
#ifdef   __linux__
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string.h>
#include <unistd.h>
#include <GL/glx.h>
#include <dlfcn.h>
#include <X11/extensions/xf86vmode.h> 
#else 
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int bridgeXInitThreads()
{
    return 1;
}

static char s_dummyDisplay[4096] = {0};
static char s_commandBuffer[8192] = {0};
static char s_dummyScreens[4096] = {0};

Display *bridgeXOpenDisplay(const char *name)
{
    log_debug("XOpenDisplay(\"%s\")\n", name ? name : "NULL");
    *(int *)(s_dummyDisplay + 0x84) = 0;                                           // Offset 0x84: Default Screen (int)
    *(void **)(s_dummyDisplay + 0x8C) = s_dummyScreens;                            // Offset 0x8C: Screens pointer
    *(Window *)(s_dummyScreens + 0x08) = 0;                                        // Offset 0x08 : window ID
    *(int *)(s_dummyScreens + 0x0C) = getConfig()->width;                          // Offset 0x0C: Width
    *(int *)(s_dummyScreens + 0x10) = getConfig()->height;                         // Offset 0x10: Height
    *(char **)(s_dummyDisplay + 0x6C) = s_commandBuffer;                           // Offset 0x6C: Current buffer pointer??
    *(char **)(s_dummyDisplay + 0x70) = s_commandBuffer + sizeof(s_commandBuffer); // Offset 0x70: bufmax??
    return (Display *)&s_dummyDisplay;
}

int bridgeXCloseDisplay(Display *dpy)
{
    return 0;
}

Window bridgeXCreateWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                           unsigned int border_width, int depth, unsigned int class_type, void *visual, unsigned long valuemask,
                           XSetWindowAttributes *attributes)
{
    log_debug("XCreateWindow: %ux%u\n", getConfig()->width, getConfig()->height);

    if (!getSDLWindow())
    {
        startSDL();
    }

    return (Window)getSDLWindow();
}

int bridgeXDestroyWindow(Display *dpy, Window win)
{
    return 1;
}

int bridgeXConfigureWindow(Display *dpy, Window w, unsigned long value_mask, int *changes)
{
    log_debug("XConfigureWindow\n");
    return 1;
}

void bridgeXMapWindow(Display *dpy, Window win)
{
    return;
}

int bridgeXReply(Display *dpy, int *rep, int extra, int discard)
{
    if (rep)
    {
        memset(rep, 0, 32);
        ((char *)rep)[0] = 1; // type = X_Reply

        unsigned short *p = (unsigned short *)((char *)rep + 4); // offset 4 and 6 for version??
        p[0] = 2;                                                // Major
        p[1] = 2;                                                // Minor
    }
    return 1;
}

int bridgeXMoveWindow(Display *dpy, Window win, int x, int y)
{
    return 1;
}

int bridgeXResizeWindow(Display *dpy, Window win, unsigned int width, unsigned int height)
{
    return 1;
}

int bridgeXSync(Display *dpy, int discard)
{
    return 1;
}

int bridgeXFlush(Display *dpy)
{
    return 1;
}

void bridgeXLockDisplay(Display *dpy)
{
}

void bridgeXUnlockDisplay(Display *dpy)
{
}

void bridgeXSetErrorHandler(void *handler)
{
}

int bridgeXGetErrorText(Display *dpy, int code, char *buffer, int length)
{
    if (buffer && length > 0)
        buffer[0] = '\0';
    return 0;
}

int bridgeXDisplayWidth(void *dpy, int screen_number)
{
    return getConfig()->width;
}

int bridgeXDisplayHeight(void *dpy, int screen_number)
{
    return getConfig()->height;
}

int bridgeXDisplayWidthMM(void *dpy, int screen_number)
{
    return (int)((getConfig()->width * 25.4f) / 96.0f);
}

int bridgeXDisplayHeightMM(void *dpy, int screen_number)
{
    return (int)((getConfig()->height * 25.4f) / 96.0f);
}

Atom bridgeXInternAtom(Display *dpy, const char *atom_name, bool create_if_not_exist)
{
    return 1;
}

static int s_dummyExtCodes[2] = {0, 128};
static void *s_dummyExtData[4] = {(void *)1, (void *)1, (void *)s_dummyExtCodes, (void *)1};
void *bridgeXextFindDisplay(Display *dpy)
{
    return s_dummyExtData;
}

int bridgeXPending(Display *dpy)
{
    return 0;
}

void bridgeXNextEvent(Display *dpy, XEvent *event_return)
{
    return;
}

void bridgeXSendEvent(Display *dpy, Window w, bool propagate, long event_mask, XEvent *event_send)
{
    return;
}

XWMHints *bridgeXAllocWMHints()
{
    return (XWMHints *)calloc(1, sizeof(XWMHints));
}

int bridgeXCheckTypedEvent(Display *dpy, int type, XEvent *event_return)
{
    return 0;
}

int bridgeXSelectInput(Display *dpy, Window win, long event_mask)
{
    return 0;
}
int bridgeXSetCloseDownMode(Display *dpy, int close_mode)
{
    return 1;
}

int bridgeXSetInputFocus(Display *dpy, Window win, int revert_to, Time time)
{
    return 1;
}

int bridgeXAutoRepeatOff(Display *dpy)
{
    return 1;
}

int bridgeXKeysymToKeycode(Display *dpy, int keysym)
{
    return 0;
}

int bridgeXTranslateCoordinates(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, int *dest_x_return, int *dest_y_return,
                                Window *child_return)
{
    SDL_Window *srcWin = getSDLWindow();
    SDL_Window *destWin = getSDLWindow();

    int srcWinX = 0;
    int srcWinY = 0;
    int destWinX = 0;
    int destWinY = 0;

    if (srcWin)
        SDL_GetWindowPosition(srcWin, &srcWinX, &srcWinY);
    if (destWin)
        SDL_GetWindowPosition(destWin, &destWinX, &destWinY);

    int globalX = src_x + srcWinX;
    int globalY = src_y + srcWinY;

    if (dest_x_return)
        *dest_x_return = globalX - destWinX;
    if (dest_y_return)
        *dest_y_return = globalY - destWinY;
    if (child_return)
        *child_return = 0;
    return 1;
}

int bridgeXQueryPointer(Display *dpy, Window win, Window *root_return, Window *child_return, int *root_x_return, int *root_y_return,
                        int *win_x_return, int *win_y_return, unsigned int *mask_return)
{
    return 0;
}

void bridgeXWarpPointer(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width, unsigned int src_height,
                        int dest_x, int dest_y)
{
}

void bridgeXGrabPointer(Display *dpy, Window win, int owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                        Window confine_to, Cursor cursor, Time time)
{
}

void bridgeXUngrabPointer(Display *dpy, Time time)
{
}

int bridgeXGrabKeyboard(Display *dpy, Window win, int owner_events, int pointer_mode, int keyboard_mode, Time time)
{
    return 0;
}

int bridgeXUngrabKeyboard(Display *dpy, Time time)
{
    return 0;
}

int bridgeXAutoRepeatOn(Display *dpy)
{
    return 1;
}

int bridgeXGetWindowAttributes(Display *dpy, Window win, XWindowAttributes *attr)
{
    if (!attr)
        return 0;
    memset(attr, 0, sizeof(XWindowAttributes));
    attr->x = 0;
    attr->y = 0;
    attr->width = getConfig()->width;
    attr->height = getConfig()->height;
    attr->depth = 24;
    attr->visual = (void *)1;
    attr->map_state = 2;
    return 1;
}

int bridgeXChangeWindowAttributes(Display *dpy, Window w, unsigned long valuemask, XSetWindowAttributes *attributes)
{
    return 1;
}

int bridgeXGetGeometry(Display *dpy, Drawable d, Window *root_return, int *x_return, int *y_return, unsigned int *width_return,
                       unsigned int *height_return, unsigned int *border_width_return, unsigned int *depth_return)
{
    if (root_return)
        *root_return = 0;
    if (x_return)
        *x_return = 0;
    if (y_return)
        *y_return = 0;
    if (width_return)
        *width_return = getConfig()->width;
    if (height_return)
        *height_return = getConfig()->height;
    if (depth_return)
        *depth_return = 24;
    if (border_width_return)
        *border_width_return = 0;
    return 1;
}

int bridgeXSetStandardProperties(Display *dpy, Window win, const char *window_name, const char *icon_name, Pixmap icon_pixmap, char **argv,
                                 int argc, XSetWindowAttributes *attributes)
{
    return 1;
}

int bridgeXChangeProperty(Display *dpy, Window win, Atom property, Atom type, int format, int mode, const unsigned char *data,
                          int nelements)
{
    return 1;
}

int bridgeXParseColor(Display *dpy, Colormap colormap, const char *spec, int *color_return)
{
    return 1;
}

void bridgeXMapRaised(Display *dpy, Window w)
{
    SDL_Window *sdlWin = getSDLWindow();
    if (sdlWin)
        SDL_RaiseWindow(sdlWin);
}

static char s_dummyScreenResources[1024] = {0};
static char s_dummyCrtcs[1024] = {0};

void *bridgeXRRGetScreenResources(Display *dpy, Window window)
{
    *(void **)(s_dummyScreenResources + 0x0C) = s_dummyCrtcs;
    return s_dummyScreenResources;
}

static char s_dummyCrtcInfo[1024] = {0};

void *bridgeXRRGetCrtcInfo(Display *dpy, int crtc)
{
    *(unsigned int *)(s_dummyCrtcInfo + 0x0C) = getConfig()->width;
    *(unsigned int *)(s_dummyCrtcInfo + 0x10) = getConfig()->height;
    return s_dummyCrtcInfo;
}

int bridgeXLookupString(XKeyEvent *key_event, char *buffer_return, int buffer_size, int *keysym_return, int *status_return)
{
    return 0;
}

int bridgeXStoreName(Display *dpy, Window win, const char *name)
{
    SDL_Window *sdlWin = getSDLWindow();
    if (sdlWin && name)
        SDL_SetWindowTitle(sdlWin, name);
    return 0;
}

int bridgeXSetWMProtocols(Display *dpy, Window win, Atom *protocols, int count)
{
    return 1;
}

int bridgeXSetWMProperties(Display *dpy, Window win, XTextProperty *window_name, XTextProperty *icon_name, char **argv, int argc,
                           XSizeHints *normal_hints, XWMHints *wm_hints, XClassHint *class_hints)
{
    return 1;
}

void bridgeXSetWMNormalHints(Display *dpy, Window w, XSizeHints *hints)
{
}

void bridgeXSetTransientForHint(Display *dpy, Window w, Window transient_for)
{
}

int bridgeXStringListToTextProperty(char **list, int count, XTextProperty *text_prop_return)
{
    if (text_prop_return && count > 0 && list && list[0])
    {
        size_t len = strlen(list[0]);
        text_prop_return->value = (unsigned char *)malloc(len + 1);
        if (text_prop_return->value)
            strcpy((char *)text_prop_return->value, list[0]);
        text_prop_return->encoding = 31;
        text_prop_return->format = 8;
        text_prop_return->nitems = len;
        return 1;
    }
    return 0;
}

void bridgeXFree(void *data)
{
    if (data)
        free(data);
}

Pixmap bridgeXCreatePixmapFromBitmapData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height,
                                         unsigned long fg, unsigned long bg, unsigned int depth)
{
    return 2001;
}

Cursor bridgeXCreatePixmapCursor(Display *dpy, Pixmap source, Pixmap mask, void *foreground_color, void *background_color, unsigned int x,
                                 unsigned int y)
{
    return 1001;
}

int bridgeXFreePixmap(Display *dpy, Pixmap pixmap)
{
    return 1;
}

int bridgeXFreeCursor(Display *dpy, Cursor cursor)
{
    return 1;
}

int bridgeXDefineCursor(Display *dpy, Window w, Cursor c)
{
    // if (getConfig()->crc32 == QUIZ_AXA_SBMS || getConfig()->crc32 == QUIZ_AXA_SBUR_LIVE)
    return 0;
}

void bridgeXUndefineCursor(Display *dpy, Window w)
{
}

Cursor bridgeXCreateFontCursor(Display *dpy, int shape)
{
    return 1001;
}

Pixmap bridgeXCreateBitmapFromData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height)
{
    return 2002;
}

int bridgeXF86VidModeQueryExtension(Display *dpy, int *event_base_return, int *error_base_return)
{
    if (event_base_return)
        *event_base_return = 0;
    if (error_base_return)
        *error_base_return = 0;
    return 1;
}

int bridgeXF86VidModeGetAllModeLines(Display *dpy, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo_return)
{
    if (modecount_return)
        *modecount_return = 1;

    if (modesinfo_return)
    {
        XF86VidModeModeInfo **modes = (XF86VidModeModeInfo **)malloc(sizeof(XF86VidModeModeInfo *));

        modes[0] = (XF86VidModeModeInfo *)malloc(sizeof(XF86VidModeModeInfo));
        memset(modes[0], 0, sizeof(XF86VidModeModeInfo));

        modes[0]->hdisplay = (unsigned short)getConfig()->width;
        modes[0]->vdisplay = (unsigned short)getConfig()->height;
        modes[0]->dotclock = 60000; // Fake 60Hz dotclock
        modes[0]->htotal = modes[0]->hdisplay + 20;
        modes[0]->vtotal = modes[0]->vdisplay + 10;

        *modesinfo_return = modes;
    }
    return 1;
}

int bridgeXF86VidModeGetModeLine(Display *dpy, int screen, int *dotclock_return, void *modeline)
{
    if (dotclock_return)
        *dotclock_return = 0;
    return 1;
}

int bridgeXF86VidModeSwitchToMode(Display *dpy, int screen, void *modeline)
{
    return 1;
}

int bridgeXF86VidModeSetViewPort(Display *dpy, int screen, int x, int y)
{
    return 1;
}

int bridgeXF86VidModeGetViewPort(Display *dpy, int screen, int *x_return, int *y_return)
{
    if (x_return)
        *x_return = 0;
    if (y_return)
        *y_return = 0;
    return 1;
}

int bridgeXF86VidModeLockModeSwitch(Display *dpy, int screen, int lock)
{
    return 1;
}

int bridgeXF86VidModeQueryVersion(Display *dpy, int *major_return, int *minor_return)
{
    if (major_return)
        *major_return = 0;
    if (minor_return)
        *minor_return = 0;
    return 1;
}

Atom bridgeXCreateColormap(Display *dpy, Window w, Visual *visual, int alloc)
{
    return 1;
}

int bridgeXFreeColormap(Display *dpy, Colormap colormap)
{
    return 1;
}

// ------------------------------------------------------------------
// The rest of the X11 client the g6 games (Angry Birds, The Walking
// Dead) and Pac-Man pull in: windowing, the 2D path (GC/pixmap/SHM),
// the keyboard, colours, WM hints, and the X ext/xi/im/randr protocol
// internals. Fakes, as the display above -- enough not to hit the crash
// stub; the real draw and input go through the loader's GL and desktop.

int bridgeXDefaultScreen(Display *dpy)
{
    return 0;
}

char *bridgeXDisplayName(Display *dpy)
{
    static const char *n = "WIN32";
    return (char *)n;
}

int bridgeXQueryExtension(Display *dpy, const char *name, int *evenbase, int *errbase)
{
    if (evenbase)
        *evenbase = 1;
    if (errbase)
        *errbase = 128;
    return 1;
}

int bridgeXMissingExtension(Display *dpy, int *evenbase, int *errbase)
{
    return 0;
}

// Event queue: the loader feeds the game's input elsewhere, so the X
// queue is always empty.
int bridgeXEventsQueued(Display *dpy, int mode)
{
    return 0;
}

int bridgeXPeekEvent(Display *dpy, XEvent *event_return)
{
    return 0;
}

int bridgeXMaskEvent(Display *dpy, long event_mask, int event_type)
{
    return 0;
}

Window bridgeXCreateSimpleWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                                 unsigned int border_width, unsigned long border_pixel, unsigned long background_pixel)
{
    if (!getSDLWindow())
        startSDL();
    return (Window)getSDLWindow();
}

int bridgeXRaiseWindow(Display *dpy, Window w)
{
    return 1;
}

int bridgeXIconifyWindow(Display *dpy, Window w, int level)
{
    return 1;
}

int bridgeXMoveResizeWindow(Display *dpy, Window w, int x, int y, unsigned int width, unsigned int height)
{
    return 1;
}

int bridgeXReparentWindow(Display *dpy, Window w, Window parent, int x, int y)
{
    return 1;
}

int bridgeXClearWindow(Display *dpy, Window w)
{
    return 1;
}

int bridgeXInstallColormap(Display *dpy, Colormap colormap)
{
    return 1;
}

int bridgeXSetWindowBackground(Display *dpy, Window w, unsigned long pixel)
{
    return 1;
}

int bridgeXSetWindowBackgroundPixmap(Display *dpy, Window w, Pixmap pixmap)
{
    return 1;
}

int bridgeXSetWindowColormap(Display *dpy, Window w, Colormap colormap)
{
    return 1;
}

int bridgeXResetScreenSaver(Display *dpy)
{
    return 1;
}

int bridgeXChangePointerControl(Display *dpy, int mouse_gain, int mouse_speed, int min_denom, int max_denom, int threshold)
{
    return 1;
}

// The real Xlib reports the acceleration: numerator, denominator, threshold.
int bridgeXGetPointerControl(Display *dpy, int *accel_numerator_ret, int *accel_denominator_ret, int *threshold_ret)
{
    if (accel_numerator_ret)
        *accel_numerator_ret = 1;
    if (accel_denominator_ret)
        *accel_denominator_ret = 1;
    if (threshold_ret)
        *threshold_ret = 0;
    return 1;
}

XVisualInfo *bridgeXGetVisualInfo(Display *dpy, long valuemask, XVisualInfo *vistr, int *nitems)
{
    (void)valuemask;
    (void)vistr;
    XVisualInfo *vi = (XVisualInfo *)calloc(1, sizeof(XVisualInfo));
    if (vi)
    {
        vi->visual = (void *)1;
        vi->visualid = 1;
        vi->screen = 0;
        vi->depth = 24;
        vi->class_type = 4; // TrueColor
        vi->red_mask = 0xFF0000;
        vi->green_mask = 0x00FF00;
        vi->blue_mask = 0x0000FF;
        vi->colormap_size = 256;
        vi->bits_per_rgb = 8;
    }
    if (nitems)
        *nitems = 1;
    return vi;
}

int bridgeXMatchVisualInfo(Display *dpy, int depth, int class_type, XVisualInfo *vistr)
{
    if (vistr)
    {
        vistr->visual = (void *)1;
        vistr->visualid = 1;
        vistr->screen = 0;
        vistr->depth = depth;
        vistr->class_type = class_type;
        vistr->red_mask = 0xFF0000;
        vistr->green_mask = 0x00FF00;
        vistr->blue_mask = 0x0000FF;
        vistr->colormap_size = 256;
        vistr->bits_per_rgb = 8;
    }
    return 0; // Success
}

VisualID bridgeXVisualIDFromVisual(Visual *visual)
{
    return 1;
}

// 2D: the games render with GL; these dummies back the 2D paths they may touch.
unsigned long bridgeXCreateGC(Display *dpy, Drawable d, unsigned long valuemask, void *values)
{
    return 1;
}

int bridgeXFreeGC(Display *dpy, unsigned long gc)
{
    return 1;
}

Pixmap bridgeXCreatePixmap(Display *dpy, Drawable d, unsigned int width, unsigned int height, unsigned int depth)
{
    return 1;
}

void *bridgeXCreateImage(Display *dpy, Visual *visual, unsigned int depth, int format, char *data, unsigned int width, unsigned int height)
{
    return calloc(1, 256);
}

int bridgeXPutImage(Display *dpy, unsigned long gc, Pixmap src, void *image, int sx, int sy, int dx, int dy, unsigned int width, unsigned int height)
{
    return 1;
}

int bridgeXListPixmapFormats(Display *dpy, int *nformats_return)
{
    if (nformats_return)
        *nformats_return = 0;
    return 0;
}

// MIT-SHM: reported absent, so the games take the plain XPutImage path.
int bridgeXShmQueryExtension(Display *dpy)
{
    return 0;
}

int bridgeXShmAttach(Display *dpy, void *shmseg)
{
    return 1;
}

int bridgeXShmDetach(Display *dpy, void *shmseg)
{
    return 1;
}

void *bridgeXShmCreateImage(Display *dpy, Visual *visual, unsigned int depth, int format, char *data, void *shmseg, unsigned int width, unsigned int height)
{
    return calloc(1, 256);
}

int bridgeXShmPutImage(Display *dpy, unsigned long gc, Pixmap dst, void *image, int sx, int sy, int dx, int dy, unsigned int width, unsigned int height, int send_event)
{
    return 1;
}

// Keyboard: the games poll their own input; the X keymap is empty.
int bridgeXKeycodeToKeysym(Display *dpy, unsigned char keycode, int index)
{
    return 0;
}

int bridgeXQueryKeymap(Display *dpy, char *key_return)
{
    if (key_return)
        memset(key_return, 0, 32);
    return 1;
}

int bridgeXRefreshKeyboardMapping(void *event)
{
    return 1;
}

void *bridgeXGetModifierMapping(Display *dpy)
{
    return calloc(1, 32);
}

void bridgeXFreeModifiermap(void *modmap)
{
    free(modmap);
}

// Colours: dummies. The output must be a full i386 XColor (12 bytes:
// long pixel; short red, green, blue; char flags; char pad).
int bridgeXAllocColor(Display *dpy, Colormap cmap, void *color_return)
{
    if (color_return)
        memset(color_return, 0, 12);
    return 1;
}

int bridgeXFreeColors(Display *dpy, Colormap cmap, int *list, int ncolors, int deltops)
{
    return 1;
}

int bridgeXQueryColors(Display *dpy, Colormap cmap, void *color, int ncolors)
{
    return 1;
}

int bridgeXStoreColors(Display *dpy, Colormap cmap, void *color, int ncolors)
{
    return 1;
}

// WM hints / properties: no-ops.
void *bridgeXAllocClassHint(void)
{
    return calloc(1, 16);
}

// The i386 XSizeHints (72 bytes): the game writes its window hints into it.
void *bridgeXAllocSizeHints(void)
{
    return calloc(1, 72);
}

int bridgeXSetClassHint(Display *dpy, Window w, void *class_hints)
{
    return 1;
}

int bridgeXSetWMHints(Display *dpy, Window w, void *wm_hints)
{
    return 1;
}

// The real Xlib allocates and returns the hints: zero flags, no hints.
// The i386 XWMHints is 36 bytes (nine longs).
void *bridgeXGetWMHints(Display *dpy, Window w)
{
    return calloc(1, 36);
}

int bridgeXSetTextProperty(Display *dpy, Window w, void *text_prop, Atom name)
{
    return 1;
}

int bridgeXDeleteProperty(Display *dpy, Window w, Atom property, Atom type, int format)
{
    return 1;
}

int bridgeXutf8TextListToTextProperty(char **list, int count, void *text_prop_return, int mode)
{
    return 1;
}

// Input method lookup: the shim has no IME, report "no conversion".
int bridgeXutf8LookupString(Display *dpy, XKeyEvent *key_event, char *buffer_return, int nbytes, int *keysym_return, int *status_return)
{
    (void)dpy;
    (void)key_event;
    if (buffer_return && nbytes > 0)
        buffer_return[0] = '\0';
    if (keysym_return)
        *keysym_return = 0;
    if (status_return)
        *status_return = 0; // XLookupStatusNone
    return 0;
}

int bridgeXSetIOErrorHandler(int (*IOErrorHandler)(Display *))
{
    (void)IOErrorHandler;
    return 0;
}

int bridgeXGetErrorDatabaseText(Display *dpy, const char *name, const char *value, char *buffer, unsigned long nbuffer)
{
    if (buffer && nbuffer)
        buffer[0] = '\0';
    return 0;
}

// X extension framework internals: the shim has no live extensions.
void bridgeXextAddDisplay(Display *dpy, void *xext)
{
}

int bridgeXextCreateExtension(Display *dpy, void *info, int extension, int event_base, int error_base)
{
    return 1;
}

void bridgeXextDestroyExtension(Display *dpy, void *info)
{
}

void bridgeXextRemoveDisplay(Display *dpy, void *xext)
{
}

void bridgeXSetExtensionErrorHandler(void *handler)
{
}

// XInput (XI) event translation: no-ops.
void bridgeXESetEventToWire(Display *dpy, void *event, void *xEvent)
{
}

void bridgeXESetWireToEvent(Display *dpy, void *event, void *wire)
{
}

// X11 wire-protocol internals: the shim talks to no server.
int bridge_XEatData(Display *dpy, int n, int *read_all)
{
    if (read_all)
        *read_all = 1;
    return 0;
}

void bridge_XGetRequest(Display *dpy, int *request, void *request_ptr)
{
}

int bridge_XSend(Display *dpy, char *request, int extra, int async)
{
    return 1;
}

int bridge_XRead(Display *dpy, char *buf, int len)
{
    return 0;
}

int bridge_XReadPad(Display *dpy, char *buf, int len, int padlen)
{
    return 0;
}

void bridge_XSetLastRequestRead(Display *dpy)
{
}

int bridge_XFlushGCCache(Display *dpy)
{
    return 1;
}

// Input methods: the games never drive a real IM; dummies.
void *bridgeXOpenIM(Display *dpy, char **args, int nargs)
{
    return calloc(1, 64);
}

int bridgeXCloseIM(void *im)
{
    free(im);
    return 1;
}

void *bridgeXCreateIC(void *im)
{
    return calloc(1, 64);
}

int bridgeXDestroyIC(void *ic)
{
    free(ic);
    return 1;
}

int bridgeXGetICValues(void *ic)
{
    return 0;
}

int bridgeXSetICFocus(void *ic, Window w)
{
    return 1;
}

int bridgeXUnsetICFocus(void *ic)
{
    return 1;
}

Display *bridgeXDisplayOfIM(void *im)
{
    return (Display *)&s_dummyDisplay;
}

int bridgeXSetLocaleModifiers(const char *mod)
{
    return 1;
}

int bridgeXFilterEvent(Display *dpy, XEvent *event, int num_vars, void *vars)
{
    return 0;
}

// Xrandr (Pac-Man's mode-switching): reported as a working 1.2, no-ops.
int bridgeXRRQueryVersion(Display *dpy, int *maj, int *min)
{
    if (maj)
        *maj = 1;
    if (min)
        *min = 2;
    return 1;
}

void *bridgeXRRGetScreenInfo(Display *dpy, Window w)
{
    return calloc(1, 128);
}

void bridgeXRRFreeScreenConfigInfo(void *info)
{
    free(info);
}

int bridgeXRRSetScreenConfig(void *dpy, void *rcs, Time t, Window root, int x, int y, unsigned int w, unsigned int h, int r, int mask)
{
    return 1;
}

void *bridgeXRRConfigCurrentConfiguration(void *dpy, int *mask_ret)
{
    static unsigned long cfg = 1;
    if (mask_ret)
        *mask_ret = 0;
    return &cfg;
}

int bridgeXRRConfigSizes(void *dpy, int *width_ret, int *height_ret, int *widths_ret)
{
    if (width_ret)
        *width_ret = 1;
    if (height_ret)
        *height_ret = 1;
    if (widths_ret)
        *widths_ret = 1;
    return 1;
}
#endif

//////////////////////////////////////////////////////////////////////

#ifdef __linux__
extern Display *x11Display;
extern Window x11Window;
extern bool gettingGPUVendor;
extern bool creatingWindow;
extern int gWidth;
extern int gHeight;

Window window;

/**
 * Stop the house of the dead games turning keyboard repeating off.
 */
int XAutoRepeatOff(Display *display)
{
    return 0;
}

Display *XOpenDisplay(const char *display_name)
{
    Display *(*_XOpenDisplay)(const char *display_name) = dlsym(RTLD_NEXT, "XOpenDisplay");

    // Counter Strike NEO opens its window with SDL 1.2's own X11 driver: it
    // needs the real X server connection, not the loader's.
    if (gettingGPUVendor || creatingWindow || (isNamcoN2Game() && namcoN2CurrentGame()->crc32 == CSNEO_N2))
    {
        return _XOpenDisplay(display_name);
    }
    else
    {
        startSDL();
        return x11Display;
    }
}

Window XCreateWindow(Display *display, Window parent, int x, int y, unsigned int width, unsigned int height, unsigned int border_width,
                     int depth, unsigned int class, Visual *visual, unsigned long valueMask, XSetWindowAttributes *attributes)
{
    Window (*_XCreateWindow)(Display *display, Window parent, int x, int y, unsigned int width, unsigned int height,
                             unsigned int border_width, int depth, unsigned int class, Visual *visual, unsigned long valueMask,
                             XSetWindowAttributes *attributes) = dlsym(RTLD_NEXT, "XCreateWindow");

    if ((gettingGPUVendor || creatingWindow))
    {
        window = _XCreateWindow(display, parent, x, y, width, height, border_width, depth, class, visual, valueMask, attributes);
        // The window is this process's (_NET_WM_PID), as SDL marks its own:
        // the quit watch (Esc, Alt+F4) only acts on a window so marked, and
        // the games making their own X11 window (Dead Heat Riders, Maximum
        // Heat 3D, Tank! Tank! Tank!) did not mark theirs.
        if (window)
        {
            long pid = getpid();
            XChangeProperty(display, window, XInternAtom(display, "_NET_WM_PID", False), XA_CARDINAL, 32,
                            PropModeReplace, (unsigned char *)&pid, 1);
        }
    }
    else
        window = x11Window;

    return window;
}

// Here we prevent the games to change the window properties
void XSetWMProperties(Display *display, Window w, XTextProperty *window_name, XTextProperty *icon_name, char **argv, int argc,
                      XSizeHints *normal_hints, XWMHints *wm_hints, XClassHint *class_hints)
{
    return;
}

// The calls below are kept from the game while the loader's SDL window stands
// in for its own. A game the loader gives no SDL window (the Namco ES1 games
// with their own X11/GLX window: gettingGPUVendor stays set, see init.c) gets
// the real ones, as XOpenDisplay and XCreateWindow above.
static bool ownXWindow(void)
{
    return gettingGPUVendor || creatingWindow;
}

int XMapWindow(Display *display, Window window)
{
    if (ownXWindow())
    {
        int (*_XMapWindow)(Display *, Window) = dlsym(RTLD_NEXT, "XMapWindow");
        return _XMapWindow(display, window);
    }
    return 0;
}

// The events a window just mapped gets, as the SDL window standing in for
// the game's would have sent them (it was mapped before the game asked).
static const int mapEventTypes[] = {MapNotify, Expose, ConfigureNotify, VisibilityNotify};
#define MAP_EVENT_COUNT ((int)(sizeof(mapEventTypes) / sizeof(mapEventTypes[0])))

// The size they give is the frame the game draws into (blitStretching.c):
// the window's, unless the game draws at a size of its own (Police Trainer
// 2's 640x480), which its reshape on ConfigureNotify must get, not the
// window's.
extern int blitWidth, blitHeight;

static void mapEvent(Display *display, int type, XEvent *event)
{
    int width = blitWidth > 0 ? blitWidth : gWidth, height = blitHeight > 0 ? blitHeight : gHeight;

    memset(event, 0, sizeof(*event));
    event->type = type;
    event->xany.display = display;
    event->xany.window = x11Window;
    // xany.window is the event's window for Expose, but the parent
    // ("event") for the structure events: theirs is set too.
    if (type == MapNotify)
        event->xmap.window = x11Window;
    else if (type == Expose)
    {
        event->xexpose.width = width;
        event->xexpose.height = height;
    }
    else if (type == ConfigureNotify)
    {
        event->xconfigure.window = x11Window;
        event->xconfigure.width = width;
        event->xconfigure.height = height;
    }
}

// Games that draw only once their window says it is mapped (Police Trainer
// 2's main loop waits for an X event while it has nothing to draw) get the
// map events once, through XPending and XNextEvent. The Teamplay games only:
// the others never had them.
static int mapEventsSent = MAP_EVENT_COUNT;

static bool sendsMapEvents(void)
{
    static bool init;

    if (!init)
    {
        init = true;
        if (isTeamplayGame())
            mapEventsSent = 0;
    }
    return mapEventsSent < MAP_EVENT_COUNT;
}

int XPending(Display *display)
{
    if (ownXWindow())
    {
        int (*_XPending)(Display *) = dlsym(RTLD_NEXT, "XPending");
        return _XPending(display);
    }
    return sendsMapEvents() ? MAP_EVENT_COUNT - mapEventsSent : 0;
}

int XNextEvent(Display *display, XEvent *event)
{
    int (*_XNextEvent)(Display *, XEvent *) = dlsym(RTLD_NEXT, "XNextEvent");

    if (!ownXWindow() && sendsMapEvents())
    {
        mapEvent(display, mapEventTypes[mapEventsSent++], event);
        return 0;
    }
    return _XNextEvent(display, event);
}

// Games waiting for an event of their window after mapping it (Puck Off:
// XMapWindow, then XIfEvent until its MapNotify): the SDL window standing in
// for theirs is already mapped, and sends them nothing. They get the events
// a window just mapped would: the first one their predicate takes.
int XIfEvent(Display *display, XEvent *event, Bool (*predicate)(Display *, XEvent *, XPointer), XPointer arg)
{
    int (*_XIfEvent)(Display *, XEvent *, Bool (*)(Display *, XEvent *, XPointer), XPointer) =
        dlsym(RTLD_NEXT, "XIfEvent");

    if (!ownXWindow())
    {
        for (int i = 0; i < MAP_EVENT_COUNT; i++)
        {
            mapEvent(display, mapEventTypes[i], event);
            if (predicate(display, event, arg))
                return 0;
        }
        log_warn("XIfEvent: the game waits for an event the loader's window does not send");
    }
    return _XIfEvent(display, event, predicate, arg);
}

int XGrabPointer(Display *display, Window grab_window, Bool owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                 Window confine_to, Cursor cursor, Time time)
{
    return 0;
}

int XSetInputFocus(Display *display, Window window, int revert_to_window, Time time)
{
    return 0;
}

Bool XTranslateCoordinates(Display *display, Window src_w, Window dest_w, int src_x, int src_y, int *dest_x_return, int *dest_y_return,
                           Window *child_return)
{
    return true;
}

int XMoveWindow(Display * display, Window w, int x, int y)
{
    return 0;
}

// The window's size is the loader's (FULLSCREEN, WIDTH, HEIGHT): a game
// resizing the SDL window standing in for its own (Police Trainer 2 makes
// it the screen's size, its fullscreen) would leave SDL with the old size,
// and the frame drawn unscaled in a corner. Its own window keeps the calls.
int XResizeWindow(Display *display, Window w, unsigned int width, unsigned int height)
{
    int (*_XResizeWindow)(Display *, Window, unsigned int, unsigned int) = dlsym(RTLD_NEXT, "XResizeWindow");

    return ownXWindow() ? _XResizeWindow(display, w, width, height) : 0;
}

int XConfigureWindow(Display *display, Window w, unsigned int valueMask, XWindowChanges *values)
{
    int (*_XConfigureWindow)(Display *, Window, unsigned int, XWindowChanges *) = dlsym(RTLD_NEXT, "XConfigureWindow");

    return ownXWindow() ? _XConfigureWindow(display, w, valueMask, values) : 0;
}

Bool XF86VidModeSwitchToMode(Display *display, int screen, XF86VidModeModeInfo *modesinfo)
{
    return 0;
}

Bool XF86VidModeGetViewPort(Display *display, int screen, int *x_return, int *y_return)
{
    return 0;
}

// A game with its own window (Tank! Tank! Tank!) paces its frames on the
// refresh rate it reads from the mode line: the real one.
Bool XF86VidModeGetModeLine(Display *display, int screen, int *dotclock_return, XF86VidModeModeLine *modeline)
{
    if (ownXWindow())
    {
        Bool (*real)(Display *, int, int *, XF86VidModeModeLine *) = dlsym(RTLD_NEXT, "XF86VidModeGetModeLine");
        if (real && real(display, screen, dotclock_return, modeline))
            return 1;
    }
    return 0;
}

int XF86VidModeGetAllModeLines(Display *display, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo)
{
    int (*_XF86VidModeGetAllModeLines)(Display *display, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo) =
        dlsym(RTLD_NEXT, "XF86VidModeGetAllModeLines");

    if (_XF86VidModeGetAllModeLines(display, screen, modecount_return, modesinfo) != 1)
    {
        printf("Error: Could not get list of screen modes.\n");
        exit(1);
    }
    else
    {
        XF86VidModeModeInfo **modes = *modesinfo;
        modes[0]->hdisplay = getConfig()->width;
        modes[0]->vdisplay = getConfig()->height;
    }
    return true;
}

void glGenFencesNV(int n, uint32_t *fences)
{
    static unsigned int curf = 1;
    while (n--)
    {
        *fences++ = curf++;
    }
    return;
}

void glDeleteFencesNV(int a, const uint32_t *b)
{
    return;
}


#endif

/*
#ifdef __cplusplus
extern "C"
{
#endif

int XInitThreads()
{
    return bridgeXInitThreads();  
}

Display *XOpenDisplay(const char *name)
{
    return bridgeXOpenDisplay(name);    
}

int XCloseDisplay(Display *dpy)
{
    return bridgeXCloseDisplay(dpy);
}

Window XCreateWindow(Display *dpy, Window parent, int x, int y, unsigned int width, unsigned int height,
                   unsigned int border_width, int depth, unsigned int class_type, void *visual, unsigned long valuemask,
                   XSetWindowAttributes *attributes)
{
    return bridgeXCreateWindow(dpy, parent, x, y, width, height, border_width, depth, class_type, visual, valuemask, attributes);
}

int XDestroyWindow(Display *dpy, Window win)
{
    return bridgeXDestroyWindow(dpy, win);
}

void XMapWindow(Display *dpy, Window win)
{
    bridgeXMapWindow(dpy, win);
}

int _XReply(Display *dpy, int *rep, int extra, int discard)
{
    return bridgeXReply(dpy, rep, extra, discard);
}

int XMoveWindow(Display *dpy, Window win, int x, int y)
{
    return bridgeXMoveWindow(dpy, win, x, y);
}

int XResizeWindow(Display *dpy, Window win, unsigned int width, unsigned int height)
{
    return bridgeXResizeWindow(dpy, win, width, height);
}

int XSync(Display *dpy, int discard)
{
    return bridgeXSync(dpy, discard);
}

int XFlush(Display *dpy)
{
    return bridgeXFlush(dpy);
}

    void XLockDisplay(Display *dpy)
{
    bridgeXLockDisplay(dpy);
}

void XUnlockDisplay(Display *dpy)
{
    bridgeXUnlockDisplay(dpy);
}

void XSetErrorHandler(void *handler)
{
    bridgeXSetErrorHandler(handler);
}

int XGetErrorText(Display *dpy, int code, char *buffer, int length)
{
    return bridgeXGetErrorText(dpy, code, buffer, length);    
}

int XDisplayWidth(void *dpy, int screen_number)
{
    return bridgeXDisplayWidth(dpy, screen_number);
}

int XDisplayHeight(void *dpy, int screen_number)
{
    return bridgeXDisplayHeight(dpy, screen_number);
}

int XDisplayWidthMM(void *dpy, int screen_number)
{
    return bridgeXDisplayWidthMM(dpy, screen_number);
}

int XDisplayHeightMM(void *dpy, int screen_number)
{
    return bridgeXDisplayHeightMM(dpy, screen_number);
}

Atom XInternAtom(Display *dpy, const char *atom_name, bool create_if_not_exist)
{
    return bridgeXInternAtom(dpy, atom_name, create_if_not_exist);
}

void *XextFindDisplay(Display *dpy)
{
    return bridgeXextFindDisplay(dpy);
}

int XPending(Display *dpy)
{
    return bridgeXPending(dpy);
}

void XNextEvent(Display *dpy, XEvent *event_return)
{
    bridgeXNextEvent(dpy, event_return);
}

void XSendEvent(Display *dpy, Window w, bool propagate, long event_mask, XEvent *event_send)
{
    bridgeXSendEvent(dpy, w, propagate, event_mask, event_send);
}

int XCheckTypedEvent(Display *dpy, int type, XEvent *event_return)
{
    return bridgeXCheckTypedEvent(dpy, type, event_return);
}

int XSelectInput(Display *dpy, Window win, long event_mask)
{
    return bridgeXSelectInput(dpy, win, event_mask);
}

int XSetCloseDownMode(Display *dpy, int close_mode)
{
    return bridgeXSetCloseDownMode(dpy, close_mode);
}

int XSetInputFocus(Display *dpy, Window win, int revert_to, Time time)
{
    return bridgeXSetInputFocus(dpy, win, revert_to, time);
}

int XAutoRepeatOff(Display *dpy)
{
    return bridgeXAutoRepeatOff(dpy);
}

int XKeysymToKeycode(Display *dpy, int keysym)
{
    return bridgeXKeysymToKeycode(dpy, keysym);
}

int XTranslateCoordinates(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, int *dest_x_return, int *dest_y_return,
                                Window *child_return)
{
    return bridgeXTranslateCoordinates(dpy, src_w, dest_w, src_x, src_y, dest_x_return, dest_y_return, child_return);
}

int XQueryPointer(Display *dpy, Window win, Window *root_return, Window *child_return, int *root_x_return, int *root_y_return,
                        int *win_x_return, int *win_y_return, unsigned int *mask_return)
{
    return bridgeXQueryPointer(dpy, win, root_return, child_return, root_x_return, root_y_return, win_x_return, win_y_return, mask_return);
}

void XWarpPointer(Display *dpy, Window src_w, Window dest_w, int src_x, int src_y, unsigned int src_width, unsigned int src_height,
                        int dest_x, int dest_y)
{
    bridgeXWarpPointer(dpy, src_w, dest_w, src_x, src_y, src_width, src_height, dest_x, dest_y);
}

void XGrabPointer(Display *dpy, Window win, int owner_events, unsigned int event_mask, int pointer_mode, int keyboard_mode,
                        Window confine_to, Cursor cursor, Time time)
{
    bridgeXGrabPointer(dpy, win, owner_events, event_mask, pointer_mode, keyboard_mode, confine_to, cursor, time);
}

void XUngrabPointer(Display *dpy, Time time)
{
    bridgeXUngrabPointer(dpy, time);
}

int XGrabKeyboard(Display *dpy, Window win, int owner_events, int pointer_mode, int keyboard_mode, Time time)
{
    return bridgeXGrabKeyboard(dpy, win, owner_events, pointer_mode, keyboard_mode, time);
}

int XUngrabKeyboard(Display *dpy, Time time)
{
    return bridgeXUngrabKeyboard(dpy, time);
}

int XAutoRepeatOn(Display *dpy)
{
    return bridgeXAutoRepeatOn(dpy);
}

int XGetWindowAttributes(Display *dpy, Window win, XWindowAttributes *attr)
{
    return bridgeXGetWindowAttributes(dpy, win, attr);
}

int XGetGeometry(Display *dpy, Drawable d, Window *root_return, int *x_return, int *y_return, unsigned int *width_return,
                       unsigned int *height_return, unsigned int *border_width_return, unsigned int *depth_return)
{
    return bridgeXGetGeometry(dpy, d, root_return, x_return, y_return, width_return, height_return, border_width_return, depth_return);
}

int XSetStandardProperties(Display *dpy, Window win, const char *window_name, const char *icon_name, Pixmap icon_pixmap, char **argv,
                                 int argc, XSetWindowAttributes *attributes)
{
    return bridgeXSetStandardProperties(dpy, win, window_name, icon_name, icon_pixmap, argv, argc, attributes);
}

int XChangeProperty(Display *dpy, Window win, Atom property, Atom type, int format, int mode, const unsigned char *data,
                          int nelements)
{
    return bridgeXChangeProperty(dpy, win, property, type, format, mode, data, nelements);
}

int XParseColor(Display *dpy, Colormap colormap, const char *spec, int *color_return)
{
    return bridgeXParseColor(dpy, colormap, spec, color_return);
}

void XMapRaised(Display *dpy, Window w)
{
    bridgeXMapRaised(dpy, w);
}

void *XRRGetScreenResources(Display *dpy, Window window)
{
    return bridgeXRRGetScreenResources(dpy, window);
}


void *XRRGetCrtcInfo(Display *dpy, int crtc)
{
    return bridgeXRRGetCrtcInfo(dpy, crtc);
}

int XLookupString(XKeyEvent *key_event, char *buffer_return, int buffer_size, int *keysym_return, int *status_return)
{
    return bridgeXLookupString(key_event, buffer_return, buffer_size, keysym_return, status_return);
}

int XStoreName(Display *dpy, Window win, const char *name)
{
    return bridgeXStoreName(dpy, win, name);
}

int XSetWMProtocols(Display *dpy, Window win, Atom *protocols, int count)
{
    return bridgeXSetWMProtocols(dpy, win, protocols, count);
}

int XSetWMProperties(Display *dpy, Window win, XTextProperty *window_name, XTextProperty *icon_name, char **argv, int argc,
                           XSizeHints *normal_hints, XWMHints *wm_hints, XClassHint *class_hints)
{
    return bridgeXSetWMProperties(dpy, win, window_name, icon_name, argv, argc, normal_hints, wm_hints, class_hints);
}

void XSetWMNormalHints(Display *dpy, Window w, XSizeHints *hints)
{
    bridgeXSetWMNormalHints(dpy, w, hints);
}

void XSetTransientForHint(Display *dpy, Window w, Window transient_for)
{
    bridgeXSetTransientForHint(dpy, w, transient_for);
}

int XStringListToTextProperty(char **list, int count, XTextProperty *text_prop_return)
{
    return bridgeXStringListToTextProperty(list, count, text_prop_return);
}

void XFree(void *data)
{
    bridgeXFree(data);
}

Pixmap XCreatePixmapFromBitmapData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height,
                                         unsigned long fg, unsigned long bg, unsigned int depth)
{
    return bridgeXCreatePixmapFromBitmapData(dpy, drawable, data, width, height, fg, bg, depth);
}

Cursor XCreatePixmapCursor(Display *dpy, Pixmap source, Pixmap mask, void *foreground_color, void *background_color, unsigned int x,
                                 unsigned int y)
{
    return bridgeXCreatePixmapCursor(dpy, source, mask, foreground_color, background_color, x, y);
}

int XFreePixmap(Display *dpy, Pixmap pixmap)
{
    return bridgeXFreePixmap(dpy, pixmap);
}

int XFreeCursor(Display *dpy, Cursor cursor)
{
    return bridgeXFreeCursor(dpy, cursor);
}

int XDefineCursor(Display *dpy, Window w, Cursor c)
{
    return bridgeXDefineCursor(dpy, w, c);
}

Cursor XCreateFontCursor(Display *dpy, int shape)
{
    return bridgeXCreateFontCursor(dpy, shape);
}

Pixmap XCreateBitmapFromData(Display *dpy, Drawable drawable, char *data, unsigned int width, unsigned int height)
{
    return bridgeXCreateBitmapFromData(dpy, drawable, data, width, height);
}

int XF86VidModeQueryExtension(Display *dpy, int *event_base_return, int *error_base_return)
{
    return bridgeXF86VidModeQueryExtension(dpy, event_base_return, error_base_return);
}

int XF86VidModeGetAllModeLines(Display *dpy, int screen, int *modecount_return, XF86VidModeModeInfo ***modesinfo_return)
{
    return bridgeXF86VidModeGetAllModeLines(dpy, screen, modecount_return, modesinfo_return);
}

int XF86VidModeGetModeLine(Display *dpy, int screen, int *dotclock_return, void *modeline)
{
    return bridgeXF86VidModeGetModeLine(dpy, screen, dotclock_return, modeline);
}

int XF86VidModeSwitchToMode(Display *dpy, int screen, void *modeline)
{
    return bridgeXF86VidModeSwitchToMode(dpy, screen, modeline);
}

int XF86VidModeSetViewPort(Display *dpy, int screen, int x, int y)
{
    return bridgeXF86VidModeSetViewPort(dpy, screen, x, y);
}

int XF86VidModeGetViewPort(Display *dpy, int screen, int *x_return, int *y_return)
{
    return bridgeXF86VidModeGetViewPort(dpy, screen, x_return, y_return);
}

int XF86VidModeLockModeSwitch(Display *dpy, int screen, int lock)
{
    return bridgeXF86VidModeLockModeSwitch(dpy, screen, lock);
}

int XF86VidModeQueryVersion(Display *dpy, int *major_return, int *minor_return)
{
    return bridgeXF86VidModeQueryVersion(dpy, major_return, minor_return);
}

Atom XCreateColormap(Display *dpy, Window w, Visual *visual, int alloc)
{
    return bridgeXCreateColormap(dpy, w, visual, alloc);
}

#ifdef __cplusplus
}
#endif

*/