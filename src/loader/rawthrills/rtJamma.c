// JAMMA I/O board of g3 engine games (Terminator Salvation).
//
// The cabinet's board sends the switches and the light guns' shots, which
// the game's poll turns into input events each frame. The board is not
// emulated: the poll is replaced by one posting the same events from
//  - with evdev input (INPUT_MODE 2), the loader's JVS state: the [EVDEV]
//    mappings as for the other games (guns: ANALOGUE_1/2 and 3/4, BUTTON_1
//    fires, BUTTON_2 the second button, BUTTON_3 reloads, as does firing at
//    the edge of the screen);
//  - otherwise, the game window: the mouse is player 1's gun (left fires,
//    right is the second button, middle reloads), keys 1/2 start, 5/6 coin,
//    F1 service, F2 test.

#include <dlfcn.h>
#include <string.h>

#include "rawthrills.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define MAX_SWITCHES 32
#define MAX_GUNS 2
#define DESKTOP_ANALOGUE_MAX 0xffff

// glut constants (freeglut).
#define GLUT_KEY_F1 1
#define GLUT_KEY_F2 2
#define GLUT_LEFT_BUTTON 0
#define GLUT_MIDDLE_BUTTON 1
#define GLUT_RIGHT_BUTTON 2
#define GLUT_DOWN 0
#define GLUT_WINDOW_WIDTH 102
#define GLUT_WINDOW_HEIGHT 103

// Data of the gun position event: gun (low nibble) and sensor (high one),
// then the position and two values the board fills from its IR camera.
typedef struct
{
    int id;
    int x, y;
    int extra[2];
} GunPosition;

static const RtGame *jammaGame;
static int (*postEvent)(int event, const void *data);
static int switchHeld[MAX_SWITCHES];
static int gunHeld[MAX_GUNS];
static int evdevInput;

// ---------------------------------------------------------------------------
// Desktop input, kept as a JVS state so that both inputs read the same way.

static JVSIO desktop = {.analogueMax = DESKTOP_ANALOGUE_MAX};
static int (*glutGet)(int what);

static void desktopSwitch(int player, int bit, int held)
{
    if (held)
        desktop.state.inputSwitch[player] |= bit;
    else
        desktop.state.inputSwitch[player] &= ~bit;
}

static void keyboard(unsigned char key, int held)
{
    switch (key)
    {
    case '1':
        desktopSwitch(PLAYER_1, BUTTON_START, held);
        break;
    case '2':
        desktopSwitch(PLAYER_2, BUTTON_START, held);
        break;
    case '5':
    case '6':
        desktop.state.coinCount[key - '5'] += held;
        break;
    }
}

static void keyDown(unsigned char key, int x, int y)
{
    (void)x;
    (void)y;
    keyboard(key, 1);
}

static void keyUp(unsigned char key, int x, int y)
{
    (void)x;
    (void)y;
    keyboard(key, 0);
}

static void special(int key, int held)
{
    if (key == GLUT_KEY_F1)
        desktopSwitch(PLAYER_1, BUTTON_SERVICE, held);
    else if (key == GLUT_KEY_F2)
        desktopSwitch(SYSTEM, BUTTON_TEST, held);
}

static void specialDown(int key, int x, int y)
{
    (void)x;
    (void)y;
    special(key, 1);
}

static void specialUp(int key, int x, int y)
{
    (void)x;
    (void)y;
    special(key, 0);
}

// Player 1's gun: its position as JVS analogue values (y upwards).
static void motion(int x, int y)
{
    int w = glutGet(GLUT_WINDOW_WIDTH), h = glutGet(GLUT_WINDOW_HEIGHT);
    if (w <= 0 || h <= 0)
        return;
    x = x < 0 ? 0 : x >= w ? w - 1 : x;
    y = y < 0 ? 0 : y >= h ? h - 1 : y;
    // Keep off the extremes, which mean off the screen.
    desktop.state.analogueChannel[ANALOGUE_1] = 1 + (int)((double)x * (DESKTOP_ANALOGUE_MAX - 2) / (w - 1));
    desktop.state.analogueChannel[ANALOGUE_2] = 1 + (int)((double)(h - 1 - y) * (DESKTOP_ANALOGUE_MAX - 2) / (h - 1));
}

static void mouse(int button, int state, int x, int y)
{
    static const int bits[] = {[GLUT_LEFT_BUTTON] = BUTTON_1, [GLUT_MIDDLE_BUTTON] = BUTTON_3,
                               [GLUT_RIGHT_BUTTON] = BUTTON_2};
    motion(x, y);
    if (button >= 0 && button < (int)(sizeof(bits) / sizeof(bits[0])))
        desktopSwitch(PLAYER_1, bits[button], state == GLUT_DOWN);
}

// The callbacks go to the window the game opened, current by now.
static void installDesktopInput(void)
{
    void (*keyboardFunc)(void (*)(unsigned char, int, int)) = dlsym(RTLD_NEXT, "glutKeyboardFunc");
    void (*keyboardUpFunc)(void (*)(unsigned char, int, int)) = dlsym(RTLD_NEXT, "glutKeyboardUpFunc");
    void (*specialFunc)(void (*)(int, int, int)) = dlsym(RTLD_NEXT, "glutSpecialFunc");
    void (*specialUpFunc)(void (*)(int, int, int)) = dlsym(RTLD_NEXT, "glutSpecialUpFunc");
    void (*mouseFunc)(void (*)(int, int, int, int)) = dlsym(RTLD_NEXT, "glutMouseFunc");
    void (*motionFunc)(void (*)(int, int)) = dlsym(RTLD_NEXT, "glutMotionFunc");
    void (*passiveMotionFunc)(void (*)(int, int)) = dlsym(RTLD_NEXT, "glutPassiveMotionFunc");
    void (*ignoreKeyRepeat)(int) = dlsym(RTLD_NEXT, "glutIgnoreKeyRepeat");

    glutGet = dlsym(RTLD_NEXT, "glutGet");
    if (!keyboardFunc || !keyboardUpFunc || !specialFunc || !specialUpFunc || !mouseFunc || !motionFunc ||
        !passiveMotionFunc || !glutGet)
    {
        log_error("Raw Thrills: glut input functions not found, no keyboard and mouse input");
        return;
    }
    if (ignoreKeyRepeat)
        ignoreKeyRepeat(1);
    keyboardFunc(keyDown);
    keyboardUpFunc(keyUp);
    specialFunc(specialDown);
    specialUpFunc(specialUp);
    mouseFunc(mouse);
    motionFunc(motion);
    passiveMotionFunc(motion);
}

// ---------------------------------------------------------------------------

static int buttonHeld(JVSIO *io, int player, int bit)
{
    return (io->state.inputSwitch[player] & bit) != 0;
}

// A gun's position in the board's space; off the screen when the gun
// points at its edge (where light guns report what is outside).
static int gunPosition(const RtJammaGun *gun, JVSIO *io, int *x, int *y)
{
    int max = io->analogueMax, ax = io->state.analogueChannel[gun->xChannel];
    int ay = io->state.analogueChannel[gun->yChannel];

    if (max <= 0 || ax <= 0 || ax >= max || ay <= 0 || ay >= max)
        return 0;
    *x = (int)((double)ax * jammaGame->gunWidth / (max + 1));
    *y = (int)((double)(max - ay) * jammaGame->gunHeight / (max + 1));
    return 1;
}

static void updateGun(int g, JVSIO *io)
{
    const RtJammaGun *gun = &jammaGame->jammaGuns[g];
    int reload = buttonHeld(io, gun->player, BUTTON_3);
    int fire = buttonHeld(io, gun->player, BUTTON_1) || reload;
    GunPosition pos = {.x = -1, .y = -1};

    if (reload || !gunPosition(gun, io, &pos.x, &pos.y))
        pos.x = pos.y = -1;
    // The shot and the second button take their position from sensors 0
    // and 1: set both before a press.
    for (int sensor = 0; sensor < 2; sensor++)
    {
        pos.id = g | sensor << 4;
        postEvent(jammaGame->gunPositionEvent, &pos);
    }
    if (fire != gunHeld[g])
        postEvent(fire ? gun->shotEvent : gun->shotEvent + 1, NULL);
    gunHeld[g] = fire;
}

#ifdef RT_JAMMA_TEST
#include <stdio.h>
#include <stdlib.h>
// TEMPORARY test driver: RT_JAMMA_SCRIPT="frame:k:c:down,frame:m:x:y:b:down,..."
static void testScript(void)
{
    static int frame;
    const char *sc = getenv("RT_JAMMA_SCRIPT");
    frame++;
    for (const char *p = sc; p && *p;)
    {
        int f, a, b, c, d, n = 0;
        char kind;
        if (sscanf(p, "%d:%c%n", &f, &kind, &n) < 2)
            break;
        p += n;
        if (kind == 'k' && sscanf(p, ":%d:%d%n", &a, &b, &n) == 2)
        {
            p += n;
            if (f == frame)
            {
                fprintf(stderr, "TEST frame %d key %c %d\n", frame, a, b);
                keyboard(a, b);
            }
        }
        else if (kind == 'm' && sscanf(p, ":%d:%d:%d:%d%n", &a, &b, &c, &d, &n) == 4)
        {
            p += n;
            if (f == frame)
            {
                fprintf(stderr, "TEST frame %d mouse %d %d %d %d\n", frame, a, b, c, d);
                mouse(c, d ? GLUT_DOWN : 1, a, b);
            }
        }
        while (*p && *p != ',')
            p++;
        if (*p)
            p++;
    }
}
#endif

static int jammaPoll(void)
{
    static int started;
#ifdef RT_JAMMA_TEST
    testScript();
#endif
    JVSIO *io = evdevInput ? getJVSIO() : &desktop;

    if (!started)
    {
        started = 1;
        if (!evdevInput)
            installDesktopInput();
    }
    // Inputs sharing an event are or-ed: handle the event at its first one.
    const RtIoInput *switches = jammaGame->jammaSwitches;
    for (int i = 0; i < MAX_SWITCHES && switches[i].type != RT_IO_END; i++)
    {
        int first = 0, held = 0;
        while (switches[first].io != switches[i].io)
            first++;
        if (first != i)
            continue;
        for (int j = i; switches[j].type != RT_IO_END; j++)
            if (switches[j].io == switches[i].io)
                held |= rtIoSwitchState(&switches[j], io);
        if (held != switchHeld[i])
            postEvent(held ? switches[i].io : switches[i].io + 1, NULL);
        switchHeld[i] = held;
    }
    for (int g = 0; g < jammaGame->jammaGunCount && g < MAX_GUNS; g++)
        updateGun(g, io);
    return 0;
}

void rtInstallJamma(const RtGame *game)
{
    if (!game->jammaPollSymbol)
        return;
    jammaGame = game;
    evdevInput = getConfig()->inputMode == 2;
    *(void **)&postEvent = rtSymbol(game->postEventSymbol);
    if (!postEvent || rtDetour(game->jammaPollSymbol, jammaPoll) != 0)
        log_error("Raw Thrills: JAMMA board input not installed");
}
