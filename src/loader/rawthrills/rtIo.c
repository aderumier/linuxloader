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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "rawthrills.h"
#include "../config/config.h"
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
    const uint8_t *code = dlsym(RTLD_DEFAULT, "io_new_data_present");
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

// A coin is a count in the JVS state: turn each increment into a one-frame
// press of the coin switch.
static int coinPulse(int slot, int count)
{
    static int seen[2], pressed[2];
    if (pressed[slot])
    {
        pressed[slot] = 0;
        return 0;
    }
    if (count > seen[slot])
    {
        seen[slot]++;
        pressed[slot] = 1;
        return 1;
    }
    seen[slot] = count;
    return 0;
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

// Runs as one of the engine's I/O backends, once per frame inside InputLoop.
static int ioLoopHook(int dt)
{
    static int rangesSet;
    const RtIoInput *inputs = currentGame->ioInputs;
    JVSIO *io = getJVSIO();
    int ret = ioLoopOrig(dt);

    for (const RtIoInput *in = inputs; in->type != RT_IO_END; in++)
    {
        if (in->type == RT_IO_ANALOG || in->type == RT_IO_ANALOG_INVERTED)
        {
            int value = io->state.analogueChannel[in->source];
            if (!rangesSet && currentGame->ioRawRange)
                ioSetInputRawRange(in->io, 0, io->analogueMax);
            ioAnalogSet(in->io, in->type == RT_IO_ANALOG ? value : io->analogueMax - value);
            continue;
        }
        // Switches sharing an I/O slot are or-ed: handle the slot once.
        const RtIoInput *first = inputs;
        while (first->io != in->io || first->type == RT_IO_ANALOG || first->type == RT_IO_ANALOG_INVERTED)
            first++;
        if (first != in)
            continue;
        int held = 0;
        for (const RtIoInput *other = in; other->type != RT_IO_END; other++)
            if (other->io == in->io && other->type != RT_IO_ANALOG && other->type != RT_IO_ANALOG_INVERTED)
                held |= rtIoSwitchState(other, io);
        ioDigitalSet(in->io, dt, held);
    }
    rangesSet = 1;
    // The engine only reads the I/O slots in frames flagged as having new
    // data: a press edge set in another frame would be lost.
    *ioNewData = 1;
    return ret;
}

static void installEvdevInput(const RtGame *game)
{
    if (!game->ioInputs)
        return;
    ioGetInputAnalog = dlsym(RTLD_DEFAULT, "io_get_input_analog");
    ioGetInputDigital = dlsym(RTLD_DEFAULT, "io_get_input_digital");
    ioInputAnalogUpdate = dlsym(RTLD_DEFAULT, "io_input_analog_update");
    ioSetInputRawRange = dlsym(RTLD_DEFAULT, "io_set_input_raw_range");
    const char *ioLoopSymbol = game->ioLoopSymbol ? game->ioLoopSymbol : "io_sdl_loop";
    ioLoopOrig = rtTrampoline(ioLoopSymbol, game->ioLoopPrologue);
    ioNewData = findNewDataFlag();
    if (!ioGetInputAnalog || !ioGetInputDigital || !ioInputAnalogUpdate || !ioSetInputRawRange || !ioLoopOrig ||
        !ioNewData)
    {
        log_error("Raw Thrills: evdev input disabled, engine I/O functions not found");
        return;
    }
    rtDetour(ioLoopSymbol, ioLoopHook);
    evdevInput = 1;
}

// ---------------------------------------------------------------------------
// RIO API (games not built on the g5 engine): switch state and press count.

#define RIO_SWITCHES 64

static int rioHeld[RIO_SWITCHES];
static uint32_t rioCount[RIO_SWITCHES];

static int rioSwState(int sw)
{
    JVSIO *io = getJVSIO();
    int held = 0;

    if (sw < 0 || sw >= RIO_SWITCHES)
        return 0;
    for (const RtIoInput *in = currentGame->rioSwitches; in->type != RT_IO_END; in++)
        if (in->io == sw)
            held |= rtIoSwitchState(in, io);
    if (held && !rioHeld[sw])
        rioCount[sw]++;
    rioHeld[sw] = held;
    return held;
}

static uint32_t rioSwCount(int sw)
{
    return sw >= 0 && sw < RIO_SWITCHES ? rioCount[sw] : 0;
}

static void installRioInput(const RtGame *game)
{
    currentGame = game;
    rtDetour("RIO_SW_State", rioSwState);
    rtDetour("RIO_SW_Count", rioSwCount);
}

// ---------------------------------------------------------------------------

void rtInstallInput(const RtGame *game)
{
    if (game->rioSwitches)
    {
        if (getConfig()->inputMode == 2)
            installRioInput(game);
        return;
    }

    const char *gameInputMapsSymbol = game->gameInputMapsSymbol ? game->gameInputMapsSymbol : "GameInputMaps";

    if (!game->gameInputMapsPrologue)
        return;
    currentGame = game;
    inputAddMap = dlsym(RTLD_DEFAULT, "InputAddMap");
    gameInputMapsOrig = rtTrampoline(gameInputMapsSymbol, game->gameInputMapsPrologue);
    if (!inputAddMap || !gameInputMapsOrig)
    {
        log_error("Raw Thrills: input hooks not installed");
        return;
    }
    rtDetour(gameInputMapsSymbol, gameInputMaps);
    if (getConfig()->inputMode == 2)
        installEvdevInput(game);
}
