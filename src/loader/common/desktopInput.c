// Desktop input of the PC-based arcade systems (Raw Thrills, Namco ES1 and
// N2, and the games that keep their own window, Roxor's): the keyboard, the
// mouse and (for the games that ask) the first SDL gamepad as a JVS state,
// and the Esc/Alt+F4 quit. The cabinet's keyboard is read from the X
// server's key map; on Windows, the desktop's keys, and the mouse over the
// game's window (the game's own SDL's: a Windows DLL's, the game runs in
// this process).

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#include <SDL3/SDL.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

#include "desktopInput.h"
#include "../graphics/frameScale.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

// The desktop keyboard, on keys given by keysym (or virtual key, on
// Windows) so that they are found on any layout: 1 or I start, 5 or U coin,
// 6 coin 2, F1 or P service, F2 test, arrows the joystick, Page Up/Down
// BUTTON_7/8.
typedef struct
{
    unsigned long key; // X11 keysym, or a Windows virtual key
    int player, bit;   // switch, or player -1: coin slot bit
} DesktopKey;

#ifdef _WIN32
// The letter virtual keys are the uppercase ASCII values (the lowercase
// values collide with unrelated keys: 'p' is VK_F1, 'i' the decimal point,
// ...).
static const DesktopKey keyMap[] = {
    {'1', PLAYER_1, BUTTON_START},
    {'I', PLAYER_1, BUTTON_START},
    {'2', PLAYER_2, BUTTON_START},
    {'O', PLAYER_2, BUTTON_START},
    {'5', -1, 0},                    // 5: coin 1
    {'U', -1, 0},                    // U: coin 1
    {'6', -1, 1},                    // 6: coin 2
    {VK_F1, PLAYER_1, BUTTON_SERVICE}, // F1
    {'P', PLAYER_1, BUTTON_SERVICE},
    {VK_SPACE, PLAYER_1, BUTTON_1},  // space: P1's button (a gun's trigger)
    {'Z', PLAYER_1, BUTTON_2},
    {'X', PLAYER_1, BUTTON_3},
    {'C', PLAYER_1, BUTTON_4},
    {'V', PLAYER_1, BUTTON_5},
    {VK_F2, SYSTEM, BUTTON_TEST},
    {VK_UP, PLAYER_1, BUTTON_UP},
    {VK_DOWN, PLAYER_1, BUTTON_DOWN},
    {VK_LEFT, PLAYER_1, BUTTON_LEFT},
    {VK_RIGHT, PLAYER_1, BUTTON_RIGHT},
    {VK_PRIOR, PLAYER_1, BUTTON_7},  // Page Up
    {VK_NEXT, PLAYER_1, BUTTON_8},   // Page Down
    {'W', PLAYER_2, BUTTON_UP},      // W: opt-in P2 keyboard directions
    {'S', PLAYER_2, BUTTON_DOWN},
    {'A', PLAYER_2, BUTTON_LEFT},
    {'D', PLAYER_2, BUTTON_RIGHT},
    {'F', PLAYER_2, BUTTON_1},       // F: opt-in P2 action buttons
    {'G', PLAYER_2, BUTTON_2},
};
#else
static const DesktopKey keyMap[] = {
    {0x31, PLAYER_1, BUTTON_START},    // 1
    {0x69, PLAYER_1, BUTTON_START},    // i
    {0x32, PLAYER_2, BUTTON_START},    // 2
    {0x6f, PLAYER_2, BUTTON_START},    // o
    {0x35, -1, 0},                     // 5: coin 1
    {0x75, -1, 0},                     // u: coin 1
    {0x36, -1, 1},                     // 6: coin 2
    {0xffbe, PLAYER_1, BUTTON_SERVICE}, // F1
    {0x70, PLAYER_1, BUTTON_SERVICE},  // p
    {0x20, PLAYER_1, BUTTON_1},        // space: P1's button (a gun's trigger)
    {0x7a, PLAYER_1, BUTTON_2},        // z
    {0x78, PLAYER_1, BUTTON_3},        // x
    {0x63, PLAYER_1, BUTTON_4},        // c
    {0x76, PLAYER_1, BUTTON_5},        // v
    {0xffbf, SYSTEM, BUTTON_TEST},     // F2
    {0xff52, PLAYER_1, BUTTON_UP},
    {0xff54, PLAYER_1, BUTTON_DOWN},
    {0xff51, PLAYER_1, BUTTON_LEFT},
    {0xff53, PLAYER_1, BUTTON_RIGHT},
    {0xff55, PLAYER_1, BUTTON_7},      // Page Up
    {0xff56, PLAYER_1, BUTTON_8},      // Page Down
    {0x77, PLAYER_2, BUTTON_UP},       // w: opt-in P2 keyboard directions
    {0x73, PLAYER_2, BUTTON_DOWN},     // s
    {0x61, PLAYER_2, BUTTON_LEFT},     // a
    {0x64, PLAYER_2, BUTTON_RIGHT},    // d
    {0x66, PLAYER_2, BUTTON_1},        // f: opt-in P2 action buttons
    {0x67, PLAYER_2, BUTTON_2},        // g
};
#endif

#define DESKTOP_KEYS (sizeof(keyMap) / sizeof(keyMap[0]))

// The coin counting and the pointer, after the keyboard's switches.
static JVSIO *finishInputState(JVSIO *io, int coin[2], int coinHeld[2]);

// The SDL gamepads (desktopGamepadEnable): SDL 3's gamepad functions, the
// loader's own (see loadGamepadSdl).
static struct
{
    bool (*InitSubSystem)(SDL_InitFlags);
    SDL_JoystickID *(*GetGamepads)(int *);
    SDL_Gamepad *(*OpenGamepad)(SDL_JoystickID);
    void (*CloseGamepad)(SDL_Gamepad *);
    bool (*GamepadConnected)(SDL_Gamepad *);
    void (*UpdateGamepads)(void);
    Sint16 (*GetGamepadAxis)(SDL_Gamepad *, SDL_GamepadAxis);
    bool (*GetGamepadButton)(SDL_Gamepad *, SDL_GamepadButton);
    void (*free)(void *);
} sdl;
static int twoPlayerKeyboard;

// The mouse over the game's window is player 1's gun: its position on the
// picture (which is scaled into the window once the window is resized, see
// frameScale.h) as analogue channels 1 and 2 (from the top left, 0 when
// outside), left button BUTTON_1, right BUTTON_2, middle BUTTON_3 (or the
// game's choice, desktopPointerMiddle).
#define DESKTOP_ANALOGUE_MAX 0xffff

static unsigned int pointerButtons;
static int pointerMiddleBit = BUTTON_3;
static int pointerWindowWidth, pointerWindowHeight;

void desktopPointerMiddle(int bit)
{
    pointerMiddleBit = bit;
}

unsigned int desktopPointerButtons(void)
{
    return pointerButtons;
}

int desktopPointerWindowSize(int *width, int *height)
{
    *width = pointerWindowWidth;
    *height = pointerWindowHeight;
    return pointerWindowWidth > 1 && pointerWindowHeight > 1;
}

#ifdef _WIN32
// Esc or Alt+F4 quits, in every game: a thread watches the keyboard, only
// while the foreground window belongs to the game (the game runs in this
// process).
static DWORD WINAPI quitWatch(LPVOID arg)
{
    (void)arg;
    for (;;)
    {
        Sleep(50);
        if (!(GetAsyncKeyState(VK_ESCAPE) & 0x8000) &&
            !((GetAsyncKeyState(VK_F4) & 0x8000) && (GetAsyncKeyState(VK_MENU) & 0x8000)))
            continue;
        // GetWindowThreadProcessId returns the thread id; the process id
        // comes through its second argument.
        HWND fg = GetForegroundWindow();
        DWORD pid;
        if (!fg || GetWindowThreadProcessId(fg, &pid) == 0 || pid != GetCurrentProcessId())
            continue;
        log_info("Desktop input: quit from the keyboard");
        ExitProcess(0);
    }
    return 0;
}

void desktopStartQuitWatch(void)
{
    CreateThread(NULL, 0, quitWatch, NULL, 0, NULL);
}

static void desktopPointer(JVSIO *io)
{
    HWND window;
    RECT rect;
    POINT pt;
    DWORD pid;
    int w, h, x, y;

    io->analogueMax = DESKTOP_ANALOGUE_MAX;
    io->state.analogueChannel[ANALOGUE_1] = io->state.analogueChannel[ANALOGUE_2] = 0;
    pointerButtons = 0;
    pointerWindowWidth = pointerWindowHeight = 0;
    window = GetForegroundWindow();
    if (!window || GetWindowThreadProcessId(window, &pid) == 0 || pid != GetCurrentProcessId() || !GetClientRect(window, &rect))
        return;
    // The client area, not the window rect (which adds the title bar and
    // borders): the picture is scaled into it.
    w = rect.right - rect.left;
    h = rect.bottom - rect.top;
    pointerWindowWidth = w;
    pointerWindowHeight = h;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        pointerButtons |= DESKTOP_POINTER_LEFT;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
        pointerButtons |= DESKTOP_POINTER_RIGHT;
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000)
        pointerButtons |= DESKTOP_POINTER_MIDDLE;
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        io->state.inputSwitch[PLAYER_1] |= BUTTON_1;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
        io->state.inputSwitch[PLAYER_1] |= BUTTON_2;
    if (GetAsyncKeyState(VK_MBUTTON) & 0x8000)
        io->state.inputSwitch[PLAYER_1] |= pointerMiddleBit;
    // The cursor in client coordinates (rect is the client area, its origin
    // (0,0), so the client-to-screen offset is not the window's).
    if (w <= 1 || h <= 1 || !GetCursorPos(&pt) || !ScreenToClient(window, &pt))
        return;
    x = pt.x;
    y = pt.y;
    if (x < 0 || y < 0 || x >= w || y >= h)
        return;
    float px = (float)x / (w - 1), py = (float)y / (h - 1);
    if (!frameScaleWindowToGame(&px, &py))
        return;
    io->state.analogueChannel[ANALOGUE_1] = 1 + (int)(px * (DESKTOP_ANALOGUE_MAX - 2));
    io->state.analogueChannel[ANALOGUE_2] = 1 + (int)(py * (DESKTOP_ANALOGUE_MAX - 2));
}

// The loader's own SDL 3, statically linked (the game's SDL 1.2 is a
// Windows DLL, its names not in this process's scope).
static int loadGamepadSdl(void)
{
    sdl.InitSubSystem = SDL_InitSubSystem;
    sdl.GetGamepads = SDL_GetGamepads;
    sdl.OpenGamepad = SDL_OpenGamepad;
    sdl.CloseGamepad = SDL_CloseGamepad;
    sdl.GamepadConnected = SDL_GamepadConnected;
    sdl.UpdateGamepads = SDL_UpdateGamepads;
    sdl.GetGamepadAxis = SDL_GetGamepadAxis;
    sdl.GetGamepadButton = SDL_GetGamepadButton;
    sdl.free = SDL_free;
    return 1;
}

JVSIO *desktopInputState(void)
{
    static JVSIO io;
    static int coinHeld[2];
    static DWORD lastTick;
    int coin[2] = {0, 0};

    // The switches are read one by one each frame: poll the keys at most
    // once every 4 ms.
    DWORD tick = GetTickCount();
    if (tick - lastTick < 4)
        return &io;
    lastTick = tick;
    for (int p = 0; p < 3; p++)
        io.state.inputSwitch[p] = 0;
    for (size_t i = 0; i < DESKTOP_KEYS; i++)
    {
        const DesktopKey *k = &keyMap[i];
        if (!twoPlayerKeyboard && k->player == PLAYER_2 && k->bit != BUTTON_START)
            continue;
        if (!(GetAsyncKeyState((SHORT)k->key) & 0x8000))
            continue;
        if (k->player < 0)
            coin[k->bit] = 1;
        else
            io.state.inputSwitch[k->player] |= k->bit;
    }
    return finishInputState(&io, coin, coinHeld);
}
#else // _WIN32
// Our own X connection, for the keyboard state.
static void *keyDisplay;
static int (*queryKeymap)(void *, char[32]);
static unsigned char (*keysymToKeycode)(void *, unsigned long);

static int openKeyboard(void)
{
    if (!keyDisplay)
    {
        void *(*openDisplay)(const char *) = dlsym(RTLD_NEXT, "XOpenDisplay");
        keysymToKeycode = dlsym(RTLD_NEXT, "XKeysymToKeycode");
        queryKeymap = dlsym(RTLD_NEXT, "XQueryKeymap");
        if (!openDisplay || !keysymToKeycode || !queryKeymap || !(keyDisplay = openDisplay(NULL)))
        {
            log_warn("Desktop input: no X display, keyboard input disabled");
            keyDisplay = (void *)-1;
        }
    }
    return keyDisplay != (void *)-1;
}

static int keyHeld(const char keys[32], unsigned char keycode)
{
    return keycode && (keys[keycode / 8] & (1 << (keycode % 8)));
}

// Esc or Alt+F4 quits, in every game: a thread watches the keyboard, with
// its own X connection. The X server reports keys pressed in any window:
// only while the focused window belongs to the game (_NET_WM_PID, set by
// SDL 2 and 3; the glut games quit from their window's key callbacks). The
// windows of SDL 1.2 (America's Army's, without sdl12-compat) and of plain
// Xlib carry no _NET_WM_PID: their process is the X server's answer (the
// X-Resource extension, libXRes), when it has one.
typedef struct
{
    unsigned long client;
    unsigned int mask;
} XResClientIdSpec;

typedef struct
{
    XResClientIdSpec spec;
    long length;
    void *value;
} XResClientIdValue;

#define XRES_CLIENT_ID_PID_MASK 2

static struct
{
    int (*queryClientIds)(void *, long, XResClientIdSpec *, long *, XResClientIdValue **);
    pid_t (*getClientPid)(XResClientIdValue *);
    void (*clientIdsDestroy)(long, XResClientIdValue *);
} xres;

static void loadXRes(void)
{
    void *lib = dlopen("libXRes.so.1", RTLD_NOW);
    if (lib)
    {
        *(void **)&xres.queryClientIds = dlsym(lib, "XResQueryClientIds");
        *(void **)&xres.getClientPid = dlsym(lib, "XResGetClientPid");
        *(void **)&xres.clientIdsDestroy = dlsym(lib, "XResClientIdsDestroy");
    }
    if (!xres.queryClientIds || !xres.getClientPid || !xres.clientIdsDestroy)
        xres.queryClientIds = NULL;
}

// The process of the X client owning window, -1 if unknown.
static pid_t windowPid(void *display, unsigned long window)
{
    XResClientIdSpec spec = {window, XRES_CLIENT_ID_PID_MASK};
    XResClientIdValue *ids = NULL;
    long count = 0;
    pid_t pid = -1;

    if (!xres.queryClientIds || xres.queryClientIds(display, 1, &spec, &count, &ids) != 0)
        return -1;
    for (long i = 0; i < count && pid < 0; i++)
        pid = xres.getClientPid(&ids[i]);
    xres.clientIdsDestroy(count, ids);
    return pid;
}

typedef struct
{
    void *(*openDisplay)(const char *);
    int (*queryKeymap)(void *, char[32]);
    unsigned char (*keysymToKeycode)(void *, unsigned long);
    int (*getInputFocus)(void *, unsigned long *, int *);
    int (*queryTree)(void *, unsigned long, unsigned long *, unsigned long *, unsigned long **, unsigned int *);
    unsigned long (*internAtom)(void *, const char *, int);
    int (*getWindowProperty)(void *, unsigned long, unsigned long, long, long, int, unsigned long, unsigned long *,
                             int *, unsigned long *, unsigned long *, unsigned char **);
    int (*freeData)(void *);
} Xlib;

// The focused window, or one of its parents, is a window of this process.
static int gameFocused(const Xlib *x, void *display, unsigned long pidAtom)
{
    unsigned long window, root, parent, *children;
    unsigned int nchildren;
    int revert;

    x->getInputFocus(display, &window, &revert);
    // None or PointerRoot (gamescope): no other X window has the keyboard.
    if (window <= 1)
        return 1;
    for (int depth = 0; window > 1 && depth < 16; depth++)
    {
        unsigned long type, nitems, after;
        unsigned char *data = NULL;
        int format;

        if (x->getWindowProperty(display, window, pidAtom, 0, 1, 0, 6 /* XA_CARDINAL */, &type, &format, &nitems,
                                 &after, &data) == 0 &&
            data)
        {
            // Xlib returns 32-bit properties as longs.
            int ours = nitems == 1 && (pid_t)*(unsigned long *)data == getpid();
            x->freeData(data);
            return ours;
        }
        // The window manager's frame is another client's: its parent next.
        if (windowPid(display, window) == getpid())
            return 1;
        if (!x->queryTree(display, window, &root, &parent, &children, &nchildren))
            break;
        if (children)
            x->freeData(children);
        if (parent == root)
            break;
        window = parent;
    }
    return 0;
}

static void *quitWatch(void *arg)
{
    Xlib x = {
        dlsym(RTLD_NEXT, "XOpenDisplay"),     dlsym(RTLD_NEXT, "XQueryKeymap"),
        dlsym(RTLD_NEXT, "XKeysymToKeycode"), dlsym(RTLD_NEXT, "XGetInputFocus"),
        dlsym(RTLD_NEXT, "XQueryTree"),       dlsym(RTLD_NEXT, "XInternAtom"),
        dlsym(RTLD_NEXT, "XGetWindowProperty"), dlsym(RTLD_NEXT, "XFree"),
    };
    void *display;

    (void)arg;
    if (!x.openDisplay || !x.queryKeymap || !x.keysymToKeycode || !x.getInputFocus || !x.queryTree ||
        !x.internAtom || !x.getWindowProperty || !x.freeData || !(display = x.openDisplay(NULL)))
    {
        log_warn("Desktop input: no X display, Esc/Alt+F4 quit disabled");
        return NULL;
    }
    unsigned char escape = x.keysymToKeycode(display, 0xff1b), f4 = x.keysymToKeycode(display, 0xffc1),
                  altL = x.keysymToKeycode(display, 0xffe9), altR = x.keysymToKeycode(display, 0xffea);
    unsigned long pidAtom = x.internAtom(display, "_NET_WM_PID", 0);
    loadXRes();

    for (;;)
    {
        char keys[32];

        usleep(50000);
        x.queryKeymap(display, keys);
        if (!keyHeld(keys, escape) && !(keyHeld(keys, f4) && (keyHeld(keys, altL) || keyHeld(keys, altR))))
            continue;
        if (!gameFocused(&x, display, pidAtom))
            continue;
        log_info("Desktop input: quit from the keyboard");
        _exit(0);
    }
    return NULL;
}

void desktopStartQuitWatch(void)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, quitWatch, NULL) == 0)
        pthread_detach(thread);
}

static void desktopPointer(JVSIO *io)
{
    static int (*getInputFocus)(void *, unsigned long *, int *);
    static int (*queryPointer)(void *, unsigned long, unsigned long *, unsigned long *, int *, int *, int *, int *,
                               unsigned int *);
    static int (*getWindowAttributes)(void *, unsigned long, int *);
    unsigned long window, root, child;
    int revert, rx, ry, x, y, attributes[64];
    unsigned int mask;

    if (!getInputFocus)
    {
        getInputFocus = dlsym(RTLD_NEXT, "XGetInputFocus");
        queryPointer = dlsym(RTLD_NEXT, "XQueryPointer");
        getWindowAttributes = dlsym(RTLD_NEXT, "XGetWindowAttributes");
    }
    io->analogueMax = DESKTOP_ANALOGUE_MAX;
    io->state.analogueChannel[ANALOGUE_1] = io->state.analogueChannel[ANALOGUE_2] = 0;
    pointerButtons = 0;
    if (!getInputFocus || !queryPointer || !getWindowAttributes)
        return;
    getInputFocus(keyDisplay, &window, &revert);
    // Window attributes: x, y, width, height first.
    if (window <= 1 || !getWindowAttributes(keyDisplay, window, attributes) ||
        !queryPointer(keyDisplay, window, &root, &child, &rx, &ry, &x, &y, &mask))
        return;
    int w = attributes[2], h = attributes[3];
    pointerWindowWidth = w;
    pointerWindowHeight = h;
    pointerButtons = (mask & (1 << 8) ? DESKTOP_POINTER_LEFT : 0) | (mask & (1 << 10) ? DESKTOP_POINTER_RIGHT : 0) |
                     (mask & (1 << 9) ? DESKTOP_POINTER_MIDDLE : 0);
    if (mask & (1 << 8))
        io->state.inputSwitch[PLAYER_1] |= BUTTON_1;
    if (mask & (1 << 10))
        io->state.inputSwitch[PLAYER_1] |= BUTTON_2;
    if (mask & (1 << 9))
        io->state.inputSwitch[PLAYER_1] |= pointerMiddleBit;
    if (w <= 1 || h <= 1 || x < 0 || y < 0 || x >= w || y >= h)
        return;
    float px = (float)x / (w - 1), py = (float)y / (h - 1);
    if (!frameScaleWindowToGame(&px, &py))
        return;
    io->state.analogueChannel[ANALOGUE_1] = 1 + (int)(px * (DESKTOP_ANALOGUE_MAX - 2));
    io->state.analogueChannel[ANALOGUE_2] = 1 + (int)(py * (DESKTOP_ANALOGUE_MAX - 2));
}

// SDL3 through its own library handle (a game's SDL 1.2 shadows its names
// in the global scope).
static int loadGamepadSdl(void)
{
    void *h;
    if (!(h = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_NOLOAD)) && !(h = dlopen("libSDL3.so.0", RTLD_NOW)))
    {
        log_warn("Desktop input: libSDL3.so.0 not found, no gamepad: %s", dlerror());
        return 0;
    }
#define LOAD(name) *(void **)&sdl.name = dlsym(h, "SDL_" #name)
    LOAD(InitSubSystem);
    LOAD(GetGamepads);
    LOAD(OpenGamepad);
    LOAD(CloseGamepad);
    LOAD(GamepadConnected);
    LOAD(UpdateGamepads);
    LOAD(GetGamepadAxis);
    LOAD(GetGamepadButton);
    LOAD(free);
#undef LOAD
    return sdl.InitSubSystem && sdl.GetGamepads && sdl.OpenGamepad && sdl.CloseGamepad && sdl.GamepadConnected &&
           sdl.UpdateGamepads && sdl.GetGamepadAxis && sdl.GetGamepadButton && sdl.free;
}

JVSIO *desktopInputState(void)
{
    static JVSIO io;
    static int mapped;
    static unsigned char keycodes[DESKTOP_KEYS];
    static int coinHeld[2];
    static struct timespec last;
    struct timespec now;
    char keys[32];
    int coin[2] = {0, 0};

    if (!openKeyboard())
        return &io;
    if (!mapped)
    {
        for (size_t i = 0; i < DESKTOP_KEYS; i++)
            keycodes[i] = keysymToKeycode(keyDisplay, keyMap[i].key);
        mapped = 1;
    }
    // The switches are read one by one each frame: poll the keyboard once.
    clock_gettime(CLOCK_MONOTONIC, &now);
    if ((now.tv_sec - last.tv_sec) * 1000000000LL + (now.tv_nsec - last.tv_nsec) < 4000000LL)
        return &io;
    last = now;
    queryKeymap(keyDisplay, keys);
    for (int p = 0; p < 3; p++)
        io.state.inputSwitch[p] = 0;
    for (size_t i = 0; i < DESKTOP_KEYS; i++)
    {
        const DesktopKey *k = &keyMap[i];
        if (!twoPlayerKeyboard && k->player == PLAYER_2 && k->bit != BUTTON_START)
            continue;
        if (!keyHeld(keys, keycodes[i]))
            continue;
        if (k->player < 0)
            coin[k->bit] = 1;
        else
            io.state.inputSwitch[k->player] |= k->bit;
    }
    return finishInputState(&io, coin, coinHeld);
}
#endif // _WIN32

static int gamepadEnabled, twoPlayerGamepads;
static SDL_Gamepad *gamepads[2];

void desktopGamepadEnable(void)
{
    if (gamepadEnabled)
        return;
    if (!loadGamepadSdl() || !sdl.InitSubSystem(SDL_INIT_GAMEPAD))
    {
        log_warn("Desktop input: no SDL gamepad support");
        return;
    }
    gamepadEnabled = 1;
}

void desktopGamepadEnableTwoPlayers(void)
{
    desktopGamepadEnable();
    twoPlayerGamepads = 1;
}

void desktopKeyboardEnableTwoPlayers(void)
{
    twoPlayerKeyboard = 1;
}

// The gamepad, opened again (once a second) when there is none.
static SDL_Gamepad *currentGamepad(int player)
{
    static time_t lastTry[2];
    time_t now = time(NULL);

    if (!gamepadEnabled || player < 0 || player > 1 || (player == 1 && !twoPlayerGamepads))
        return NULL;
    sdl.UpdateGamepads();
    if (gamepads[player] && !sdl.GamepadConnected(gamepads[player]))
    {
        sdl.CloseGamepad(gamepads[player]);
        gamepads[player] = NULL;
    }
    if (!gamepads[player] && now != lastTry[player])
    {
        int count = 0;
        SDL_JoystickID *ids = sdl.GetGamepads(&count);
        lastTry[player] = now;
        if (ids && count > player && (gamepads[player] = sdl.OpenGamepad(ids[player])))
            log_info("Desktop input: player %d gamepad connected", player + 1);
        if (ids)
            sdl.free(ids);
    }
    return gamepads[player];
}

int desktopGamepadAxis(int axis)
{
    SDL_Gamepad *pad = currentGamepad(0);
    return pad ? sdl.GetGamepadAxis(pad, (SDL_GamepadAxis)axis) : 0;
}

// The first SDL gamepad (desktopGamepadEnable): Start starts, Back is coin
// 1, A (south) BUTTON_1, the d-pad the joystick, R3 the test switch, L3
// service; its axes are read with desktopGamepadAxis.
static void gamepadSwitches(JVSIO *io, int coin[2])
{
    static const struct
    {
        SDL_GamepadButton button;
        int player, bit;
    } map[] = {
        {SDL_GAMEPAD_BUTTON_START, PLAYER_1, BUTTON_START},       {SDL_GAMEPAD_BUTTON_SOUTH, PLAYER_1, BUTTON_1},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, PLAYER_1, BUTTON_UP},        {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PLAYER_1, BUTTON_DOWN},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PLAYER_1, BUTTON_LEFT},    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PLAYER_1, BUTTON_RIGHT},
        {SDL_GAMEPAD_BUTTON_RIGHT_STICK, SYSTEM, BUTTON_TEST},    {SDL_GAMEPAD_BUTTON_LEFT_STICK, PLAYER_1, BUTTON_SERVICE},
    };
    for (int player = 0; player < (twoPlayerGamepads ? 2 : 1); player++)
    {
        SDL_Gamepad *pad = currentGamepad(player);
        if (!pad)
            continue;
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
            if ((map[i].player == PLAYER_1 || (player == 0 && map[i].player == SYSTEM)) &&
                sdl.GetGamepadButton(pad, map[i].button))
                io->state.inputSwitch[map[i].player == SYSTEM ? SYSTEM : PLAYER_1 + player] |= map[i].bit;
        if (sdl.GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK))
            coin[player] = 1;
        if (twoPlayerGamepads && sdl.GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST))
            io->state.inputSwitch[PLAYER_1 + player] |= BUTTON_2;
    }
}

// desktopInputState() without the mouse (see desktopKeyboardState()).
static JVSIO keyboardOnly;

// The coin counting and the pointer, after the keyboard's switches.
static JVSIO *finishInputState(JVSIO *io, int coin[2], int coinHeld[2])
{
    gamepadSwitches(io, coin);
    for (int c = 0; c < 2; c++)
    {
        if (coin[c] && !coinHeld[c])
            io->state.coinCount[c]++;
        coinHeld[c] = coin[c];
    }
    keyboardOnly = *io;
    desktopPointer(io);
    return io;
}

JVSIO *desktopKeyboardState(void)
{
    desktopInputState();
    return &keyboardOnly;
}
