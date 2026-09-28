// JAMMA I/O board of g3 engine games (Terminator Salvation).
//
// The cabinet's board counts the transitions of its switches (a count is
// odd while a switch is held) and sends the light guns' shots. The game
// reads the switch counters through its board API (JammaOp) and turns their
// changes into input events, in its poll run each frame and in its test
// menus. The board is not there: its switch counters are kept here and
// handed to the game's requests for them, the gun shots are posted as the
// input events the board's messages would have caused, and the IR gun
// manager's aim of each gun (from which the game places the reticle and
// the shots) is answered with its position. Inputs:
//  - with evdev input (INPUT_MODE 2), the loader's JVS state: the [EVDEV]
//    mappings as for the other games (guns: ANALOGUE_1/2 and 3/4, BUTTON_1
//    fires, BUTTON_2 the reload button, BUTTON_3 reloads by firing off the
//    screen, as does firing at its edge; player 1's UP/DOWN the volume);
//  - otherwise, the game window: the mouse is player 1's gun (left fires,
//    right is the reload button, middle fires off the screen); keys work on
//    QWERTY and AZERTY keyboards: 1 or I start player 1, 2 or O player 2,
//    5 or U coin, 6 coin 2, P or F1 service, F2 test, up/down arrows volume.

#include <dlfcn.h>
#include <string.h>

#include "rawthrills.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

#define MAX_BOARD_SWITCHES 32
#define MAX_GUNS 2
#define DESKTOP_ANALOGUE_MAX 0xffff

// JammaOp requests: switch counters (with a flag telling whether they
// changed) and the first read of them.
#define JAMMAOP_GET_SWITCHES 0x112
#define JAMMAOP_GET_SWITCHES_FIRST 0x312

// glut constants (freeglut).
#define GLUT_KEY_F1 1
#define GLUT_KEY_F2 2
#define GLUT_KEY_UP 101
#define GLUT_KEY_DOWN 103
#define GLUT_LEFT_BUTTON 0
#define GLUT_MIDDLE_BUTTON 1
#define GLUT_RIGHT_BUTTON 2
#define GLUT_DOWN 0
#define GLUT_WINDOW_WIDTH 102
#define GLUT_WINDOW_HEIGHT 103

static const RtGame *jammaGame;
static int (*postEvent)(int event, const void *data);
static int (*jammaPollOrig)(void);
static int (*jammaOpOrig)(int op, int a, int b, int c, int d, int e);
static uint32_t boardSwitches[MAX_BOARD_SWITCHES];
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

// glut reports characters, which depend on the keyboard layout: the digit
// keys are also taken as their AZERTY characters (& é ( -, é in Latin-1),
// and letters placed alike on QWERTY and AZERTY do the same.
static void keyboard(unsigned char key, int held)
{
    switch (key)
    {
    case '1':
    case '&':
    case 'i':
    case 'I':
        desktopSwitch(PLAYER_1, BUTTON_START, held);
        break;
    case '2':
    case 0xe9:
    case 'o':
    case 'O':
        desktopSwitch(PLAYER_2, BUTTON_START, held);
        break;
    case '5':
    case '(':
    case 'u':
    case 'U':
        desktop.state.coinCount[0] += held;
        break;
    case '6':
    case '-':
        desktop.state.coinCount[1] += held;
        break;
    case 'p':
    case 'P':
        desktopSwitch(PLAYER_1, BUTTON_SERVICE, held);
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
    else if (key == GLUT_KEY_UP)
        desktopSwitch(PLAYER_1, BUTTON_UP, held);
    else if (key == GLUT_KEY_DOWN)
        desktopSwitch(PLAYER_1, BUTTON_DOWN, held);
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
    if (w <= 1 || h <= 1)
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

#ifdef RT_JAMMA_TEST
#include <stdio.h>
#include <stdlib.h>
// TEMPORARY test driver: RT_JAMMA_SCRIPT="frame:k:char:down,frame:s:key:down,
// frame:m:x:y:button:down,..." (frames counted in the poll).
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
        if ((kind == 'k' || kind == 's') && sscanf(p, ":%d:%d%n", &a, &b, &n) == 2)
        {
            p += n;
            if (f == frame)
            {
                fprintf(stderr, "TEST frame %d %c %d %d\n", frame, kind, a, b);
                if (kind == 'k')
                    keyboard(a, b);
                else
                    special(a, b);
            }
        }
        else if (kind == 'w' && sscanf(p, ":%i:%i%n", &a, &b, &n) == 2)
        {
            p += n;
            if (f == frame)
            {
                fprintf(stderr, "TEST frame %d write %#x = %d (was %d)\n", frame, a, b, *(int *)(uintptr_t)a);
                *(int *)(uintptr_t)a = b;
            }
        }
        else if (kind == 'p' && sscanf(p, ":%i%n", &a, &n) == 1)
        {
            p += n;
            if (f == frame)
                fprintf(stderr, "TEST frame %d read %#x = %d\n", frame, a, *(int *)(uintptr_t)a);
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

// ---------------------------------------------------------------------------
// The board: switch counters and gun shots.

static int buttonHeld(JVSIO *io, int player, int bit)
{
    return (io->state.inputSwitch[player] & bit) != 0;
}

// Count a transition of each board switch whose state changed. Inputs
// sharing a switch are or-ed.
static void updateSwitches(JVSIO *io)
{
    int held[MAX_BOARD_SWITCHES] = {0};

    for (const RtIoInput *in = jammaGame->jammaSwitches; in->type != RT_IO_END; in++)
        if (in->io < MAX_BOARD_SWITCHES)
            held[in->io] |= rtIoSwitchState(in, io);
    for (int i = 0; i < MAX_BOARD_SWITCHES; i++)
        if (held[i] != (int)(boardSwitches[i] & 1))
            boardSwitches[i]++;
}

// A gun's position as a fraction of the screen (from its top left); off
// the screen when the gun points at its edge (where light guns report what
// is outside).
static int gunPosition(const RtJammaGun *gun, JVSIO *io, double *x, double *y)
{
    int max = io->analogueMax, ax = io->state.analogueChannel[gun->xChannel];
    int ay = io->state.analogueChannel[gun->yChannel];

    if (max <= 0 || ax <= 0 || ax >= max || ay <= 0 || ay >= max)
        return 0;
    *x = (double)ax / (max + 1);
    *y = (double)(max - ay) / (max + 1);
    return 1;
}

// Last position of each gun, for the IR gun manager's aim.
static double aimX[MAX_GUNS], aimY[MAX_GUNS];
static int aimValid[MAX_GUNS];

// The shot's position comes from the IR gun manager's aim (the game turns
// it into the board's sensor data itself); the board only reports the
// trigger. A shot off the screen reloads: the aim is then missing.
static void updateGun(int g, JVSIO *io)
{
    const RtJammaGun *gun = &jammaGame->jammaGuns[g];
    int offScreen = buttonHeld(io, gun->player, BUTTON_3);
    int fire = buttonHeld(io, gun->player, BUTTON_1) || offScreen;
    double x, y;

    aimValid[g] = !offScreen && gunPosition(gun, io, &x, &y);
    if (aimValid[g])
    {
        aimX[g] = x;
        aimY[g] = y;
    }
    if (fire != gunHeld[g])
        postEvent(fire ? gun->shotEvent : gun->shotEvent + 1, NULL);
    gunHeld[g] = fire;
}

// The IR gun manager's aim of a player's gun, in its camera space.
static int gunAim(int player, float *x, float *y, int unused)
{
    (void)unused;
    if (player < 0 || player >= MAX_GUNS)
        return -1;
    if (x)
        *x = aimValid[player] ? (float)(aimX[player] * jammaGame->gunAimWidth) : -1.0f;
    if (y)
        *y = aimValid[player] ? (float)(aimY[player] * jammaGame->gunAimHeight) : -1.0f;
    return 0;
}

// The game's requests to the board: switch counters from here, the others
// to its board API (which fails without a board).
static int jammaOp(int op, int a, int b, int c, int d, int e)
{
    if (op == JAMMAOP_GET_SWITCHES || op == JAMMAOP_GET_SWITCHES_FIRST)
    {
        if (op == JAMMAOP_GET_SWITCHES && a)
            *(int *)(uintptr_t)a = 1;
        if (b)
            *(uint32_t **)(uintptr_t)b = boardSwitches;
        return 0;
    }
    return jammaOpOrig(op, a, b, c, d, e);
}

// Each frame: update the board, then run the game's poll, which reads the
// switch counters and posts their events.
static int jammaPoll(void)
{
    static int started;
    JVSIO *io = evdevInput ? getJVSIO() : &desktop;

    if (!started)
    {
        started = 1;
        if (!evdevInput)
            installDesktopInput();
    }
#ifdef RT_JAMMA_TEST
    testScript();
#endif
    updateSwitches(io);
    for (int g = 0; g < jammaGame->jammaGunCount && g < MAX_GUNS; g++)
        updateGun(g, io);
    return jammaPollOrig();
}

void rtInstallJamma(const RtGame *game)
{
    if (!game->jammaPollSymbol)
        return;
    jammaGame = game;
    evdevInput = getConfig()->inputMode == 2;
    if (game->jammaGunCount)
        *(void **)&postEvent = rtSymbol(game->postEventSymbol);
    jammaPollOrig = rtTrampoline(game->jammaPollSymbol, game->jammaPollPrologue);
    jammaOpOrig = rtTrampoline(game->jammaOpSymbol, game->jammaOpPrologue);
    if ((game->jammaGunCount && !postEvent) || !jammaPollOrig || !jammaOpOrig || rtDetour(game->jammaOpSymbol, jammaOp) != 0 ||
        rtDetour(game->jammaPollSymbol, jammaPoll) != 0)
        log_error("Raw Thrills: JAMMA board input not installed");
    if (game->gunAimSymbol && rtDetour(game->gunAimSymbol, gunAim) != 0)
        log_error("Raw Thrills: gun aim not installed");
}
