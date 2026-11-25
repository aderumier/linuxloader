// libX11 and GLX on Win32 windows and WGL (see ll64.h), enough for SDL 2's
// X11 video driver linked into the games: one display, one screen, one
// TrueColor visual; X windows are Win32 windows, their messages XEvents. The
// structures are Xlib's as an LP64 Linux build lays them out (long: 64 bits).
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ll64.h"

typedef uint64_t XID;
typedef XID Window, Atom, Colormap, Cursor, Pixmap, Drawable, KeySym, VisualID, Time;

SYSV void *lxMalloc(size_t n);
SYSV void *lxCalloc(size_t a, size_t b);
SYSV void lxFree(void *p);
SYSV char *lxStrdup(const char *s);

// ---------------------------------------------------------------------------
// Xlib's structures.

typedef struct
{
    void *extData;
    VisualID visualid;
    int class_;
    int pad;
    uint64_t redMask, greenMask, blueMask;
    int bitsPerRgb, mapEntries;
} XVisual;

typedef struct
{
    XVisual *visual;
    VisualID visualid;
    int screen, depth, class_, pad;
    uint64_t redMask, greenMask, blueMask;
    int colormapSize, bitsPerRgb;
} XVisualInfo;

typedef struct
{
    void *extData;
    void *display;
    Window root;
    int width, height, mwidth, mheight, ndepths, pad0;
    void *depths;
    int rootDepth, pad1;
    XVisual *rootVisual;
    void *defaultGc;
    Colormap cmap;
    uint64_t whitePixel, blackPixel;
    int maxMaps, minMaps, backingStore, saveUnders;
    int64_t rootInputMask;
} XScreen;

typedef struct
{
    void *extData;
    void *private1;
    int fd, private2, protoMajor, protoMinor;
    char *vendor;
    XID private3, private4, private5;
    int private6, pad0;
    void *resourceAlloc;
    int byteOrder, bitmapUnit, bitmapPad, bitmapBitOrder, nformats, pad1;
    void *pixmapFormat;
    int private8, release;
    void *private9, *private10;
    int qlen, pad2;
    uint64_t lastRequestRead, request;
    void *private11, *private12, *private13, *private14;
    unsigned maxRequestSize, pad3;
    void *db;
    void *private15;
    char *displayName;
    int defaultScreen, nscreens;
    XScreen *screens;
    uint64_t motionBuffer, private16;
    int minKeycode, maxKeycode;
    void *private17, *private18;
    int private19, pad4;
    char *xdefaults;
    uint8_t spare[256];
} XDisplay;

// XEvent: a union of 24 longs.
typedef union
{
    int type;
    struct
    {
        int type, pad;
        uint64_t serial;
        int sendEvent, pad1;
        void *display;
        Window window, root, subwindow;
        Time time;
        int x, y, xRoot, yRoot;
        unsigned state, keycode;   // keycode: button for buttons
        int sameScreen;
    } key;
    struct
    {
        int type, pad;
        uint64_t serial;
        int sendEvent, pad1;
        void *display;
        Window event, window;
        int x, y, width, height, borderWidth, pad2;
        Window above;
        int overrideRedirect;
    } configure;
    struct
    {
        int type, pad;
        uint64_t serial;
        int sendEvent, pad1;
        void *display;
        Window event, window;
        int overrideRedirect;
    } map;
    struct
    {
        int type, pad;
        uint64_t serial;
        int sendEvent, pad1;
        void *display;
        Window window;
        int x, y, width, height, count;
    } expose;
    struct
    {
        int type, pad;
        uint64_t serial;
        int sendEvent, pad1;
        void *display;
        Window window;
        int mode, detail;
    } focus;
    struct
    {
        int type, pad;
        uint64_t serial;
        int sendEvent, pad1;
        void *display;
        Window window, root, subwindow;
        Time time;
        int x, y, xRoot, yRoot, mode, detail, sameScreen, focus;
        unsigned state;
    } crossing;
    struct
    {
        int type, pad;
        uint64_t serial;
        int sendEvent, pad1;
        void *display;
        Window window;
        Atom messageType;
        int format, pad2;
        int64_t l[5];
    } client;
    struct
    {
        int type, pad;
        uint64_t serial;
        int sendEvent, pad1;
        void *display;
        Window window;
    } any;
    int64_t pad[24];
} XEvent;

enum
{
    KeyPress = 2, KeyRelease = 3, ButtonPress = 4, ButtonRelease = 5, MotionNotify = 6, EnterNotify = 7,
    LeaveNotify = 8, FocusIn = 9, FocusOut = 10, Expose = 12, UnmapNotify = 18, MapNotify = 19,
    ConfigureNotify = 22, ClientMessage = 33,
};

typedef struct
{
    int x, y, width, height, borderWidth, depth;
    XVisual *visual;
    Window root;
    int class_, bitGravity, winGravity, backingStore;
    uint64_t backingPlanes, backingPixel;
    int saveUnder, pad0;
    Colormap colormap;
    int mapInstalled, mapState;
    int64_t allEventMasks, yourEventMask, doNotPropagateMask;
    int overrideRedirect, pad1;
    XScreen *screen;
} XWindowAttributes;

// ---------------------------------------------------------------------------
// The display, its windows and event queue.

#define ROOT_WINDOW 0x100
#define VISUAL_ID 0x21
#define MAX_WINDOWS 32
#define QUEUE_SIZE 1024

typedef struct
{
    Window id;
    HWND hwnd;
    HDC dc;
    // width, height: the client area; gameWidth, gameHeight: the size the
    // game made it (in full screen the frame is scaled from it).
    int width, height, gameWidth, gameHeight, mapped, fullscreen, blankCursor, pixelFormatSet;
    int64_t eventMask;
} XWin;

static XDisplay display;
static XScreen screen;
static XVisual visual;
static XWin windows[MAX_WINDOWS];
static Window nextWindowId = 0x200001;
static XEvent queue[QUEUE_SIZE];
static int qHead, qCount;
static uint64_t serial;
static Atom atomWmProtocols, atomWmDeleteWindow, atomNetWmState, atomNetWmStateFullscreen, atomNetWmName,
    atomWmName;
static int pixelFormat;
static HWND focusWindow;
static int mouseInside;

static XWin *findWindow(Window id)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].id && windows[i].id == id)
            return &windows[i];
    return NULL;
}

static XWin *windowOf(HWND h)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].id && windows[i].hwnd == h)
            return &windows[i];
    return NULL;
}

int lxGameFocused(void)
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!fg)
        return 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static void enqueue(const XEvent *e)
{
    if (qCount == QUEUE_SIZE)
    {
        qHead = (qHead + 1) % QUEUE_SIZE;
        qCount--;
    }
    queue[(qHead + qCount) % QUEUE_SIZE] = *e;
    ((XEvent *)&queue[(qHead + qCount) % QUEUE_SIZE])->any.serial = ++serial;
    qCount++;
}

static void dequeueAt(int i, XEvent *out)
{
    // i: the index in the queue (0 the oldest).
    *out = queue[(qHead + i) % QUEUE_SIZE];
    for (int k = i; k > 0; k--)
        queue[(qHead + k) % QUEUE_SIZE] = queue[(qHead + k - 1) % QUEUE_SIZE];
    qHead = (qHead + 1) % QUEUE_SIZE;
    qCount--;
}

static void initEvent(XEvent *e, int type, XWin *w)
{
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->any.display = &display;
    e->any.window = w ? w->id : ROOT_WINDOW;
}

// Windows' scan codes to evdev's key codes (X's are 8 more).
static int linuxKey(LPARAM lp)
{
    int sc = (int)((lp >> 16) & 0xff), ext = (int)((lp >> 24) & 1);
    if (!ext)
    {
        if (sc == 0x45)
            return 119;   // Pause (Num Lock comes extended)
        return sc;
    }
    switch (sc)
    {
    case 0x1c: return 96;   // KP Enter
    case 0x1d: return 97;   // Right Ctrl
    case 0x35: return 98;   // KP /
    case 0x37: return 99;   // Print
    case 0x38: return 100;  // Right Alt
    case 0x45: return 69;   // Num Lock
    case 0x47: return 102;
    case 0x48: return 103;
    case 0x49: return 104;
    case 0x4b: return 105;
    case 0x4d: return 106;
    case 0x4f: return 107;
    case 0x50: return 108;
    case 0x51: return 109;
    case 0x52: return 110;
    case 0x53: return 111;
    case 0x5b: return 125;
    case 0x5c: return 126;
    case 0x5d: return 127;
    }
    return 0;
}

static unsigned modifierState(void)
{
    unsigned s = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) s |= 1;
    if (GetKeyState(VK_CAPITAL) & 1) s |= 2;
    if (GetKeyState(VK_CONTROL) & 0x8000) s |= 4;
    if (GetKeyState(VK_MENU) & 0x8000) s |= 8;
    if (GetKeyState(VK_NUMLOCK) & 1) s |= 16;
    if (GetKeyState(VK_LBUTTON) & 0x8000) s |= 0x100;
    if (GetKeyState(VK_MBUTTON) & 0x8000) s |= 0x200;
    if (GetKeyState(VK_RBUTTON) & 0x8000) s |= 0x400;
    return s;
}

// Where the game's frame is in the client area (letterboxed unless
// [Display] KEEP_ASPECT_RATIO=0).
static void frameRect(XWin *w, int *x, int *y, int *fw, int *fh)
{
    *x = *y = 0;
    *fw = w->width;
    *fh = w->height;
    if (!w->fullscreen || !w->gameWidth || !w->gameHeight || (w->width == w->gameWidth && w->height == w->gameHeight))
    {
        *fw = w->gameWidth ? w->gameWidth : w->width;
        *fh = w->gameHeight ? w->gameHeight : w->height;
        return;
    }
    if (lxIniInt("Display", "KEEP_ASPECT_RATIO", 1))
    {
        if ((int64_t)w->width * w->gameHeight > (int64_t)w->height * w->gameWidth)
            *fw = (int)((int64_t)w->height * w->gameWidth / w->gameHeight);
        else
            *fh = (int)((int64_t)w->width * w->gameHeight / w->gameWidth);
        *x = (w->width - *fw) / 2;
        *y = (w->height - *fh) / 2;
    }
}

static XWin *presented;

int lxGamePointer(float *x, float *y)
{
    XWin *w = presented;
    POINT p;
    int fx, fy, fw, fh;
    if (!w || !GetCursorPos(&p) || !ScreenToClient(w->hwnd, &p))
        return 0;
    frameRect(w, &fx, &fy, &fw, &fh);
    if (fw <= 0 || fh <= 0)
        return 0;
    *x = (float)(p.x - fx) / (float)fw;
    *y = (float)(p.y - fy) / (float)fh;
    return *x >= 0.f && *x <= 1.f && *y >= 0.f && *y <= 1.f;
}

// Client coordinates to the game's.
static POINT toGame(XWin *w, POINT p)
{
    int x, y, fw, fh;
    frameRect(w, &x, &y, &fw, &fh);
    if (fw > 0 && fh > 0 && w->gameWidth && w->gameHeight)
    {
        p.x = (LONG)((int64_t)(p.x - x) * w->gameWidth / fw);
        p.y = (LONG)((int64_t)(p.y - y) * w->gameHeight / fh);
    }
    return p;
}

static void pointerEvent(XWin *w, int type, unsigned button, LPARAM lp)
{
    XEvent e;
    POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
    POINT r = p;
    initEvent(&e, type, w);
    ClientToScreen(w->hwnd, &r);
    p = toGame(w, p);
    e.key.root = ROOT_WINDOW;
    e.key.time = GetTickCount();
    e.key.x = p.x;
    e.key.y = p.y;
    e.key.xRoot = r.x;
    e.key.yRoot = r.y;
    e.key.state = modifierState();
    e.key.keycode = button;
    e.key.sameScreen = 1;
    enqueue(&e);
}

static void configureEvent(XWin *w)
{
    XEvent e;
    RECT rc;
    POINT p = {0, 0};
    GetClientRect(w->hwnd, &rc);
    ClientToScreen(w->hwnd, &p);
    initEvent(&e, ConfigureNotify, w);
    e.configure.event = w->id;
    e.configure.window = w->id;
    w->width = rc.right;
    w->height = rc.bottom;
    if (!w->fullscreen)
        w->gameWidth = w->width, w->gameHeight = w->height;
    e.configure.x = w->fullscreen ? 0 : p.x;
    e.configure.y = w->fullscreen ? 0 : p.y;
    e.configure.width = w->gameWidth;
    e.configure.height = w->gameHeight;
    enqueue(&e);
}

static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    XWin *w = windowOf(hwnd);
    XEvent e;
    if (!w)
        return DefWindowProcW(hwnd, msg, wp, lp);
    switch (msg)
    {
    case WM_CLOSE:
        lxLog("window closed\n");
        lxExit(0);
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        int down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN, key = linuxKey(lp);
        if (down && wp == VK_ESCAPE)
        {
            lxLog("quit from the keyboard\n");
            lxExit(0);
        }
        if (down && wp == VK_F4 && (GetKeyState(VK_MENU) & 0x8000))
        {
            lxLog("quit from the keyboard\n");
            lxExit(0);
        }
        // X's auto-repeat is left out: one press, one release.
        if (down && (lp & (1 << 30)))
            return 0;
        if (key)
        {
            POINT p;
            initEvent(&e, down ? KeyPress : KeyRelease, w);
            GetCursorPos(&p);
            e.key.root = ROOT_WINDOW;
            e.key.time = GetTickCount();
            e.key.xRoot = p.x;
            e.key.yRoot = p.y;
            ScreenToClient(hwnd, &p);
            e.key.x = p.x;
            e.key.y = p.y;
            e.key.state = modifierState();
            e.key.keycode = (unsigned)key + 8;
            e.key.sameScreen = 1;
            enqueue(&e);
        }
        return 0;
    }
    case WM_SYSCHAR:
        return 0;
    case WM_MOUSEMOVE:
        if (!mouseInside)
        {
            TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&t);
            mouseInside = 1;
            pointerEvent(w, EnterNotify, 0, lp);
        }
        pointerEvent(w, MotionNotify, 0, lp);
        return 0;
    case WM_MOUSELEAVE:
        mouseInside = 0;
        pointerEvent(w, LeaveNotify, 0, 0);
        return 0;
    case WM_LBUTTONDOWN: pointerEvent(w, ButtonPress, 1, lp); SetCapture(hwnd); return 0;
    case WM_LBUTTONUP: pointerEvent(w, ButtonRelease, 1, lp); ReleaseCapture(); return 0;
    case WM_MBUTTONDOWN: pointerEvent(w, ButtonPress, 2, lp); return 0;
    case WM_MBUTTONUP: pointerEvent(w, ButtonRelease, 2, lp); return 0;
    case WM_RBUTTONDOWN: pointerEvent(w, ButtonPress, 3, lp); return 0;
    case WM_RBUTTONUP: pointerEvent(w, ButtonRelease, 3, lp); return 0;
    case WM_MOUSEWHEEL:
    {
        POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
        unsigned b = (short)HIWORD(wp) > 0 ? 4 : 5;
        ScreenToClient(hwnd, &p);
        pointerEvent(w, ButtonPress, b, MAKELPARAM(p.x, p.y));
        pointerEvent(w, ButtonRelease, b, MAKELPARAM(p.x, p.y));
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT)
        {
            SetCursor(w->blankCursor ? NULL : LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_SIZE:
        if (w->mapped)
            configureEvent(w);
        return 0;
    case WM_MOVE:
        if (w->mapped)
            configureEvent(w);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        initEvent(&e, msg == WM_SETFOCUS ? FocusIn : FocusOut, w);
        e.focus.mode = 0;
        e.focus.detail = 3;   // NotifyNonlinear
        focusWindow = msg == WM_SETFOCUS ? hwnd : NULL;
        enqueue(&e);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        initEvent(&e, Expose, w);
        e.expose.width = w->width;
        e.expose.height = w->height;
        enqueue(&e);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void pump(void)
{
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
        DispatchMessageW(&m);
}

static void pumpWait(void)
{
    MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    pump();
}

// ---------------------------------------------------------------------------
// Keysyms: the US layout, evdev key codes (X's minus 8), plain and shifted.

static const struct { uint8_t key; uint32_t sym, shifted; } keymap[] = {
    {1, 0xff1b, 0xff1b}, {2, '1', '!'}, {3, '2', '@'}, {4, '3', '#'}, {5, '4', '$'}, {6, '5', '%'},
    {7, '6', '^'}, {8, '7', '&'}, {9, '8', '*'}, {10, '9', '('}, {11, '0', ')'}, {12, '-', '_'},
    {13, '=', '+'}, {14, 0xff08, 0xff08}, {15, 0xff09, 0xfe20}, {16, 'q', 'Q'}, {17, 'w', 'W'},
    {18, 'e', 'E'}, {19, 'r', 'R'}, {20, 't', 'T'}, {21, 'y', 'Y'}, {22, 'u', 'U'}, {23, 'i', 'I'},
    {24, 'o', 'O'}, {25, 'p', 'P'}, {26, '[', '{'}, {27, ']', '}'}, {28, 0xff0d, 0xff0d},
    {29, 0xffe3, 0xffe3}, {30, 'a', 'A'}, {31, 's', 'S'}, {32, 'd', 'D'}, {33, 'f', 'F'}, {34, 'g', 'G'},
    {35, 'h', 'H'}, {36, 'j', 'J'}, {37, 'k', 'K'}, {38, 'l', 'L'}, {39, ';', ':'}, {40, '\'', '"'},
    {41, '`', '~'}, {42, 0xffe1, 0xffe1}, {43, '\\', '|'}, {44, 'z', 'Z'}, {45, 'x', 'X'}, {46, 'c', 'C'},
    {47, 'v', 'V'}, {48, 'b', 'B'}, {49, 'n', 'N'}, {50, 'm', 'M'}, {51, ',', '<'}, {52, '.', '>'},
    {53, '/', '?'}, {54, 0xffe2, 0xffe2}, {55, 0xffaa, 0xffaa}, {56, 0xffe9, 0xffe9}, {57, ' ', ' '},
    {58, 0xffe5, 0xffe5}, {59, 0xffbe, 0xffbe}, {60, 0xffbf, 0xffbf}, {61, 0xffc0, 0xffc0},
    {62, 0xffc1, 0xffc1}, {63, 0xffc2, 0xffc2}, {64, 0xffc3, 0xffc3}, {65, 0xffc4, 0xffc4},
    {66, 0xffc5, 0xffc5}, {67, 0xffc6, 0xffc6}, {68, 0xffc7, 0xffc7}, {69, 0xff7f, 0xff7f},
    {70, 0xff14, 0xff14}, {71, 0xff95, 0xffb7}, {72, 0xff97, 0xffb8}, {73, 0xff9a, 0xffb9},
    {74, 0xffad, 0xffad}, {75, 0xff96, 0xffb4}, {76, 0xff9d, 0xffb5}, {77, 0xff98, 0xffb6},
    {78, 0xffab, 0xffab}, {79, 0xff9c, 0xffb1}, {80, 0xff99, 0xffb2}, {81, 0xff9b, 0xffb3},
    {82, 0xff9e, 0xffb0}, {83, 0xff9f, 0xffae}, {86, '<', '>'}, {87, 0xffc8, 0xffc8}, {88, 0xffc9, 0xffc9},
    {96, 0xff8d, 0xff8d}, {97, 0xffe4, 0xffe4}, {98, 0xffaf, 0xffaf}, {99, 0xff61, 0xff61},
    {100, 0xffea, 0xffea}, {102, 0xff50, 0xff50}, {103, 0xff52, 0xff52}, {104, 0xff55, 0xff55},
    {105, 0xff51, 0xff51}, {106, 0xff53, 0xff53}, {107, 0xff57, 0xff57}, {108, 0xff54, 0xff54},
    {109, 0xff56, 0xff56}, {110, 0xff63, 0xff63}, {111, 0xffff, 0xffff}, {119, 0xff13, 0xff13},
    {125, 0xffeb, 0xffeb}, {126, 0xffec, 0xffec}, {127, 0xff67, 0xff67},
};

static KeySym keycodeToSym(unsigned keycode, int level)
{
    for (size_t i = 0; i < LX_COUNT(keymap); i++)
        if (keymap[i].key + 8u == keycode)
            return level ? keymap[i].shifted : keymap[i].sym;
    return 0;
}

static unsigned symToKeycode(KeySym sym)
{
    for (size_t i = 0; i < LX_COUNT(keymap); i++)
        if (keymap[i].sym == sym)
            return keymap[i].key + 8u;
    for (size_t i = 0; i < LX_COUNT(keymap); i++)
        if (keymap[i].shifted == sym)
            return keymap[i].key + 8u;
    return 0;
}

int lxKeyHeld(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// ---------------------------------------------------------------------------
// Atoms.

#define MAX_ATOMS 512
static char *atomNames[MAX_ATOMS];
static int nAtoms = 69;   // the predefined ones (XA_*) below

static SYSV Atom xInternAtom(void *d, const char *name, int onlyIfExists)
{
    (void)d;
    static const char *const predefined[] = {[4] = "ATOM", [6] = "CARDINAL", [31] = "STRING", [39] = "WM_NAME",
                                             [40] = "WM_NORMAL_HINTS", [35] = "WM_HINTS", [67] = "WM_CLASS"};
    for (size_t i = 0; i < LX_COUNT(predefined); i++)
        if (predefined[i] && !strcmp(predefined[i], name))
            return i;
    for (int i = 69; i < nAtoms; i++)
        if (atomNames[i] && !strcmp(atomNames[i], name))
            return (Atom)i;
    if (onlyIfExists || nAtoms >= MAX_ATOMS)
        return 0;
    atomNames[nAtoms] = _strdup(name);
    return (Atom)nAtoms++;
}

static SYSV int xInternAtoms(void *d, char **names, int count, int onlyIfExists, Atom *out)
{
    for (int i = 0; i < count; i++)
        out[i] = xInternAtom(d, names[i], onlyIfExists);
    return 1;
}

static SYSV char *xGetAtomName(void *d, Atom a)
{
    (void)d;
    return lxStrdup(a < (Atom)nAtoms && atomNames[a] ? atomNames[a] : "ATOM");
}

// ---------------------------------------------------------------------------
// The display.

static SYSV void *xOpenDisplay(const char *name)
{
    static int opened;
    (void)name;
    if (!opened++)
    {
        DEVMODEW dm = {.dmSize = sizeof(dm)};
        EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm);
        visual.visualid = VISUAL_ID;
        visual.class_ = 4;   // TrueColor
        visual.redMask = 0xff0000;
        visual.greenMask = 0xff00;
        visual.blueMask = 0xff;
        visual.bitsPerRgb = 8;
        visual.mapEntries = 256;
        screen.display = &display;
        screen.root = ROOT_WINDOW;
        screen.width = (int)dm.dmPelsWidth;
        screen.height = (int)dm.dmPelsHeight;
        screen.mwidth = screen.width * 254 / 960;
        screen.mheight = screen.height * 254 / 960;
        screen.ndepths = 1;
        screen.rootDepth = 24;
        screen.rootVisual = &visual;
        screen.cmap = 0x20;
        screen.whitePixel = 0xffffff;
        screen.blackPixel = 0;
        display.fd = 1000;
        display.protoMajor = 11;
        display.vendor = "linuxloader64";
        display.byteOrder = 0;
        display.bitmapUnit = 32;
        display.bitmapPad = 32;
        display.release = 12000000;
        display.displayName = ":0";
        display.defaultScreen = 0;
        display.nscreens = 1;
        display.screens = &screen;
        display.minKeycode = 8;
        display.maxKeycode = 255;
        display.maxRequestSize = 65535;
        atomWmProtocols = xInternAtom(&display, "WM_PROTOCOLS", 0);
        atomWmDeleteWindow = xInternAtom(&display, "WM_DELETE_WINDOW", 0);
        atomNetWmState = xInternAtom(&display, "_NET_WM_STATE", 0);
        atomNetWmStateFullscreen = xInternAtom(&display, "_NET_WM_STATE_FULLSCREEN", 0);
        atomNetWmName = xInternAtom(&display, "_NET_WM_NAME", 0);
        atomWmName = 39;
        lxLog("X display opened: screen %dx%d\n", screen.width, screen.height);
    }
    return &display;
}

static SYSV int xZero(void) { return 0; }
static SYSV int xOne(void) { return 1; }
static SYSV void *xNull(void) { return NULL; }
static SYSV int xFree(void *p) { lxFree(p); return 1; }
static SYSV const char *xDisplayName(const char *n) { return n ? n : ":0"; }
static SYSV const char *xDisplayString(void *d) { (void)d; return ":0"; }

static void *errorHandler, *ioErrorHandler;
static SYSV void *xSetErrorHandler(void *h) { void *o = errorHandler; errorHandler = h; return o; }
static SYSV void *xSetIOErrorHandler(void *h) { void *o = ioErrorHandler; ioErrorHandler = h; return o; }

static SYSV int xPending(void *d)
{
    (void)d;
    pump();
    return qCount;
}

static SYSV int xEventsQueued(void *d, int mode)
{
    (void)d;
    if (mode)
        pump();
    return qCount;
}

static SYSV int xNextEvent(void *d, XEvent *e)
{
    (void)d;
    pump();
    while (!qCount)
        pumpWait();
    dequeueAt(0, e);
    return 0;
}

static SYSV int xPeekEvent(void *d, XEvent *e)
{
    (void)d;
    pump();
    while (!qCount)
        pumpWait();
    *e = queue[qHead];
    return 0;
}

typedef SYSV int (*Predicate)(void *, XEvent *, void *);

static int findIf(Predicate pred, void *arg, XEvent *e)
{
    for (int i = 0; i < qCount; i++)
        if (pred(&display, &queue[(qHead + i) % QUEUE_SIZE], arg))
        {
            dequeueAt(i, e);
            return 1;
        }
    return 0;
}

static SYSV int xIfEvent(void *d, XEvent *e, Predicate pred, void *arg)
{
    (void)d;
    pump();
    while (!findIf(pred, arg, e))
        pumpWait();
    return 0;
}

static SYSV int xCheckIfEvent(void *d, XEvent *e, Predicate pred, void *arg)
{
    (void)d;
    pump();
    return findIf(pred, arg, e);
}

static SYSV int xPeekIfEvent(void *d, XEvent *e, Predicate pred, void *arg)
{
    (void)d;
    pump();
    for (;;)
    {
        for (int i = 0; i < qCount; i++)
            if (pred(&display, &queue[(qHead + i) % QUEUE_SIZE], arg))
            {
                *e = queue[(qHead + i) % QUEUE_SIZE];
                return 0;
            }
        pumpWait();
    }
}

static SYSV int xCheckTypedWindowEvent(void *d, Window w, int type, XEvent *e)
{
    (void)d;
    pump();
    for (int i = 0; i < qCount; i++)
    {
        XEvent *q = &queue[(qHead + i) % QUEUE_SIZE];
        if (q->type == type && q->any.window == w)
        {
            dequeueAt(i, e);
            return 1;
        }
    }
    return 0;
}

static SYSV int xCheckWindowEvent(void *d, Window w, int64_t mask, XEvent *e)
{
    (void)d;
    (void)mask;
    pump();
    for (int i = 0; i < qCount; i++)
        if (queue[(qHead + i) % QUEUE_SIZE].any.window == w)
        {
            dequeueAt(i, e);
            return 1;
        }
    return 0;
}

static SYSV int xWindowEvent(void *d, Window w, int64_t mask, XEvent *e)
{
    while (!xCheckWindowEvent(d, w, mask, e))
        pumpWait();
    return 0;
}

static SYSV int xFilterEvent(XEvent *e, Window w) { (void)e; (void)w; return 0; }
static SYSV int xFlush(void *d) { (void)d; pump(); return 1; }
static SYSV int xSync(void *d, int discard) { (void)d; (void)discard; pump(); return 1; }

// ---------------------------------------------------------------------------
// Windows.

static void registerClass(void)
{
    static int done;
    WNDCLASSEXW wc = {sizeof(wc)};
    if (done++)
        return;
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(GetModuleHandleW(NULL), MAKEINTRESOURCE(1));
    wc.lpszClassName = L"linuxloader64";
    RegisterClassExW(&wc);
}

static int fullscreenSetting(void)
{
    return lxIniInt("Display", "FULLSCREEN", 0);
}

static void placeWindow(XWin *w)
{
    if (w->fullscreen)
    {
        HMONITOR mon = MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfoW(mon, &mi);
        SetWindowLongPtrW(w->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(w->hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    }
}

static SYSV Window xCreateWindow(void *d, Window parent, int x, int y, unsigned width, unsigned height,
                                 unsigned border, int depth, unsigned class_, void *vis, uint64_t mask, void *attrs)
{
    XWin *w = NULL;
    RECT rc = {0, 0, (LONG)width, (LONG)height};
    DWORD style = WS_OVERLAPPEDWINDOW;
    (void)d; (void)border; (void)depth; (void)class_; (void)vis; (void)mask; (void)attrs; (void)parent;
    for (int i = 0; i < MAX_WINDOWS && !w; i++)
        if (!windows[i].id)
            w = &windows[i];
    if (!w)
        return 0;
    registerClass();
    memset(w, 0, sizeof(*w));
    AdjustWindowRect(&rc, style, FALSE);
    w->id = nextWindowId++;
    w->width = w->gameWidth = (int)width;
    w->height = w->gameHeight = (int)height;
    w->hwnd = CreateWindowExW(0, L"linuxloader64", L"linuxloader64", style, CW_USEDEFAULT, CW_USEDEFAULT,
                              rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!w->hwnd)
    {
        lxLog("XCreateWindow: CreateWindow failed (error %lu)\n", GetLastError());
        w->id = 0;
        return 0;
    }
    w->dc = GetDC(w->hwnd);
    w->fullscreen = fullscreenSetting();
    lxLog("XCreateWindow %ux%u at %d,%d -> %#llx\n", width, height, x, y, (unsigned long long)w->id);
    return w->id;
}

static SYSV Window xCreateSimpleWindow(void *d, Window parent, int x, int y, unsigned width, unsigned height,
                                       unsigned border, uint64_t b, uint64_t bg)
{
    (void)b; (void)bg;
    return xCreateWindow(d, parent, x, y, width, height, border, 24, 1, NULL, 0, NULL);
}

static SYSV int xDestroyWindow(void *d, Window id)
{
    XWin *w = findWindow(id);
    (void)d;
    if (w)
    {
        ReleaseDC(w->hwnd, w->dc);
        DestroyWindow(w->hwnd);
        memset(w, 0, sizeof(*w));
    }
    return 1;
}

static SYSV int xMapWindow(void *d, Window id)
{
    XWin *w = findWindow(id);
    XEvent e;
    (void)d;
    if (!w)
        return 1;
    lxLog("XMapWindow %#llx (%dx%d)\n", (unsigned long long)id, w->width, w->height);
    placeWindow(w);
    ShowWindow(w->hwnd, SW_SHOW);
    SetForegroundWindow(w->hwnd);
    UpdateWindow(w->hwnd);
    w->mapped = 1;
    initEvent(&e, MapNotify, w);
    e.map.event = w->id;
    e.map.window = w->id;
    enqueue(&e);
    configureEvent(w);
    initEvent(&e, Expose, w);
    e.expose.width = w->width;
    e.expose.height = w->height;
    enqueue(&e);
    return 1;
}

static SYSV int xUnmapWindow(void *d, Window id)
{
    XWin *w = findWindow(id);
    XEvent e;
    (void)d;
    if (w)
    {
        ShowWindow(w->hwnd, SW_HIDE);
        w->mapped = 0;
        initEvent(&e, UnmapNotify, w);
        e.map.event = w->id;
        e.map.window = w->id;
        enqueue(&e);
    }
    return 1;
}

static SYSV int xIconifyWindow(void *d, Window id, int scr)
{
    XWin *w = findWindow(id);
    (void)d; (void)scr;
    if (w)
        ShowWindow(w->hwnd, SW_MINIMIZE);
    return 1;
}

static SYSV int xMoveResizeWindow(void *d, Window id, int x, int y, unsigned width, unsigned height)
{
    XWin *w = findWindow(id);
    RECT rc = {0, 0, (LONG)width, (LONG)height};
    (void)d;
    if (!w)
        return 1;
    if (w->fullscreen)
    {
        w->gameWidth = (int)width;
        w->gameHeight = (int)height;
        return 1;
    }
    AdjustWindowRect(&rc, (DWORD)GetWindowLongPtrW(w->hwnd, GWL_STYLE), FALSE);
    SetWindowPos(w->hwnd, NULL, x, y, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE |
                 (x == INT32_MIN ? SWP_NOMOVE : 0));
    return 1;
}

static SYSV int xResizeWindow(void *d, Window id, unsigned width, unsigned height)
{
    return xMoveResizeWindow(d, id, INT32_MIN, INT32_MIN, width, height);
}

static SYSV int xMoveWindow(void *d, Window id, int x, int y)
{
    XWin *w = findWindow(id);
    (void)d;
    if (w && !w->fullscreen)
        SetWindowPos(w->hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    return 1;
}

static SYSV int xRaiseWindow(void *d, Window id)
{
    XWin *w = findWindow(id);
    (void)d;
    if (w)
        SetForegroundWindow(w->hwnd);
    return 1;
}

static SYSV int xGetWindowAttributes(void *d, Window id, XWindowAttributes *a)
{
    XWin *w = findWindow(id);
    (void)d;
    memset(a, 0, sizeof(*a));
    a->depth = 24;
    a->visual = &visual;
    a->root = ROOT_WINDOW;
    a->class_ = 1;
    a->colormap = 0x20;
    a->screen = &screen;
    if (id == ROOT_WINDOW || !w)
    {
        a->width = screen.width;
        a->height = screen.height;
        a->mapState = 2;
        return 1;
    }
    {
        RECT rc;
        POINT p = {0, 0};
        GetClientRect(w->hwnd, &rc);
        ClientToScreen(w->hwnd, &p);
        a->x = w->fullscreen ? 0 : p.x;
        a->y = w->fullscreen ? 0 : p.y;
        a->width = w->fullscreen ? w->gameWidth : rc.right;
        a->height = w->fullscreen ? w->gameHeight : rc.bottom;
    }
    a->mapState = w->mapped ? 2 : 0;   // IsViewable / IsUnmapped
    a->yourEventMask = w->eventMask;
    return 1;
}

static SYSV int xGetGeometry(void *d, Drawable id, Window *root, int *x, int *y, unsigned *width, unsigned *height,
                             unsigned *border, unsigned *depth)
{
    XWindowAttributes a;
    xGetWindowAttributes(d, id, &a);
    *root = ROOT_WINDOW;
    *x = a.x;
    *y = a.y;
    *width = (unsigned)a.width;
    *height = (unsigned)a.height;
    *border = 0;
    *depth = 24;
    return 1;
}

static SYSV int xSelectInput(void *d, Window id, int64_t mask)
{
    XWin *w = findWindow(id);
    (void)d;
    if (w)
        w->eventMask = mask;
    return 1;
}

static SYSV int xTranslateCoordinates(void *d, Window src, Window dst, int x, int y, int *dx, int *dy, Window *child)
{
    XWin *s = findWindow(src), *t = findWindow(dst);
    POINT p = {x, y};
    (void)d;
    if (s)
        ClientToScreen(s->hwnd, &p);
    if (t)
        ScreenToClient(t->hwnd, &p);
    *dx = p.x;
    *dy = p.y;
    if (child)
        *child = 0;
    return 1;
}

static SYSV int xQueryPointer(void *d, Window id, Window *root, Window *child, int *rx, int *ry, int *wx, int *wy,
                              unsigned *mask)
{
    XWin *w = findWindow(id);
    POINT p;
    (void)d;
    GetCursorPos(&p);
    *root = ROOT_WINDOW;
    *child = 0;
    *rx = p.x;
    *ry = p.y;
    if (w)
        ScreenToClient(w->hwnd, &p);
    *wx = p.x;
    *wy = p.y;
    *mask = modifierState();
    return 1;
}

static SYSV int xWarpPointer(void *d, Window src, Window dst, int sx, int sy, unsigned sw, unsigned sh, int x, int y)
{
    XWin *w = findWindow(dst);
    POINT p = {x, y};
    (void)d; (void)src; (void)sx; (void)sy; (void)sw; (void)sh;
    if (w)
        ClientToScreen(w->hwnd, &p);
    SetCursorPos(p.x, p.y);
    return 1;
}

static SYSV int xGetInputFocus(void *d, Window *focus, int *revert)
{
    XWin *w = focusWindow ? windowOf(focusWindow) : NULL;
    (void)d;
    *focus = w ? w->id : 1;
    *revert = 0;
    return 1;
}

// Cursors: 1 is the blank one SDL makes to hide it, 2 the arrow.
static SYSV Cursor xCreatePixmapCursor(void *d, Pixmap s, Pixmap m, void *fg, void *bg, unsigned x, unsigned y)
{
    (void)d; (void)s; (void)m; (void)fg; (void)bg; (void)x; (void)y;
    return 1;
}
static SYSV Cursor xCreateFontCursor(void *d, unsigned shape) { (void)d; (void)shape; return 2; }
static SYSV int xDefineCursor(void *d, Window id, Cursor c)
{
    XWin *w = findWindow(id);
    (void)d;
    if (w)
    {
        w->blankCursor = c == 1;
        if (w->blankCursor)
            SetCursor(NULL);
    }
    return 1;
}
static SYSV int xUndefineCursor(void *d, Window id) { return xDefineCursor(d, id, 2); }
static SYSV Pixmap xCreatePixmap(void) { return 0x300; }

// Titles and the window manager's properties.
typedef struct
{
    unsigned char *value;
    Atom encoding;
    int format, pad;
    uint64_t nitems;
} XTextProperty;

static void setTitle(Window id, const char *title)
{
    XWin *w = findWindow(id);
    wchar_t wide[256];
    if (!w || !title)
        return;
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wide, 256);
    wide[255] = 0;
    SetWindowTextW(w->hwnd, wide);
}

static SYSV int xStoreName(void *d, Window id, const char *name) { (void)d; setTitle(id, name); return 1; }
static SYSV int xTextListToTextProperty(void *d, char **list, int count, int style, XTextProperty *p)
{
    (void)d; (void)style;
    p->value = (unsigned char *)lxStrdup(count > 0 && list[0] ? list[0] : "");
    p->encoding = 31;
    p->format = 8;
    p->nitems = strlen((char *)p->value);
    return 0;
}
static SYSV int xStringListToTextProperty(char **list, int count, XTextProperty *p)
{
    xTextListToTextProperty(NULL, list, count, 0, p);
    return 1;
}
static SYSV void xSetTextProperty(void *d, Window id, XTextProperty *p, Atom prop)
{
    (void)d;
    if (prop == atomWmName || prop == atomNetWmName)
        setTitle(id, (const char *)p->value);
}
static SYSV void xSetWMName(void *d, Window id, XTextProperty *p) { (void)d; setTitle(id, (const char *)p->value); }
static SYSV void xSetWMProperties(void *d, Window id, XTextProperty *name, XTextProperty *icon, char **argv, int argc,
                                  void *hints, void *wmHints, void *classHints)
{
    (void)d; (void)icon; (void)argv; (void)argc; (void)hints; (void)wmHints; (void)classHints;
    if (name)
        setTitle(id, (const char *)name->value);
}

static void setFullscreen(XWin *w, int on)
{
    if (!w || w->fullscreen == on)
        return;
    w->fullscreen = on;
    if (on)
        placeWindow(w);
    else
    {
        SetWindowLongPtrW(w->hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(w->hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
}

static SYSV int xChangeProperty(void *d, Window id, Atom prop, Atom type, int format, int mode,
                                const unsigned char *data, int n)
{
    (void)d; (void)type; (void)mode;
    if ((prop == atomNetWmName || prop == atomWmName) && format == 8 && data)
    {
        char t[256];
        snprintf(t, sizeof(t), "%.*s", n, (const char *)data);
        setTitle(id, t);
    }
    else if (prop == atomNetWmState && format == 32 && data)
    {
        int full = 0;
        for (int i = 0; i < n; i++)
            full |= ((const int64_t *)data)[i] == (int64_t)atomNetWmStateFullscreen;
        if (full)
            setFullscreen(findWindow(id), 1);
    }
    return 1;
}

// The window manager SDL looks for (_NET_SUPPORTING_WM_CHECK on the root and
// on its own window, then that window's _NET_WM_NAME): one that takes
// _NET_WM_STATE, so that SDL goes full screen through it.
#define WM_WINDOW 0x101

static SYSV int xGetWindowProperty(void *d, Window id, Atom prop, int64_t off, int64_t len, int del, Atom req,
                                   Atom *type, int *format, uint64_t *nitems, uint64_t *after, unsigned char **data)
{
    (void)d; (void)off; (void)len; (void)del; (void)req;
    *type = 0;
    *format = 0;
    *nitems = 0;
    *after = 0;
    *data = NULL;
    if (prop == xInternAtom(&display, "_NET_SUPPORTING_WM_CHECK", 0) && (id == ROOT_WINDOW || id == WM_WINDOW))
    {
        int64_t *v = lxMalloc(sizeof(int64_t));
        *v = WM_WINDOW;
        *type = 33;   // XA_WINDOW
        *format = 32;
        *nitems = 1;
        *data = (unsigned char *)v;
    }
    else if (prop == atomNetWmName && id == WM_WINDOW)
    {
        *type = xInternAtom(&display, "UTF8_STRING", 0);
        *format = 8;
        *data = (unsigned char *)lxStrdup("linuxloader64");
        *nitems = strlen((char *)*data);
    }
    return 0;
}

// Client messages: _NET_WM_STATE to the root (SDL's fullscreen toggle).
static SYSV int xSendEvent(void *d, Window id, int propagate, int64_t mask, XEvent *e)
{
    (void)d; (void)id; (void)propagate; (void)mask;
    if (e->type == ClientMessage && e->client.messageType == atomNetWmState)
    {
        // l[0]: 0 remove, 1 add, 2 toggle; l[1], l[2]: the states.
        XWin *w = findWindow(e->client.window);
        if (w && (e->client.l[1] == (int64_t)atomNetWmStateFullscreen ||
                  e->client.l[2] == (int64_t)atomNetWmStateFullscreen))
            setFullscreen(w, e->client.l[0] == 2 ? !w->fullscreen : (int)e->client.l[0]);
    }
    return 1;
}

// Visuals.
static void fillVisualInfo(XVisualInfo *v)
{
    memset(v, 0, sizeof(*v));
    v->visual = &visual;
    v->visualid = VISUAL_ID;
    v->depth = 24;
    v->class_ = 4;
    v->redMask = visual.redMask;
    v->greenMask = visual.greenMask;
    v->blueMask = visual.blueMask;
    v->colormapSize = 256;
    v->bitsPerRgb = 8;
}

static SYSV XVisualInfo *xGetVisualInfo(void *d, int64_t mask, XVisualInfo *tmpl, int *n)
{
    XVisualInfo *v;
    (void)d;
    if ((mask & 1) && tmpl->visualid != VISUAL_ID)   // VisualIDMask
    {
        *n = 0;
        return NULL;
    }
    v = lxMalloc(sizeof(*v));
    fillVisualInfo(v);
    *n = 1;
    return v;
}

static SYSV int xMatchVisualInfo(void *d, int scr, int depth, int class_, XVisualInfo *v)
{
    (void)d; (void)scr;
    fillVisualInfo(v);
    return depth == 24 && class_ == 4;
}

static SYSV VisualID xVisualIDFromVisual(XVisual *v) { return v ? v->visualid : VISUAL_ID; }

static SYSV void *xListPixmapFormats(void *d, int *n)
{
    int *f = lxMalloc(3 * sizeof(int));
    (void)d;
    f[0] = 24;
    f[1] = 32;
    f[2] = 32;
    *n = 1;
    return f;
}

static SYSV Colormap xCreateColormap(void *d, Window w, void *v, int alloc) { (void)d; (void)w; (void)v; (void)alloc; return 0x20; }

// The keyboard.
static SYSV int xDisplayKeycodes(void *d, int *min, int *max)
{
    (void)d;
    *min = 8;
    *max = 255;
    return 1;
}
static SYSV KeySym xKeycodeToKeysym(void *d, unsigned keycode, int index) { (void)d; return keycodeToSym(keycode & 0xff, index & 1); }
static SYSV KeySym xkbKeycodeToKeysym(void *d, unsigned keycode, int group, int level)
{
    (void)d; (void)group;
    return keycodeToSym(keycode & 0xff, level & 1);
}
static SYSV unsigned char xKeysymToKeycode(void *d, KeySym s) { (void)d; return (unsigned char)symToKeycode(s); }
static SYSV KeySym xLookupKeysym(XEvent *e, int index) { return keycodeToSym(e->key.keycode, index & 1); }
static SYSV int xLookupString(XEvent *e, char *buf, int n, KeySym *sym, void *status)
{
    KeySym s = keycodeToSym(e->key.keycode, (e->key.state & 1) != 0);
    (void)status;
    if (sym)
        *sym = s;
    if (s >= 0x20 && s < 0x7f && n > 0)
    {
        buf[0] = (char)s;
        return 1;
    }
    if ((s == 0xff0d || s == 0xff8d) && n > 0)
    {
        buf[0] = '\r';
        return 1;
    }
    return 0;
}

typedef struct
{
    int maxKeypermod, pad;
    uint8_t *modifiermap;
} XModifierKeymap;
static SYSV XModifierKeymap *xGetModifierMapping(void *d)
{
    XModifierKeymap *m = lxCalloc(1, sizeof(*m));
    (void)d;
    m->maxKeypermod = 1;
    m->modifiermap = lxCalloc(8, 1);
    m->modifiermap[0] = 50;   // Shift_L
    m->modifiermap[1] = 66;   // Caps_Lock
    m->modifiermap[2] = 37;   // Control_L
    m->modifiermap[3] = 64;   // Alt_L
    m->modifiermap[4] = 77;   // Num_Lock
    return m;
}
static SYSV int xFreeModifiermap(XModifierKeymap *m)
{
    if (m)
    {
        lxFree(m->modifiermap);
        lxFree(m);
    }
    return 1;
}

static SYSV int xQueryKeymap(void *d, char keys[32])
{
    (void)d;
    memset(keys, 0, 32);
    if (!lxGameFocused())
        return 1;
    for (size_t i = 0; i < LX_COUNT(keymap); i++)
    {
        unsigned key = keymap[i].key, sc = key, vk;
        if (key >= 96)
        {
            // The extended ones: back to their scan codes.
            static const struct { uint8_t key, sc; } ext[] = {
                {96, 0x1c}, {97, 0x1d}, {98, 0x35}, {99, 0x37}, {100, 0x38}, {102, 0x47}, {103, 0x48}, {104, 0x49},
                {105, 0x4b}, {106, 0x4d}, {107, 0x4f}, {108, 0x50}, {109, 0x51}, {110, 0x52}, {111, 0x53},
                {125, 0x5b}, {126, 0x5c}, {127, 0x5d}};
            sc = 0;
            for (size_t k = 0; k < LX_COUNT(ext); k++)
                if (ext[k].key == key)
                    sc = 0xe000 | ext[k].sc;
            if (!sc)
                continue;
        }
        vk = MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
        if (vk && (GetAsyncKeyState((int)vk) & 0x8000))
            keys[(key + 8) / 8] |= (char)(1 << ((key + 8) % 8));
    }
    return 1;
}

typedef struct
{
    int pad[2];
} XDummy;

static SYSV void *xAllocHints(void) { return lxCalloc(1, 128); }
static SYSV int xGrabSuccess(void) { return 0; }
static SYSV int xQueryExtension(void *d, const char *name, int *op, int *ev, int *err)
{
    (void)d;
    lxTraceLog("XQueryExtension %s: no\n", name);
    *op = *ev = *err = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// GLX on WGL.

typedef struct
{
    HGLRC rc;
} GlxContext;

static GlxContext *currentContext;
static Window currentDrawable;
static int wantDepth = 24, wantStencil = 8, wantAlpha = 8, wantSamples;
static BOOL(WINAPI *wglSwapIntervalEXT)(int);

void lxGlResolve(void);
void *lxGlProc(const char *name);

static int choosePixelFormat(HDC dc)
{
    PIXELFORMATDESCRIPTOR pfd = {sizeof(pfd), 1};
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cAlphaBits = (BYTE)wantAlpha;
    pfd.cDepthBits = (BYTE)wantDepth;
    pfd.cStencilBits = (BYTE)wantStencil;
    pfd.iLayerType = PFD_MAIN_PLANE;
    return ChoosePixelFormat(dc, &pfd);
}

static void preparePixelFormat(XWin *w)
{
    PIXELFORMATDESCRIPTOR pfd;
    if (w->pixelFormatSet)
        return;
    if (!pixelFormat)
        pixelFormat = choosePixelFormat(w->dc);
    DescribePixelFormat(w->dc, pixelFormat, sizeof(pfd), &pfd);
    if (!SetPixelFormat(w->dc, pixelFormat, &pfd))
        lxLog("SetPixelFormat failed (error %lu)\n", GetLastError());
    w->pixelFormatSet = 1;
}

// glXChooseVisual's attributes: the booleans have no value.
static SYSV XVisualInfo *glxChooseVisual(void *d, int scr, int *attrs)
{
    XVisualInfo *v;
    (void)d; (void)scr;
    for (int *a = attrs; a && *a; a++)
    {
        switch (*a)
        {
        case 4: case 5: case 6: case 1:   // GLX_RGBA, DOUBLEBUFFER, STEREO, USE_GL
            break;
        case 11: wantAlpha = a[1]; a++; break;
        case 12: wantDepth = a[1]; a++; break;
        case 13: wantStencil = a[1]; a++; break;
        case 100001: wantSamples = a[1]; a++; break;
        default: a++; break;
        }
    }
    v = lxMalloc(sizeof(*v));
    fillVisualInfo(v);
    lxLog("glXChooseVisual: depth %d stencil %d alpha %d samples %d\n", wantDepth, wantStencil, wantAlpha, wantSamples);
    return v;
}

static SYSV GlxContext *glxCreateContext(void *d, XVisualInfo *vis, GlxContext *share, int direct)
{
    XWin *w = NULL;
    GlxContext *c;
    (void)d; (void)vis; (void)direct;
    for (int i = MAX_WINDOWS - 1; i >= 0 && !w; i--)
        if (windows[i].id)
            w = &windows[i];
    if (!w)
    {
        // A context before any window: on one of its own, never shown.
        Window id = xCreateWindow(&display, ROOT_WINDOW, 0, 0, 64, 64, 0, 24, 1, NULL, 0, NULL);
        w = findWindow(id);
    }
    if (!w)
        return NULL;
    preparePixelFormat(w);
    c = calloc(1, sizeof(*c));
    c->rc = wglCreateContext(w->dc);
    if (!c->rc)
    {
        lxLog("wglCreateContext failed (error %lu)\n", GetLastError());
        free(c);
        return NULL;
    }
    if (share && share->rc && !wglShareLists(share->rc, c->rc))
        lxLog("wglShareLists failed (error %lu)\n", GetLastError());
    lxTraceLog("glXCreateContext share %p -> %p (thread %lu)\n", (void *)share, (void *)c, GetCurrentThreadId());
    return c;
}

static SYSV int glxMakeCurrent(void *d, Drawable id, GlxContext *c)
{
    XWin *w = findWindow(id);
    static int resolved;
    (void)d;
    if (!c)
    {
        wglMakeCurrent(NULL, NULL);
        currentContext = NULL;
        currentDrawable = 0;
        return 1;
    }
    if (!w)
        return 0;
    preparePixelFormat(w);
    if (!wglMakeCurrent(w->dc, c->rc))
    {
        lxLog("wglMakeCurrent failed (error %lu)\n", GetLastError());
        return 0;
    }
    if (c != currentContext || id != currentDrawable)
        lxTraceLog("glXMakeCurrent %#llx %p (thread %lu)\n", (unsigned long long)id, (void *)c, GetCurrentThreadId());
    currentContext = c;
    currentDrawable = id;
    if (!resolved++)
    {
        lxGlResolve();
        *(PROC *)&wglSwapIntervalEXT = wglGetProcAddress("wglSwapIntervalEXT");
        lxLog("GL: %s, %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    }
    return 1;
}

static SYSV void glxDestroyContext(void *d, GlxContext *c)
{
    (void)d;
    if (!c)
        return;
    if (c == currentContext)
        glxMakeCurrent(d, 0, NULL);
    wglDeleteContext(c->rc);
    free(c);
}

void (*lxSwapHook)(int width, int height);

// Full screen at another size than the game's: its frame (the lower left
// game size of the back buffer) copied to a texture and drawn over the whole
// client area.
static void scaleFrame(XWin *w)
{
    static void(APIENTRY * genFramebuffers)(GLsizei, GLuint *);
    static void(APIENTRY * bindFramebuffer)(GLenum, GLuint);
    static void(APIENTRY * framebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    static void(APIENTRY * blitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    static GLuint fbo, tex;
    static int texW, texH, failed;
    GLint drawFb = 0, readFb = 0, oldTex = 0, scissor;
    GLfloat clear[4];
    GLboolean mask[4];
    int x, y, fw, fh;

    if (!w->fullscreen || failed || (w->width == w->gameWidth && w->height == w->gameHeight) || !w->gameWidth)
        return;
    if (!fbo)
    {
        *(PROC *)&genFramebuffers = wglGetProcAddress("glGenFramebuffers");
        *(PROC *)&bindFramebuffer = wglGetProcAddress("glBindFramebuffer");
        *(PROC *)&framebufferTexture2D = wglGetProcAddress("glFramebufferTexture2D");
        *(PROC *)&blitFramebuffer = wglGetProcAddress("glBlitFramebuffer");
        if (!genFramebuffers || !bindFramebuffer || !framebufferTexture2D || !blitFramebuffer)
        {
            lxLog("no framebuffer objects: the frame is not scaled\n");
            failed = 1;
            return;
        }
        genFramebuffers(1, &fbo);
        glGenTextures(1, &tex);
        lxLog("the %dx%d frame scaled to %dx%d\n", w->gameWidth, w->gameHeight, w->width, w->height);
    }
    glGetIntegerv(0x8CA6, &drawFb);   // GL_DRAW_FRAMEBUFFER_BINDING
    glGetIntegerv(0x8CAA, &readFb);   // GL_READ_FRAMEBUFFER_BINDING
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (texW != w->gameWidth || texH != w->gameHeight)
    {
        texW = w->gameWidth;
        texH = w->gameHeight;
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        bindFramebuffer(0x8D40, fbo);   // GL_FRAMEBUFFER
        framebufferTexture2D(0x8D40, 0x8CE0, GL_TEXTURE_2D, tex, 0);   // COLOR_ATTACHMENT0
    }
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    bindFramebuffer(0x8CA8, 0);    // GL_READ_FRAMEBUFFER
    bindFramebuffer(0x8CA9, fbo);  // GL_DRAW_FRAMEBUFFER
    blitFramebuffer(0, 0, texW, texH, 0, 0, texW, texH, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    bindFramebuffer(0x8CA8, fbo);
    bindFramebuffer(0x8CA9, 0);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    frameRect(w, &x, &y, &fw, &fh);
    // GL's origin is the lower left.
    blitFramebuffer(0, 0, texW, texH, x, w->height - y - fh, x + fw, w->height - y, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    bindFramebuffer(0x8CA8, (GLuint)readFb);
    bindFramebuffer(0x8CA9, (GLuint)drawFb);
    glBindTexture(GL_TEXTURE_2D, (GLuint)oldTex);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    if (scissor)
        glEnable(GL_SCISSOR_TEST);
}

static SYSV void glxSwapBuffers(void *d, Drawable id)
{
    XWin *w = findWindow(id);
    (void)d;
    if (!w)
        return;
    static unsigned frames;
    static int glErrors = -1;
    if (glErrors < 0)
        glErrors = getenv("LL64_GLERR") != NULL;
    presented = w;
    // Drawn over the game's frame, before it is scaled to the window.
    if (lxSwapHook)
        lxSwapHook(w->gameWidth ? w->gameWidth : w->width, w->gameHeight ? w->gameHeight : w->height);
    if (glErrors)
    {
        static int shown;
        GLenum err = glGetError();
        if (err && shown++ < 20)
            lxLog("GL error %#x before frame %u\n", err, frames);
    }
    if (frames % 300 == 299)
    {
        void lxGlCountReport(void);
        lxGlCountReport();
    }
    scaleFrame(w);
    SwapBuffers(w->dc);
    if (!(frames++ % 3600))
        lxTraceLog("frame %u (%dx%d)\n", frames, w->width, w->height);
}

static SYSV void glxQueryDrawable(void *d, Drawable id, int attr, unsigned *value)
{
    XWin *w = findWindow(id);
    (void)d;
    *value = 0;
    if (!w)
        return;
    if (attr == 0x801d)
        *value = (unsigned)w->gameWidth;
    else if (attr == 0x801e)
        *value = (unsigned)w->gameHeight;
}

static SYSV int glxQueryExtension(void *d, int *err, int *ev)
{
    (void)d;
    if (err)
        *err = 0;
    if (ev)
        *ev = 0;
    return 1;
}
static SYSV int glxQueryVersion(void *d, int *major, int *minor)
{
    (void)d;
    *major = 1;
    *minor = 4;
    return 1;
}
static SYSV const char *glxQueryExtensionsString(void *d, int scr)
{
    (void)d; (void)scr;
    return "GLX_ARB_get_proc_address GLX_EXT_swap_control GLX_EXT_visual_info GLX_ARB_multisample";
}
static SYSV const char *glxGetClientString(void *d, int name)
{
    (void)d;
    return name == 1 ? "linuxloader64" : name == 2 ? "1.4" : glxQueryExtensionsString(d, 0);
}
static SYSV void glxSwapIntervalEXT(void *d, Drawable id, int interval)
{
    (void)d; (void)id;
    if (wglSwapIntervalEXT)
        wglSwapIntervalEXT(interval);
}
static SYSV int glxSwapIntervalMESA(unsigned interval)
{
    if (wglSwapIntervalEXT)
        wglSwapIntervalEXT((int)interval);
    return 0;
}
static SYSV void *glxGetCurrentContext(void) { return currentContext; }
static SYSV Drawable glxGetCurrentDrawable(void) { return currentDrawable; }
static SYSV void *glxGetCurrentDisplay(void) { return currentContext ? &display : NULL; }
static SYSV int glxGetConfig(void *d, XVisualInfo *v, int attr, int *value)
{
    (void)d; (void)v;
    switch (attr)
    {
    case 1: case 4: case 5: *value = 1; break;          // USE_GL, RGBA, DOUBLEBUFFER
    case 2: *value = 32; break;                         // BUFFER_SIZE
    case 8: case 9: case 10: *value = 8; break;         // RED/GREEN/BLUE_SIZE
    case 11: *value = wantAlpha; break;
    case 12: *value = wantDepth; break;
    case 13: *value = wantStencil; break;
    default: *value = 0;
    }
    return 0;
}

static SYSV void *glxGetProcAddress(const char *name);

static const LxSym glxSyms[] = {
    {"glXChooseVisual", glxChooseVisual}, {"glXCreateContext", glxCreateContext},
    {"glXDestroyContext", glxDestroyContext}, {"glXMakeCurrent", glxMakeCurrent},
    {"glXSwapBuffers", glxSwapBuffers}, {"glXQueryDrawable", glxQueryDrawable},
    {"glXQueryExtension", glxQueryExtension}, {"glXQueryVersion", glxQueryVersion},
    {"glXQueryExtensionsString", glxQueryExtensionsString}, {"glXGetClientString", glxGetClientString},
    {"glXQueryServerString", glxGetClientString}, {"glXSwapIntervalEXT", glxSwapIntervalEXT},
    {"glXSwapIntervalMESA", glxSwapIntervalMESA}, {"glXSwapIntervalSGI", glxSwapIntervalMESA},
    {"glXGetCurrentContext", glxGetCurrentContext}, {"glXGetCurrentDrawable", glxGetCurrentDrawable},
    {"glXGetCurrentDisplay", glxGetCurrentDisplay}, {"glXGetConfig", glxGetConfig},
    {"glXGetProcAddress", glxGetProcAddress}, {"glXGetProcAddressARB", glxGetProcAddress},
};

static SYSV void *glxGetProcAddress(const char *name)
{
    void *p;
    for (size_t i = 0; i < LX_COUNT(glxSyms); i++)
        if (!strcmp(glxSyms[i].name, name))
            return glxSyms[i].fn;
    p = lxGlProc(name);
    lxTraceLog("glXGetProcAddress %s -> %p\n", name, p);
    return p;
}

// ---------------------------------------------------------------------------

static const LxSym syms[] = {
    {"XOpenDisplay", xOpenDisplay}, {"XCloseDisplay", xZero}, {"XInitThreads", xOne}, {"XFree", xFree},
    {"XDisplayName", xDisplayName}, {"XDisplayString", xDisplayString}, {"XSetErrorHandler", xSetErrorHandler},
    {"XSetIOErrorHandler", xSetIOErrorHandler}, {"XInternAtom", xInternAtom}, {"XInternAtoms", xInternAtoms},
    {"XGetAtomName", xGetAtomName}, {"XPending", xPending}, {"XEventsQueued", xEventsQueued},
    {"XNextEvent", xNextEvent}, {"XPeekEvent", xPeekEvent}, {"XIfEvent", xIfEvent}, {"XCheckIfEvent", xCheckIfEvent},
    {"XPeekIfEvent", xPeekIfEvent}, {"XCheckTypedWindowEvent", xCheckTypedWindowEvent},
    {"XCheckWindowEvent", xCheckWindowEvent}, {"XWindowEvent", xWindowEvent}, {"XFilterEvent", xFilterEvent},
    {"XFlush", xFlush}, {"XSync", xSync}, {"XCreateWindow", xCreateWindow},
    {"XCreateSimpleWindow", xCreateSimpleWindow}, {"XDestroyWindow", xDestroyWindow}, {"XMapWindow", xMapWindow},
    {"XMapRaised", xMapWindow}, {"XUnmapWindow", xUnmapWindow}, {"XWithdrawWindow", xUnmapWindow},
    {"XIconifyWindow", xIconifyWindow}, {"XMoveResizeWindow", xMoveResizeWindow}, {"XResizeWindow", xResizeWindow},
    {"XMoveWindow", xMoveWindow}, {"XRaiseWindow", xRaiseWindow}, {"XSetInputFocus", xOne},
    {"XGetWindowAttributes", xGetWindowAttributes}, {"XGetGeometry", xGetGeometry}, {"XSelectInput", xSelectInput},
    {"XTranslateCoordinates", xTranslateCoordinates}, {"XQueryPointer", xQueryPointer},
    {"XWarpPointer", xWarpPointer}, {"XGetInputFocus", xGetInputFocus}, {"XGrabPointer", xGrabSuccess},
    {"XGrabKeyboard", xGrabSuccess}, {"XUngrabPointer", xOne}, {"XUngrabKeyboard", xOne}, {"XGrabServer", xOne},
    {"XUngrabServer", xOne}, {"XCreatePixmapCursor", xCreatePixmapCursor}, {"XCreateFontCursor", xCreateFontCursor},
    {"XDefineCursor", xDefineCursor}, {"XUndefineCursor", xUndefineCursor}, {"XFreeCursor", xOne},
    {"XCreateBitmapFromData", xCreatePixmap}, {"XCreatePixmap", xCreatePixmap}, {"XFreePixmap", xOne},
    {"XStoreName", xStoreName}, {"Xutf8TextListToTextProperty", xTextListToTextProperty},
    {"XmbTextListToTextProperty", xTextListToTextProperty}, {"XStringListToTextProperty", xStringListToTextProperty},
    {"XSetTextProperty", xSetTextProperty}, {"XSetWMName", xSetWMName}, {"XSetWMIconName", xOne},
    {"XSetWMProperties", xSetWMProperties}, {"Xutf8SetWMProperties", xSetWMProperties},
    {"XSetWMNormalHints", xOne}, {"XSetWMHints", xOne}, {"XSetClassHint", xOne}, {"XSetWMProtocols", xOne},
    {"XSetTransientForHint", xOne}, {"XChangeProperty", xChangeProperty}, {"XDeleteProperty", xOne},
    {"XGetWindowProperty", xGetWindowProperty}, {"XSendEvent", xSendEvent}, {"XGetVisualInfo", xGetVisualInfo},
    {"XMatchVisualInfo", xMatchVisualInfo}, {"XVisualIDFromVisual", xVisualIDFromVisual},
    {"XListPixmapFormats", xListPixmapFormats}, {"XCreateColormap", xCreateColormap}, {"XFreeColormap", xOne},
    {"XInstallColormap", xOne}, {"XUninstallColormap", xOne}, {"XStoreColors", xOne},
    {"XDisplayKeycodes", xDisplayKeycodes}, {"XKeycodeToKeysym", xKeycodeToKeysym},
    {"XkbKeycodeToKeysym", xkbKeycodeToKeysym}, {"XKeysymToKeycode", xKeysymToKeycode},
    {"XLookupKeysym", xLookupKeysym}, {"XLookupString", xLookupString}, {"XKeysymToString", xNull},
    {"XGetModifierMapping", xGetModifierMapping}, {"XFreeModifiermap", xFreeModifiermap},
    {"XQueryKeymap", xQueryKeymap}, {"XAutoRepeatOn", xOne}, {"XAutoRepeatOff", xOne},
    {"XAllocSizeHints", xAllocHints}, {"XAllocWMHints", xAllocHints}, {"XAllocClassHint", xAllocHints},
    {"XGetWMHints", xNull}, {"XGetWMNormalHints", xZero}, {"XQueryExtension", xQueryExtension},
    {"XResetScreenSaver", xOne}, {"XSuspendScreenSaver", xOne}, {"XOpenIM", xNull}, {"XCloseIM", xZero},
    {"XCreateIC", xNull}, {"XGetSelectionOwner", xZero}, {"XSynchronize", xNull}, {"XClearWindow", xOne},
    {"XReparentWindow", xOne}, {"XSetWindowBackground", xOne}, {"XMissingExtension", xZero},
    {"XGetErrorText", xZero}, {"XGetErrorDatabaseText", xZero}, {"XESetWireToEvent", xNull},
    {"XESetEventToWire", xNull}, {"XGetEventData", xZero}, {"XFreeEventData", xZero},
    {"XextAddDisplay", xNull}, {"XextFindDisplay", xNull}, {"XextCreateExtension", xNull},
    {"XextRemoveDisplay", xZero}, {"XextDestroyExtension", xZero}, {"XGetPointerControl", xZero},
    {"XChangePointerControl", xOne}, {"XFreeStringList", xZero},
};

void lxX11Init(void)
{
    lxRegister(syms, LX_COUNT(syms));
    lxRegister(glxSyms, LX_COUNT(glxSyms));
}
