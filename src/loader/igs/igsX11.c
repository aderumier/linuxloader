// The IGS games' X display unlocked, and the screen's size (see igs.c):
// apart, Xlib's internal header and MinHook's both define BOOL and INT32.

#include <X11/Xlibint.h>

#include "igs.h"

void igsUnlockDisplay(void *display)
{
    Display *dpy = display;

    dpy->lock_fns = NULL;
    dpy->lock = NULL;
}

int igsScreenSize(int *width, int *height)
{
    Display *dpy = XOpenDisplay(NULL);

    if (!dpy)
        return 0;
    *width = DisplayWidth(dpy, DefaultScreen(dpy));
    *height = DisplayHeight(dpy, DefaultScreen(dpy));
    XCloseDisplay(dpy);
    return *width > 0 && *height > 0;
}
