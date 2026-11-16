// Halo: Fireteam Raven on Windows (see ../rawthrills/g7/g7Halo.c for the
// switch records, the alias table and the gun calls): its RIO boards and
// IR guns answered, its switches from the keyboard, the mouse (player 1's
// gun) and XInput pads (a gun each, aimed with the sticks).
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <string.h>
#include "ll64.h"
#include "../rawthrills/g7/g7.h"

// The cabinets (see g7Halo.c): 4 and 8 seat two players, 3 and 9 four, the
// four player ones as two cabinets whose right one has the operator buttons.
static int cabinetPlayers(void)
{
    int cabinet = g7CabinetType();
    return cabinet == 4 || cabinet == 8 ? 2 : 4;
}
static int operatorCabinet(void)
{
    return cabinetPlayers() == 4 ? 1 : 0;
}

#define SW_GUN_TRIGGER(station, n) (0x208 + (station) * 0xc + (n))
#define SW_START(station)          (0x24e + (station))
#define SW_COIN(cab, n)            (0x257 + (cab) * 8 + (n))
#define SW_CAB_DIAG(cab)           (0x287 + (cab) * 4)
#define SW_CAB_SERVICE(cab)        (0x288 + (cab) * 4)
#define SW_CAB_VOL_UP(cab)         (0x289 + (cab) * 4)
#define SW_CAB_VOL_DOWN(cab)       (0x28a + (cab) * 4)

#define INPUT_UPDATE 0x54cdc0
#define RAW_RECORD 0x61e890
#define ALIAS_TABLE (0x1766f40 + 0x1122 * 8)
#define ALIAS_COUNT 0x1771850

static SYSV int gunConnected(int gun)
{
    long guns = g7EnvNum("HALO_GUNS", cabinetPlayers());
    return gun >= 0 && gun < (int)guns;
}

// Raw input records, shadowed (see g7Halo.c).
static SYSV uint32_t *(*devGetOrig)(int id);
static struct { int raw; uint32_t rec[20]; } devs[64];
static int devCount;

static SYSV uint32_t *devGet(int id)
{
    for (int i = 0; i < devCount; i++)
        if (devs[i].raw == id)
            return devs[i].rec;
    return devGetOrig(id);
}

static int rawInput(int id) { return id < 0x24e || id > 0x2ff; }

static int aliasSources(int logical, int *out, int max)
{
    const int *table = (const int *)ALIAS_TABLE;
    int count = *(const int *)ALIAS_COUNT, n = 0;
    for (int pass = 0; pass < 2 && n == 0; pass++)
        for (int i = 0; i < count && n < max; i++)
            if (table[i * 2 + 1] == logical && rawInput(table[i * 2]) == !pass)
                out[n++] = table[i * 2];
    return n;
}

// Keys (virtual-key codes), as on Linux. Operator entries name cabinet 0's
// switch, moved to the operator's cabinet; start buttons are -player.
static const struct { int vk, sw, operator; } keymap[] = {
    {VK_F2, SW_CAB_DIAG(0), 1},                                   // test
    {VK_F1, SW_CAB_SERVICE(0), 1}, {'P', SW_CAB_SERVICE(0), 1},   // service
    {VK_UP, SW_CAB_VOL_UP(0), 1}, {VK_DOWN, SW_CAB_VOL_DOWN(0), 1},
    {'5', SW_COIN(0, 0)}, {'U', SW_COIN(0, 0)}, {'6', SW_COIN(0, 1)},
    {'7', SW_COIN(1, 0)}, {'8', SW_COIN(1, 1)},
    {'1', -1}, {'I', -1}, {'2', -2}, {'O', -2}, {'3', -3}, {'4', -4},
};

// The gun's three buttons: trigger, reload, action.
static const int mouseButtons[3] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON};

static struct { int id, held, wasHeld; uint32_t heldFor; } switches[64];
static int switchCount;
// A bind drives a slot from a key or mouse button (BIND_KEY, its virtual
// key) or a pad's switch (BIND_PAD, player * PAD_SWITCHES + PAD_*).
enum { BIND_KEY, BIND_PAD };
enum { PAD_TRIGGER, PAD_RELOAD, PAD_ACTION, PAD_START, PAD_COIN, PAD_TEST, PAD_SERVICE, PAD_SWITCHES };
static struct { int kind, code, slot; } binds[160];
static int bindCount;
static int mouseSlots[3] = {-1, -1, -1};

static int playerStation(int player)
{
    static const int stations2p[] = {0, 3}, stations4p[] = {0, 1, 2, 3};
    int twoPlayer = cabinetPlayers() == 2;
    if (player < 0 || player >= (twoPlayer ? 2 : 4))
        return -1;
    return twoPlayer ? stations2p[player] : stations4p[player];
}

static int startSwitch(int player)
{
    int station = playerStation(player);
    return station < 0 ? 0 : SW_START(station);
}

static int slotFor(int raw)
{
    int slot;
    for (slot = 0; slot < switchCount; slot++)
        if (switches[slot].id == raw)
            return slot;
    if (switchCount == (int)LX_COUNT(switches) || devCount == (int)LX_COUNT(devs))
        return -1;
    switches[switchCount].id = raw;
    devs[devCount++].raw = raw;
    return switchCount++;
}

static int bindLogical(int kind, int code, int logical)
{
    int raws[8], n = aliasSources(logical, raws, 8), bound = 0;
    for (int i = 0; i < n; i++)
    {
        int slot = slotFor(raws[i]);
        if (slot < 0 || bindCount == (int)LX_COUNT(binds))
            continue;
        binds[bindCount].kind = kind;
        binds[bindCount].code = code;
        binds[bindCount].slot = slot;
        bindCount++;
        bound++;
    }
    return bound;
}

static void bindAll(void)
{
    int unmapped = 0;
    for (size_t i = 0; i < LX_COUNT(keymap); i++)
    {
        int sw = keymap[i].sw;
        if (sw < 0)
            sw = startSwitch(-sw - 1);
        else if (keymap[i].operator)
            sw += operatorCabinet() * 4;
        if (sw > 0 && !bindLogical(BIND_KEY, keymap[i].vk, sw))
            unmapped++;
    }
    for (int b = 0; b < 3; b++)
    {
        int raws[8];
        if (aliasSources(SW_GUN_TRIGGER(0, b), raws, 8) > 0)
            mouseSlots[b] = slotFor(raws[0]);
    }
    for (int p = 0; p < G7_MAX_PADS; p++)
    {
        int station = playerStation(p), code = p * PAD_SWITCHES;
        if (station < 0)
            continue;
        for (int b = 0; b < 3; b++)
            bindLogical(BIND_PAD, code + PAD_TRIGGER + b, SW_GUN_TRIGGER(station, b));
        bindLogical(BIND_PAD, code + PAD_START, SW_START(station));
        bindLogical(BIND_PAD, code + PAD_COIN, SW_COIN(p / 2, p % 2));
        bindLogical(BIND_PAD, code + PAD_TEST, SW_CAB_DIAG(operatorCabinet()));
        bindLogical(BIND_PAD, code + PAD_SERVICE, SW_CAB_SERVICE(operatorCabinet()));
    }
    lxLog("g7: halo: %d bindings over %d raw inputs (%d switches had no raw id)\n", bindCount, switchCount, unmapped);
}

// XInput pads: the triggers and face buttons are the gun's, the sticks aim.
static DWORD(WINAPI *xinputGetState)(DWORD, XINPUT_STATE *);
static unsigned padSwitches[G7_MAX_PADS];
static int padConnected[G7_MAX_PADS];

// Gun aim, in the game's 0..1 coordinates from the top left (the Y flip is
// done on the way out); -1: offscreen.
static float aimX[G7_MAX_PADS] = {-1, -1, -1, -1}, aimY[G7_MAX_PADS] = {-1, -1, -1, -1};
// A pad aims its gun once its stick moves; the mouse takes its gun back
// when it moves.
static int padAims[G7_MAX_PADS];

static float stickAxis(SHORT v)
{
    const int dead = 7849;
    if (v > dead)
        return (float)(v - dead) / (32767.f - dead);
    if (v < -dead)
        return (float)(v + dead) / (32768.f - dead);
    return 0.f;
}

static void padsPoll(float dt)
{
    static int loaded;
    if (!loaded++)
    {
        static const char *const dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        for (size_t i = 0; i < LX_COUNT(dlls) && !xinputGetState; i++)
        {
            HMODULE m = LoadLibraryA(dlls[i]);
            if (m)
                *(FARPROC *)&xinputGetState = GetProcAddress(m, "XInputGetState");
        }
    }
    for (int p = 0; p < G7_MAX_PADS; p++)
    {
        XINPUT_STATE st;
        unsigned held = 0;
        padSwitches[p] = 0;
        if (!xinputGetState || xinputGetState((DWORD)p, &st))
        {
            padConnected[p] = 0;
            continue;
        }
        if (!padConnected[p]++)
            lxLog("g7: halo: pad %d connected\n", p + 1);
        WORD b = st.Gamepad.wButtons;
        if (st.Gamepad.bRightTrigger > 64 || (b & XINPUT_GAMEPAD_A))
            held |= 1u << PAD_TRIGGER;
        if (st.Gamepad.bLeftTrigger > 64 || (b & (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_LEFT_SHOULDER)))
            held |= 1u << PAD_RELOAD;
        if (b & (XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_RIGHT_SHOULDER))
            held |= 1u << PAD_ACTION;
        if (b & XINPUT_GAMEPAD_START)
            held |= 1u << PAD_START;
        if (b & XINPUT_GAMEPAD_BACK)
            held |= 1u << PAD_COIN;
        if (b & XINPUT_GAMEPAD_RIGHT_THUMB)
            held |= 1u << PAD_TEST;
        if (b & XINPUT_GAMEPAD_LEFT_THUMB)
            held |= 1u << PAD_SERVICE;
        padSwitches[p] = held;

        // Both sticks aim (one for a coarse sweep is enough), the d-pad nudges.
        float vx = stickAxis(st.Gamepad.sThumbLX) + stickAxis(st.Gamepad.sThumbRX);
        float vy = -(stickAxis(st.Gamepad.sThumbLY) + stickAxis(st.Gamepad.sThumbRY));
        if (b & XINPUT_GAMEPAD_DPAD_LEFT) vx -= 0.3f;
        if (b & XINPUT_GAMEPAD_DPAD_RIGHT) vx += 0.3f;
        if (b & XINPUT_GAMEPAD_DPAD_UP) vy -= 0.3f;
        if (b & XINPUT_GAMEPAD_DPAD_DOWN) vy += 0.3f;
        if ((vx != 0.f || vy != 0.f) && !padAims[p])
        {
            padAims[p] = 1;
            if (aimX[p] < 0.f)
                aimX[p] = aimY[p] = 0.5f;
        }
        if (!padAims[p])
            continue;
        // Full deflection crosses the screen in a second and a half.
        aimX[p] += vx * dt / 1.5f;
        aimY[p] += vy * dt / 1.5f;
        aimX[p] = aimX[p] < 0.f ? 0.f : aimX[p] > 1.f ? 1.f : aimX[p];
        aimY[p] = aimY[p] < 0.f ? 0.f : aimY[p] > 1.f ? 1.f : aimY[p];
    }
}

// The mouse: player 1's gun (HALO_MOUSE_GUN another) while it moves over
// the picture.
static void mousePoll(int focused)
{
    static int gun = -1;
    static POINT last;
    POINT now;
    float x, y;
    if (gun < 0)
        gun = (int)g7EnvNum("HALO_MOUSE_GUN", 0);
    if (gun >= G7_MAX_PADS || !GetCursorPos(&now))
        return;
    if (now.x != last.x || now.y != last.y)
        padAims[gun] = 0;
    last = now;
    if (padAims[gun])
        return;
    if (focused && lxGamePointer(&x, &y))
    {
        aimX[gun] = x;
        aimY[gun] = y;
    }
    else
        aimX[gun] = aimY[gun] = -1.f;
}

static SYSV void gunGetPos(int gun, float *x, float *y)
{
    static int invertX = -1, invertY = -1;
    if (invertX < 0)
    {
        invertX = g7EnvNum("HALO_INVERT_X", 0) != 0;
        invertY = g7EnvNum("HALO_INVERT_Y", 1) != 0;
    }
    if (gun < 0 || gun >= G7_MAX_PADS || aimX[gun] < 0.f)
    {
        *x = *y = -1.f;
        return;
    }
    *x = invertX ? 1.f - aimX[gun] : aimX[gun];
    *y = invertY ? 1.f - aimY[gun] : aimY[gun];
}

// The game's per-frame input update, the records filled before it
// propagates them (see g7Halo.c).
static SYSV int (*inputOrig)(int);
static SYSV int inputUpdate(int arg)
{
    static int debug = -1;
    static ULONGLONG last;
    ULONGLONG now = GetTickCount64();
    int dt = last ? (int)(now - last) : 0, focused = lxGameFocused();

    last = now;
    if (debug < 0)
    {
        debug = g7EnvNum("HALO_INPUT_DEBUG", 0) != 0;
        lxLog("g7: halo: input update hooked\n");
        bindAll();
    }
    for (int i = 0; i < switchCount; i++)
        switches[i].held = 0;
    padsPoll((float)dt / 1000.f);
    mousePoll(focused);
    for (int i = 0; i < bindCount; i++)
    {
        int held = binds[i].kind == BIND_PAD
                       ? (padSwitches[binds[i].code / PAD_SWITCHES] >> (binds[i].code % PAD_SWITCHES)) & 1
                       : focused && (GetAsyncKeyState(binds[i].code) & 0x8000);
        if (held)
            switches[binds[i].slot].held = 1;
    }
    for (int b = 0; b < 3; b++)
        if (focused && mouseSlots[b] >= 0 && (GetAsyncKeyState(mouseButtons[b]) & 0x8000) && !padAims[0])
            switches[mouseSlots[b]].held = 1;
    for (int i = 0; i < switchCount; i++)
    {
        uint32_t *rec = devGet(switches[i].id);
        int held = switches[i].held;
        if (!rec)
            continue;
        rec[1] = held && !switches[i].wasHeld;
        rec[2] = !held && switches[i].wasHeld;
        switches[i].heldFor = held ? switches[i].heldFor + (uint32_t)dt + 1 : 0;
        rec[3] = switches[i].heldFor;
        if (debug && (rec[1] || rec[2]))
            lxLog("g7: halo: raw %#x %s\n", switches[i].id, rec[1] ? "pressed" : "released");
        switches[i].wasHeld = held;
    }
    return inputOrig(arg);
}

// ---------------------------------------------------------------------------

#define HALO_DIR "/pm/g7/halo"

const char *g7HaloMapPath(const char *p, char *buf)
{
    // The file check hashes the executable: the original, not the dump.
    if (!strcmp(p, HALO_DIR "/game"))
    {
        snprintf(buf, MAX_PATH, "%s\\game.orig", g7GameDir);
        return buf;
    }
    return NULL;
}

// The shader sources' path builder (a, b, c, out, size): the game's own
// shaders (gameshaders/<name>) include the shared headers as
// gameshaders/include/, which this install keeps, encrypted, in
// shaders/include. The envelope decrypts a file by the path the game asks
// for, so the game asks for that one: redirecting the open alone (as on
// Linux) hands the shield and sword shaders the encrypted bytes.
#define SHADER_PATH 0x4e5700
static SYSV int (*shaderPathOrig)(const char *, const char *, const char *, char *, int);
static SYSV int shaderPath(const char *a, const char *b, const char *c, char *out, int size)
{
    static const char gameIncludes[] = "/gameshaders/include/";
    int r = shaderPathOrig(a, b, c, out, size);
    char *g;
    if (r >= 0 && out && (g = strstr(out, gameIncludes)))
        memmove(g + 1, g + 5, strlen(g + 5) + 1);   // drop "game"
    return r;
}

static SYSV int one(void) { return 1; }
static SYSV int zero(void) { return 0; }

void g7HaloInstall(const G7Game *game)
{
    (void)game;
    g7Detour(0x6278d0, zero);   // RIO_Connected: 0
    g7Detour(0x6213f0, one);    // RIO2 boards expected (see g7Halo.c)
    *(void **)&inputOrig = g7HookNear(INPUT_UPDATE, 6, inputUpdate);
    *(void **)&devGetOrig = g7HookNear(RAW_RECORD, 8, devGet);
    g7Detour(0xdd4b10, gunConnected);
    g7Detour(0xdd6c60, gunGetPos);
    // "push %r15; push %r14; mov %rcx,%r15"
    *(void **)&shaderPathOrig = g7HookNear(SHADER_PATH, 7, shaderPath);
}
