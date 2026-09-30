// RIO I/O board and input for g5 engine games.
//
// The USB transport of the RIO board is stubbed (a connected but silent
// board). Input goes through the engine's own input map, adjusted per game
// (see the input fields of RtGame):
//  - games can have the developer mode that maps the mouse to the guns (its
//    command-line switch was removed from the release builds) turned back
//    on, and keyboard keys added for the cabinet buttons;
//  - with evdev input (INPUT_MODE 2), the loader's evdev controllers fill the
//    emulated JVS state from the [EVDEV] mappings as for Lindbergh games, and
//    that state is fed each frame to the engine's I/O slots, where the
//    cabinet board's inputs arrive.

#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rawthrills.h"
#include "../config/config.h"
#include "../graphics/frameScale.h"
#include "../hardware/lindbergh/jvs.h"
#include "../log/log.h"

// ---------------------------------------------------------------------------
// RIO board: stub its USB transport so the game sees a connected, silent board.

static int retZero(void)
{
    return 0;
}

static int retOne(void)
{
    return 1;
}

void rtInstallIo(const RtGame *game)
{
    for (const RtStub *stub = game->stubs; stub && stub->name; stub++)
        rtDetour(stub->name, stub->value ? (void *)retOne : (void *)retZero);
    if (game->install)
        game->install(game);
    rtDetour("pmrt_usb_open", retOne);
    rtDetour("pmrt_usb_interrupt_read", retZero);
    rtDetour("pmrt_usb_interrupt_write", retZero);
    rtDetour("pmrt_usb_control_xfr", retZero);
    rtDetour("io_usbdev_init", retZero);
    rtDetour("read_thread_func", retZero);
    rtDetour("parse_in_report", retZero);
    rtDetour("RIO_FileLog", retZero);
    rtDetour("_Z30cpp_gyrogun_update_rt_wirelessi", retZero);
    rtDetour("xicoCardThread", retZero);
}

// ---------------------------------------------------------------------------
// Engine input map.

static const RtGame *currentGame;
static int (*inputAddMap)(int src, int dst);
static int (*gameInputMapsOrig)(void);
static int evdevInput;

static void addMaps(const RtMap *maps)
{
    for (const RtMap *m = maps; m && (m->src || m->dst); m++)
        inputAddMap(m->src, m->dst);
}

static int filtered(const RtMapFilter *filters, uint32_t src, uint32_t dst)
{
    for (const RtMapFilter *f = filters; f && f->srcMax; f++)
        if (src >= f->srcMin && src <= f->srcMax && dst >= f->dstMin && dst <= f->dstMax)
            return 1;
    return 0;
}

// Drop the map entries matching one of the filters.
static void filterInputMaps(const RtMapFilter *filters)
{
    uint32_t(*map)[2] = (uint32_t(*)[2])(uintptr_t)currentGame->inputMap;
    uint32_t *count = (uint32_t *)(uintptr_t)currentGame->inputMapCount;
    uint32_t kept = 0;

    if (!filters)
        return;
    for (uint32_t i = 0; i < *count; i++)
    {
        if (filtered(filters, map[i][0], map[i][1]))
            continue;
        map[kept][0] = map[i][0];
        map[kept][1] = map[i][1];
        kept++;
    }
    *count = kept;
}

// Games registering their maps inline: each entry as it is added.
static int (*inputAddMapOrig)(int src, int dst);

static int inputAddMapHook(int src, int dst)
{
    static int added;
    int ret;

    if (filtered(currentGame->devMapFilters, src, dst) || (evdevInput && filtered(currentGame->evdevMapFilters, src, dst)))
        return 0;
    ret = inputAddMapOrig(src, dst);
    if (!added)
    {
        added = 1;
        addMaps(currentGame->extraMaps);
        if (evdevInput)
            addMaps(currentGame->evdevMaps);
    }
    return ret;
}

static int gameInputMaps(void)
{
    if (currentGame->devInputFlag)
        *(volatile uint32_t *)(uintptr_t)currentGame->devInputFlag = 1;
    int ret = gameInputMapsOrig();

    filterInputMaps(currentGame->devMapFilters);
    addMaps(currentGame->extraMaps);
    if (evdevInput)
    {
        filterInputMaps(currentGame->evdevMapFilters);
        addMaps(currentGame->evdevMaps);
    }
    return ret;
}

// ---------------------------------------------------------------------------
// Evdev input through the loader's JVS state, fed to the engine's I/O slots
// (where the cabinet board's inputs arrive) from its SDL input backend.

// Engine digital input slot (io_input_digital).
typedef struct
{
    uint32_t unused;
    uint32_t pressed;  // became held this frame
    uint32_t released; // stopped being held this frame
    uint32_t heldTime;
    uint32_t pressCount;
    uint32_t releaseCount;
    uint32_t raw;
    uint32_t rawCount;
} IoDigital;

static JVSIO *desktopIo(void);
static int ioFromDesktop;

static void *(*ioGetInputAnalog)(int id);
static void *(*ioGetInputDigital)(int id);
static void (*ioInputAnalogUpdate)(void *slot, float raw);
static int (*ioSetInputRawRange)(int id, float min, float max);
static int (*ioLoopOrig)(int dt);
static volatile uint32_t *ioNewData;

// io_new_data_present only returns a flag: "mov flag,%eax" (after a frame
// setup in some builds). Find that flag.
static volatile uint32_t *findNewDataFlag(void)
{
    const uint8_t *code = rtSymbol("io_new_data_present");
    for (int i = 0; code && i < 16; i++)
        if (code[i] == 0xa1)
            return (volatile uint32_t *)(uintptr_t) * (const uint32_t *)(code + i + 1);
    return NULL;
}

// The engine's io_input_digital_update (not exported by every game) for a
// plain on/off switch.
static void ioDigitalSet(int id, int dt, int held)
{
    IoDigital *slot = ioGetInputDigital(id), old;

    if (!slot)
        return;
    old = *slot;
    slot->pressed = !old.pressed && !old.heldTime && held;
    slot->released = !old.released && old.heldTime && !held;
    slot->heldTime = held ? old.heldTime + dt + slot->pressed : 0;
    slot->raw = held;
    slot->pressCount += slot->pressed;
    slot->releaseCount += slot->released;
    slot->rawCount += held >= (int)old.raw ? held - old.raw : held + (0x100 - old.raw);
}

static void ioAnalogSet(int id, float raw)
{
    void *slot = ioGetInputAnalog(id);
    if (slot)
        ioInputAnalogUpdate(slot, raw);
}

// A coin is a count in the JVS state: each increment becomes a press of the
// coin switch, held COIN_PULSE_MS and followed by as long a gap, as a coin
// mech's. A one-frame press is too short for games filtering their coin
// switch at a high frame rate (MotoGP).
#define COIN_PULSE_MS 80

typedef struct
{
    int seen, held;
    uint64_t until;
} CoinPulse;

static uint64_t nowMs(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int coinPulseState(CoinPulse *p, int count)
{
    uint64_t now = nowMs();

    if (p->held)
    {
        if (now < p->until)
            return 1;
        p->held = 0;
        p->until = now + COIN_PULSE_MS;
        return 0;
    }
    if (now < p->until)
        return 0;
    if (count > p->seen)
    {
        p->seen++;
        p->held = 1;
        p->until = now + COIN_PULSE_MS;
        return 1;
    }
    p->seen = count;
    return 0;
}

static int coinPulse(int slot, int count)
{
    static CoinPulse pulses[2];
    return coinPulseState(&pulses[slot], count);
}

int rtIoSwitchState(const RtIoInput *in, JVSIO *io)
{
    switch (in->type)
    {
    case RT_IO_ANALOG_SWITCH:
        return io->state.analogueChannel[in->source] > io->analogueMax / 2;
    case RT_IO_SWITCH:
        return (io->state.inputSwitch[in->player] & in->source) != 0;
    case RT_IO_COIN:
        return coinPulse(in->player, io->state.coinCount[in->player]);
    default:
        return 0;
    }
}

// A switch's presses since the last read, for readers polling once a frame:
// a press shorter than that is still seen (see JVSState.switchPresses).
int rtSwitchTaps(JVSIO *io, int player, uint32_t bit, unsigned int *last)
{
    unsigned int presses = io->state.switchPresses[player][__builtin_ctz(bit)];
    int taps = (int)(presses - *last);
    *last = presses;
    return taps;
}

// Each press a press and a release, but for the state the switch is now in.
// A press already over is held for this frame (a light gun's pump is a
// click, whatever the player does): the games act on a held switch (a
// reload once), not on its transitions alone.
void rtSwitchCount(uint32_t *count, int *wasHeld, int held, int taps, int inverted)
{
    held |= taps > 0;
    if (taps > 0)
        *count += 2 * taps + *wasHeld - held;
    *wasHeld = held;
    if ((held ^ inverted) != (int)(*count & 1))
        (*count)++;
}

// A switch of the desktop keys (their coins as counted presses).
static int desktopSwitchState(const RtIoInput *in)
{
    static CoinPulse pulses[2];
    JVSIO *desktop = desktopIo();

    if (in->type == RT_IO_SWITCH)
        return (desktop->state.inputSwitch[in->player] & in->source) != 0;
    if (in->type != RT_IO_COIN || in->player > 1)
        return 0;
    return coinPulseState(&pulses[in->player], desktop->state.coinCount[in->player]);
}

// Whether desktop keys drive this analog slot.
static int keysDriveSlot(const RtIoInput *inputs, uint16_t slot)
{
    for (const RtIoInput *in = inputs; in->type != RT_IO_END; in++)
        if (in->type == RT_IO_SWITCH_ANALOG && in->io == slot)
            return 1;
    return 0;
}

// Runs as one of the engine's I/O backends, once per frame inside InputLoop.
static int ioLoopHook(int dt)
{
    static int rangesSet;
    const RtIoInput *inputs = currentGame->ioInputs;
    JVSIO *io = ioFromDesktop ? desktopIo() : getJVSIO();
    // With evdev input, the desktop keys of ioDesktop games still work (the
    // operator's test and service keys on a gun-only setup).
    int desktopToo = !ioFromDesktop && currentGame->ioDesktop;
    int ret = ioLoopOrig(dt);

    for (const RtIoInput *in = inputs; in->type != RT_IO_END; in++)
    {
        if (in->type == RT_IO_SWITCH_ANALOG)
        {
            // The desktop keys: in desktop mode they are the slot's only
            // input (at rest when none is held); with evdev input they
            // override the controller's axis while held.
            JVSIO *keys = ioFromDesktop ? io : desktopToo ? desktopIo() : NULL;
            uint32_t low = in->source & 0xffff, high = in->source >> 16;
            uint32_t held = keys ? keys->state.inputSwitch[in->player] & (low | high) : 0;
            if (!held && !ioFromDesktop)
                continue;
            int value = !low ? 0 : io->analogueMax / 2;
            if ((held & high) && !(held & low))
                value = io->analogueMax;
            else if ((held & low) && !(held & high))
                value = 0;
            if (ioFromDesktop && currentGame->ioRawRange && (!rangesSet || currentGame->ioRawRange == 2))
                ioSetInputRawRange(in->io, 0, io->analogueMax);
            ioAnalogSet(in->io, value);
            continue;
        }
        if (in->type == RT_IO_ANALOG || in->type == RT_IO_ANALOG_INVERTED)
        {
            // In desktop mode, slots driven by keys (RT_IO_SWITCH_ANALOG) are
            // not the mouse's.
            if (ioFromDesktop && keysDriveSlot(inputs, in->io))
                continue;
            int value = io->state.analogueChannel[in->source];
            if (currentGame->ioRawRange && (!rangesSet || currentGame->ioRawRange == 2))
                ioSetInputRawRange(in->io, 0, io->analogueMax);
            ioAnalogSet(in->io, in->type == RT_IO_ANALOG ? value : io->analogueMax - value);
            continue;
        }
        // Switches sharing an I/O slot are or-ed: handle the slot once.
        const RtIoInput *first = inputs;
        while (first->io != in->io || first->type == RT_IO_ANALOG || first->type == RT_IO_ANALOG_INVERTED ||
               first->type == RT_IO_SWITCH_ANALOG)
            first++;
        if (first != in)
            continue;
        int held = 0;
        for (const RtIoInput *other = in; other->type != RT_IO_END; other++)
            if (other->io == in->io && other->type != RT_IO_ANALOG && other->type != RT_IO_ANALOG_INVERTED &&
                other->type != RT_IO_SWITCH_ANALOG)
                held |= rtIoSwitchState(other, io) || (desktopToo && desktopSwitchState(other));
        ioDigitalSet(in->io, dt, held);
    }
    rangesSet = 1;
    if (currentGame->ioFrame)
        currentGame->ioFrame(io);
    // The engine only reads the I/O slots in frames flagged as having new
    // data: a press edge set in another frame would be lost.
    *ioNewData = 1;
    return ret;
}

// IR tracked guns: the camera manager's answers (see RtGame).
#define IR_CAMERA_WIDTH 800
#define IR_CAMERA_HEIGHT 600

// A gun's position from the loader's input, 0..1 from the top left; 0 off
// the screen (a light gun reports the edge of its range there, the desktop
// pointer 0).
static int irGunOnScreen(int gun, float *x, float *y)
{
    JVSIO *io = ioFromDesktop ? desktopIo() : getJVSIO();
    int max = io->analogueMax, ax, ay;

    if (gun < 0 || gun > 1 || max <= 0)
        return 0;
    ax = io->state.analogueChannel[ANALOGUE_1 + 2 * gun];
    ay = io->state.analogueChannel[ANALOGUE_2 + 2 * gun];
    if (ax <= 0 || ax >= max || ay <= 0 || ay >= max)
        return 0;
    *x = (float)ax / max;
    *y = (float)ay / max;
    // The evdev guns aim at the screen: to the picture, when that does not
    // fill it (the desktop pointer is already the picture's).
    return ioFromDesktop || frameScaleWindowToGame(x, y);
}

// A connected gun that never points at the screen stalls the game: the
// first gun is always connected, the second when evdev aims it (a gun or a
// mouse is mapped on ANALOGUE_3), else once it has been on the screen. The
// games check it for their "Right gun not connected!" and their calibration.
static int irGunActive(int gun)
{
    static int seen[2] = {1, -1};
    float x, y;

    if (gun < 0 || gun > 1)
        return 0;
    if (seen[1] < 0)
        seen[1] = evdevInput && getConfig()->arcadeInputs.analogue_3[0] != 0;
    if (!seen[gun] && irGunOnScreen(gun, &x, &y))
        seen[gun] = 1;
    return seen[gun];
}

static int irGunRaw(int gun, float *x, float *y)
{
    if (!x || !y)
        return -1;
    if (!irGunOnScreen(gun, x, y))
    {
        *x = *y = -1.f;
        return 0;
    }
    *x *= IR_CAMERA_WIDTH;
    *y *= IR_CAMERA_HEIGHT;
    return 0;
}

// The aim, in the gun slots' raw range (min at +0x14 of a slot, max at
// +0x18, as io_irtrack checks it: 0..1, Y up as in Halo's): the loader's
// guns and mice point where they aim already, so the game's calibration,
// which only worked for the first gun, is left out.
#define IR_GUN_SLOT 0x181
#define IR_GUN_BUTTON_SLOT_SIZE 0x2c
static int irGunAim(int gun, float *x, float *y)
{
    float px, py;

    if (!x || !y)
        return 0;
    if (!irGunOnScreen(gun, &px, &py))
    {
        *x = *y = -1.f;
        return 0;
    }
    const float *sx = ioGetInputAnalog(IR_GUN_SLOT + 2 * gun);
    const float *sy = ioGetInputAnalog(IR_GUN_SLOT + 2 * gun + 1);
    *x = sx[5] + px * (sx[6] - sx[5]);
    *y = sy[5] + (1.f - py) * (sy[6] - sy[5]);
    return 0;
}

// Whether io_irtrack reads a gun button's count inverted: bit 0 of its slot's
// flags, which the games set on their gun buttons (see RtGame).
static int irGunButtonInverted(int gun, int button)
{
    const RtGame *g = currentGame;
    const uint8_t *slots = g->irGunSlots ? *(uint8_t *const *)(uintptr_t)g->irGunSlots : NULL;

    if (!slots)
        return 0;
    slots += gun * g->irGunSlotStride + g->irGunButtonSlot + (button == 3 ? 2 : button) * IR_GUN_BUTTON_SLOT_SIZE;
    return *(const uint32_t *)slots & 1;
}

// Buttons 0 and 1 are the trigger and the pump; Aliens Armageddon's guns
// have a grenade button too, read as button 3 (button 2 is never asked for).
// A count of the button's transitions: odd while held, or even for a slot
// the game inverts. The engine reads it once a frame, and takes several
// transitions at once: the presses the evdev input counted since (a quick
// pull pressed and released between two frames) are all in it.
static int irGunButton(int gun, int button)
{
    static const int bits[] = {BUTTON_1, BUTTON_2, 0, BUTTON_3};
    static uint32_t count[2][4];
    static int wasHeld[2][4];
    static unsigned int lastPresses[2][4];
    JVSIO *io = ioFromDesktop ? desktopIo() : getJVSIO();

    if (!irGunActive(gun) || button < 0 || button >= 4 || !bits[button])
        return 0;
    rtSwitchCount(&count[gun][button], &wasHeld[gun][button],
                  (io->state.inputSwitch[PLAYER_1 + gun] & bits[button]) != 0,
                  rtSwitchTaps(io, PLAYER_1 + gun, bits[button], &lastPresses[gun][button]),
                  irGunButtonInverted(gun, button));
    return (int)count[gun][button];
}

static void installEvdevInput(const RtGame *game)
{
    if (!game->ioInputs)
        return;
    *(void **)&ioGetInputAnalog = rtSymbol("io_get_input_analog");
    *(void **)&ioGetInputDigital = rtSymbol("io_get_input_digital");
    *(void **)&ioInputAnalogUpdate = rtSymbol("io_input_analog_update");
    *(void **)&ioSetInputRawRange = rtSymbol("io_set_input_raw_range");
    const char *ioLoopSymbol = game->ioLoopSymbol ? game->ioLoopSymbol : "io_sdl_loop";
    ioLoopOrig = rtTrampoline(ioLoopSymbol, game->ioLoopPrologue);
    ioNewData = findNewDataFlag();
    // The analog functions are only needed by games feeding an analog slot.
    int analog = 0;
    for (const RtIoInput *in = game->ioInputs; in->type != RT_IO_END; in++)
        analog |= in->type == RT_IO_ANALOG || in->type == RT_IO_ANALOG_INVERTED || in->type == RT_IO_SWITCH_ANALOG;
    if ((analog && (!ioGetInputAnalog || !ioInputAnalogUpdate || !ioSetInputRawRange)) || !ioGetInputDigital ||
        !ioLoopOrig || !ioNewData)
    {
        log_error("Raw Thrills: evdev input disabled, engine I/O functions not found");
        return;
    }
    rtDetour(ioLoopSymbol, ioLoopHook);
    evdevInput = getConfig()->inputMode == 2;
    ioFromDesktop = !evdevInput;
    if (game->irGunActiveSymbol &&
        (rtDetour(game->irGunActiveSymbol, irGunActive) != 0 || rtDetour(game->irGunRawSymbols[0], irGunRaw) != 0 ||
         rtDetour(game->irGunRawSymbols[1], irGunRaw) != 0 || rtDetour(game->irGunButtonSymbol, irGunButton) != 0))
        log_error("Raw Thrills: IR gun manager not answered, the guns stay disconnected");
    if (game->irGunAimSymbol && (!ioGetInputAnalog || rtDetour(game->irGunAimSymbol, irGunAim) != 0))
        log_error("Raw Thrills: IR gun aim not answered, the game's calibration applies");
}

// ---------------------------------------------------------------------------
// RIO API (games not built on the g5 engine): switch state and press count.

#define RIO_SWITCHES 64

static int rioHeld[RIO_SWITCHES];
static uint32_t rioCount[RIO_SWITCHES];

// Desktop input (INPUT_MODE other than 2, games with rioDesktopKeys): the
// keyboard, read from the X server's key map, on keys given by keysym so
// that they are found on any layout: 1 or I start, 5 or U coin, 6 coin 2,
// F1 or P service, F2 test, arrows the joystick, Page Up/Down BUTTON_7/8.
typedef struct
{
    unsigned long keysym; // X11 keysym
    int player, bit;      // switch, or player -1: coin slot bit
} RtDesktopKey;

static const RtDesktopKey desktopKeys[] = {
    {0x31, PLAYER_1, BUTTON_START},   // 1
    {0x69, PLAYER_1, BUTTON_START},   // i
    {0x32, PLAYER_2, BUTTON_START},   // 2
    {0x6f, PLAYER_2, BUTTON_START},   // o
    {0x35, -1, 0},                    // 5: coin 1
    {0x75, -1, 0},                    // u: coin 1
    {0x36, -1, 1},                    // 6: coin 2
    {0xffbe, PLAYER_1, BUTTON_SERVICE}, // F1
    {0x70, PLAYER_1, BUTTON_SERVICE}, // p
    {0x20, PLAYER_1, BUTTON_1},       // space: P1's button (a gun's trigger)
    {0x7a, PLAYER_1, BUTTON_2},       // z
    {0x78, PLAYER_1, BUTTON_3},       // x
    {0x63, PLAYER_1, BUTTON_4},       // c
    {0x76, PLAYER_1, BUTTON_5},       // v
    {0xffbf, SYSTEM, BUTTON_TEST},    // F2
    {0xff52, PLAYER_1, BUTTON_UP},
    {0xff54, PLAYER_1, BUTTON_DOWN},
    {0xff51, PLAYER_1, BUTTON_LEFT},
    {0xff53, PLAYER_1, BUTTON_RIGHT},
    {0xff55, PLAYER_1, BUTTON_7},     // Page Up
    {0xff56, PLAYER_1, BUTTON_8},     // Page Down
};

#define DESKTOP_KEYS (sizeof(desktopKeys) / sizeof(desktopKeys[0]))

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
            log_warn("Raw Thrills: no X display, keyboard input disabled");
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
// SDL; the glut games quit from their window's key callbacks).
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
} RtXlib;

// The focused window, or one of its parents, is a window of this process.
static int gameFocused(const RtXlib *x, void *display, unsigned long pidAtom)
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
    RtXlib x = {
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
        log_warn("Raw Thrills: no X display, Esc/Alt+F4 quit disabled");
        return NULL;
    }
    unsigned char escape = x.keysymToKeycode(display, 0xff1b), f4 = x.keysymToKeycode(display, 0xffc1),
                  altL = x.keysymToKeycode(display, 0xffe9), altR = x.keysymToKeycode(display, 0xffea);
    unsigned long pidAtom = x.internAtom(display, "_NET_WM_PID", 0);

    for (;;)
    {
        char keys[32];

        usleep(50000);
        x.queryKeymap(display, keys);
        if (!keyHeld(keys, escape) && !(keyHeld(keys, f4) && (keyHeld(keys, altL) || keyHeld(keys, altR))))
            continue;
        if (!gameFocused(&x, display, pidAtom))
            continue;
        log_info("Raw Thrills: quit from the keyboard");
        _exit(0);
    }
    return NULL;
}

void rtStartQuitWatch(void)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, quitWatch, NULL) == 0)
        pthread_detach(thread);
}

// The mouse over the focused window (the game's) is player 1's gun: its
// position on the picture (which is scaled into the window once the window is
// resized, see frameScale.h) as analogue channels 1 and 2 (from the top left,
// 0 when outside), left button BUTTON_1, right BUTTON_2, middle BUTTON_3.
#define DESKTOP_ANALOGUE_MAX 0xffff

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
    if (!getInputFocus || !queryPointer || !getWindowAttributes)
        return;
    getInputFocus(keyDisplay, &window, &revert);
    // Window attributes: x, y, width, height first.
    if (window <= 1 || !getWindowAttributes(keyDisplay, window, attributes) ||
        !queryPointer(keyDisplay, window, &root, &child, &rx, &ry, &x, &y, &mask))
        return;
    int w = attributes[2], h = attributes[3];
    if (mask & (1 << 8))
        io->state.inputSwitch[PLAYER_1] |= BUTTON_1;
    if (mask & (1 << 10))
        io->state.inputSwitch[PLAYER_1] |= BUTTON_2;
    if (mask & (1 << 9))
        io->state.inputSwitch[PLAYER_1] |= BUTTON_3;
    if (w <= 1 || h <= 1 || x < 0 || y < 0 || x >= w || y >= h)
        return;
    float px = (float)x / (w - 1), py = (float)y / (h - 1);
    if (!frameScaleWindowToGame(&px, &py))
        return;
    io->state.analogueChannel[ANALOGUE_1] = 1 + (int)(px * (DESKTOP_ANALOGUE_MAX - 2));
    io->state.analogueChannel[ANALOGUE_2] = 1 + (int)(py * (DESKTOP_ANALOGUE_MAX - 2));
}

static JVSIO *desktopIo(void)
{
    static JVSIO io;
    static int mapped;
    static unsigned char keycodes[DESKTOP_KEYS];
    static int coinHeld[2];
    static struct timespec last;
    struct timespec now;
    char keys[32];

    if (!openKeyboard())
        return &io;
    if (!mapped)
    {
        for (size_t i = 0; i < DESKTOP_KEYS; i++)
            keycodes[i] = keysymToKeycode(keyDisplay, desktopKeys[i].keysym);
        mapped = 1;
    }
    // The switches are read one by one each frame: poll the keyboard once.
    clock_gettime(CLOCK_MONOTONIC, &now);
    if ((now.tv_sec - last.tv_sec) * 1000000000L + (now.tv_nsec - last.tv_nsec) < 4000000L)
        return &io;
    last = now;
    queryKeymap(keyDisplay, keys);

    int coin[2] = {0, 0};
    for (int p = 0; p < 3; p++)
        io.state.inputSwitch[p] = 0;
    for (size_t i = 0; i < DESKTOP_KEYS; i++)
    {
        const RtDesktopKey *k = &desktopKeys[i];
        if (!keyHeld(keys, keycodes[i]))
            continue;
        if (k->player < 0)
            coin[k->bit] = 1;
        else
            io.state.inputSwitch[k->player] |= k->bit;
    }
    for (int c = 0; c < 2; c++)
    {
        if (coin[c] && !coinHeld[c])
            io.state.coinCount[c]++;
        coinHeld[c] = coin[c];
    }
    desktopPointer(&io);
    return &io;
}

int rtGunOnScreen(int gun, float *x, float *y)
{
    return irGunOnScreen(gun, x, y);
}

JVSIO *rtDesktopKeys(void)
{
    return desktopIo();
}

// With evdev input, the desktop keys of rioDesktopKeys games still work
// (the operator's test and service keys on a gun-only setup).
static int desktopKeyState(const RtIoInput *in)
{
    static CoinPulse pulses[2];
    JVSIO *desktop = desktopIo();

    if (in->type == RT_IO_SWITCH)
        return (desktop->state.inputSwitch[in->player] & in->source) != 0;
    if (in->type != RT_IO_COIN || in->player > 1)
        return 0;
    return coinPulseState(&pulses[in->player], desktop->state.coinCount[in->player]);
}

static int rioSwState(int sw)
{
    int evdev = getConfig()->inputMode == 2;
    JVSIO *io = evdev ? getJVSIO() : desktopIo();
    int held = 0;

    if (sw < 0 || sw >= RIO_SWITCHES)
        return 0;
    for (const RtIoInput *in = currentGame->rioSwitches; in->type != RT_IO_END; in++)
        if (in->io == sw)
            held |= rtIoSwitchState(in, io) | (evdev && currentGame->rioDesktopKeys && desktopKeyState(in));
    if (held && !rioHeld[sw])
        rioCount[sw]++;
    rioHeld[sw] = held;
    return held;
}

static uint32_t rioSwCount(int sw)
{
    return sw >= 0 && sw < RIO_SWITCHES ? rioCount[sw] : 0;
}

// The board's switch reports: a transition count per switch (odd: held).
static int (*rioLoopOrig)(int dt);
static int (*rioEvent)(int sw, int type, uint32_t count, uint32_t time);
static uint32_t rioTransitions[RIO_SWITCHES];

static int rioLoopHook(int dt)
{
    int ret = rioLoopOrig(dt), changed = 0;
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    for (const RtIoInput *in = currentGame->rioSwitches; in->type != RT_IO_END; in++)
    {
        int sw = in->io;
        const RtIoInput *first = currentGame->rioSwitches;
        while (first->io != sw)
            first++;
        if (first != in || sw < 0 || sw >= RIO_SWITCHES)
            continue;
        int held = rioSwState(sw) == 1;
        if (held != (int)(rioTransitions[sw] & 1))
        {
            rioTransitions[sw]++;
            rioEvent(sw, 3, rioTransitions[sw], (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000));
            changed = 1;
        }
    }
    return ret || changed;
}

// ---------------------------------------------------------------------------
// UMC board (mounted guns): pots and switches from the loader's input
// (evdev, or the desktop mouse), in its report.

#define UMC_SWITCHES 8
#define UMC_ANALOGS 8
#define UMC_MAX 1023

static JVSIO *inputIo(void)
{
    return getConfig()->inputMode == 2 ? getJVSIO() : desktopIo();
}

static int umcConnected(void)
{
    return 0;
}

static int umcRead(int *report)
{
    static uint32_t transitions[UMC_SWITCHES];
    static int pots[UMC_ANALOGS];
    JVSIO *io = inputIo();
    int max = io->analogueMax;

    for (int g = 0; g < currentGame->umcGunCount; g++)
    {
        const RtUmcGun *gun = &currentGame->umcGuns[g];
        int ax = io->state.analogueChannel[gun->xChannel], ay = io->state.analogueChannel[gun->yChannel];
        int buttons = io->state.inputSwitch[gun->player];

        // Off the screen: the gun stays where it was.
        if (max > 0 && ax > 0 && ax < max && ay > 0 && ay < max)
        {
            pots[gun->umcX] = (int)((double)ax * UMC_MAX / max);
            pots[gun->umcY] = (int)((double)(max - ay) * UMC_MAX / max);
        }
        pots[gun->umcLever] = buttons & BUTTON_2 ? UMC_MAX : 0;
        int held = (buttons & BUTTON_1) != 0;
        if (held != (int)(transitions[gun->umcTrigger] & 1))
            transitions[gun->umcTrigger] = (transitions[gun->umcTrigger] + 1) & 0xff;
    }
    for (int i = 0; i < UMC_SWITCHES; i++)
        report[i] = (int)transitions[i];
    for (int i = 0; i < UMC_ANALOGS; i++)
        report[UMC_SWITCHES + i] = pots[i];
    return 0;
}

static void installUmc(const RtGame *game)
{
    currentGame = game;
    if (rtDetour(game->umcConnectedSymbol, umcConnected) != 0 || rtDetour(game->umcReadSymbol, umcRead) != 0 ||
        (game->umcStatusSymbol && rtDetour(game->umcStatusSymbol, umcConnected) != 0))
        log_error("Raw Thrills: UMC board hooks not installed");
}

static void installRioInput(const RtGame *game)
{
    currentGame = game;
    rtDetour("RIO_SW_State", rioSwState);
    rtDetour("RIO_SW_Count", rioSwCount);
    if (!game->rioEventSymbol || !game->ioLoopSymbol)
        return;
    *(void **)&rioEvent = rtSymbol(game->rioEventSymbol);
    rioLoopOrig = rtTrampoline(game->ioLoopSymbol, game->ioLoopPrologue);
    if (!rioEvent || !rioLoopOrig || rtDetour(game->ioLoopSymbol, rioLoopHook) != 0)
        log_error("Raw Thrills: RIO switch reports not installed");
}

// ---------------------------------------------------------------------------

void rtInstallInput(const RtGame *game)
{
    if (game->rioSwitches)
    {
        if (getConfig()->inputMode == 2 || game->rioDesktopKeys)
            installRioInput(game);
        if (game->umcReadSymbol)
            installUmc(game);
        return;
    }

    const char *gameInputMapsSymbol = game->gameInputMapsSymbol ? game->gameInputMapsSymbol : "GameInputMaps";

    if (game->inputAddMapPrologue)
    {
        // The evdev input decides which maps are dropped and added: set up
        // first, the maps are registered when main runs.
        currentGame = game;
        if (getConfig()->inputMode == 2 || game->ioDesktop)
            installEvdevInput(game);
        inputAddMapOrig = rtTrampoline("InputAddMap", game->inputAddMapPrologue);
        inputAddMap = inputAddMapOrig;
        if (!inputAddMapOrig || rtDetour("InputAddMap", inputAddMapHook) != 0)
            log_error("Raw Thrills: input hooks not installed");
        return;
    }
    // Games whose input map needs nothing added or dropped (Pink Panther
    // Jewel Heist): only their slots are fed.
    if (!game->gameInputMapsPrologue)
    {
        currentGame = game;
        if (getConfig()->inputMode == 2 || game->ioDesktop)
            installEvdevInput(game);
        return;
    }
    currentGame = game;
    *(void **)&inputAddMap = rtSymbol("InputAddMap");
    gameInputMapsOrig = rtTrampoline(gameInputMapsSymbol, game->gameInputMapsPrologue);
    if (!inputAddMap || !gameInputMapsOrig)
    {
        log_error("Raw Thrills: input hooks not installed");
        return;
    }
    rtDetour(gameInputMapsSymbol, gameInputMaps);
    if (getConfig()->inputMode == 2 || game->ioDesktop)
        installEvdevInput(game);
}
