// Centipede Chaos's switches on Windows (see ../rawthrills/g7/g7Centipede.c
// for the records and raw ids): the keyboard while the game's window has the
// focus, and XInput pads.
#include <windows.h>
#include <xinput.h>
#include <string.h>
#include "ll64.h"
#include "../rawthrills/g7/g7.h"

#define INPUT_UPDATE 0x4fac90
#define RAW_RECORD 0x5e8cf0
// The I/O board wait: "mov $20000,%ebx" before a loop that polls for the
// RIO every 50 ms (0x46c840); no RIO here, so no wait.
#define RIO_WAIT 0x46c845

static const struct { int vk, raw; } inputs[] = {
    {VK_F2, 0x239},                        // diag (held a second: the test menu)
    {VK_F1, 0x23a}, {'P', 0x23a},          // service
    {VK_PRIOR, 0x23b}, {VK_NEXT, 0x23c},   // volume
    {'5', 0x23d}, {'6', 0x246}, {'7', 0x24f},   // coins
    {'1', 0x243}, {'2', 0x24c}, {'3', 0x255},   // start
    {VK_SPACE, 0x243}, {VK_LCONTROL, 0x243},
    {VK_LEFT, 0x23f}, {VK_RIGHT, 0x240}, {VK_UP, 0x241}, {VK_DOWN, 0x242},
    {'A', 0x24c},
    {'D', 0x248}, {'G', 0x249}, {'R', 0x24a}, {'F', 0x24b},
    {VK_RCONTROL, 0x255},
    {'J', 0x251}, {'L', 0x252}, {'I', 0x253}, {'K', 0x254},
    {VK_NUMPAD0, 0x2b6}, {VK_NUMPAD1, 0x2b7}, {VK_NUMPAD2, 0x2b8}, {VK_NUMPAD3, 0x2b9}, {VK_NUMPAD4, 0x2ba},
    {VK_NUMPAD5, 0x2bb}, {VK_NUMPAD6, 0x2bc}, {VK_NUMPAD7, 0x2bd}, {VK_NUMPAD8, 0x2be}, {VK_NUMPAD9, 0x2bf},
    {VK_MULTIPLY, 0x2c0}, {VK_DECIMAL, 0x2c1},
};
#define INPUTS (sizeof(inputs) / sizeof(inputs[0]))

static struct
{
    int raw, held, wasHeld;
    uint32_t heldFor;
    uint32_t record[20];
} switches[INPUTS];

#define PLAYERS 3
static const int playerButtons[PLAYERS] = {0x23f, 0x248, 0x251};
static const int playerCoins[PLAYERS] = {0x23d, 0x246, 0x24f};

static void holdRaw(int raw)
{
    for (size_t i = 0; i < INPUTS; i++)
        if (switches[i].raw == raw)
        {
            switches[i].held = 1;
            return;
        }
}

// XInput, from whichever DLL Windows has.
static DWORD(WINAPI *xinputGetState)(DWORD, XINPUT_STATE *);
static unsigned padHeld[G7_MAX_PADS];

void g7PadsPoll(int players)
{
    static int loaded;
    if (!loaded++)
    {
        static const char *const dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
        for (size_t i = 0; i < 3 && !xinputGetState; i++)
        {
            HMODULE m = LoadLibraryA(dlls[i]);
            if (m)
                *(FARPROC *)&xinputGetState = GetProcAddress(m, "XInputGetState");
        }
    }
    for (int p = 0; p < players && p < G7_MAX_PADS; p++)
    {
        XINPUT_STATE st;
        unsigned held = 0;
        if (xinputGetState && !xinputGetState((DWORD)p, &st))
        {
            WORD b = st.Gamepad.wButtons;
            SHORT x = st.Gamepad.sThumbLX, y = st.Gamepad.sThumbLY;
            if ((b & XINPUT_GAMEPAD_DPAD_UP) || y > 16000) held |= G7_PAD_UP;
            if ((b & XINPUT_GAMEPAD_DPAD_DOWN) || y < -16000) held |= G7_PAD_DOWN;
            if ((b & XINPUT_GAMEPAD_DPAD_LEFT) || x < -16000) held |= G7_PAD_LEFT;
            if ((b & XINPUT_GAMEPAD_DPAD_RIGHT) || x > 16000) held |= G7_PAD_RIGHT;
            if (b & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y)) held |= G7_PAD_BUTTON;
            if (b & XINPUT_GAMEPAD_START) held |= G7_PAD_START;
            if (b & XINPUT_GAMEPAD_BACK) held |= G7_PAD_SELECT;
            if (b & XINPUT_GAMEPAD_RIGHT_THUMB) held |= G7_PAD_TEST;
            if (b & XINPUT_GAMEPAD_LEFT_THUMB) held |= G7_PAD_SERVICE;
        }
        padHeld[p] = held;
    }
}

unsigned int g7PadHeld(int player)
{
    return player >= 0 && player < G7_MAX_PADS ? padHeld[player] : 0;
}

static SYSV uint32_t *(*rawRecordOrig)(int id);

static SYSV uint32_t *rawRecord(int id)
{
    if (id < 0)
        return NULL;
    for (size_t i = 0; i < INPUTS; i++)
        if (switches[i].raw == id)
            return switches[i].record;
    return rawRecordOrig(id);
}

static SYSV int (*inputUpdateOrig)(int);

static SYSV int inputUpdate(int arg)
{
    static ULONGLONG last;
    static int debug = -1;
    ULONGLONG now = GetTickCount64();
    int dt = last ? (int)(now - last) : 0, focused = lxGameFocused();

    last = now;
    if (debug < 0)
    {
        debug = g7GameEnv("INPUT_DEBUG", 0) != 0;
        lxLog("g7: centipede: input update hooked\n");
    }
    for (size_t i = 0; i < INPUTS; i++)
        switches[i].held = 0;
    for (size_t i = 0; focused && i < INPUTS; i++)
        if (GetAsyncKeyState(inputs[i].vk) & 0x8000)
            holdRaw(inputs[i].raw);
    g7PadsPoll(PLAYERS);
    for (int p = 0; p < PLAYERS; p++)
    {
        unsigned int pad = g7PadHeld(p);
        static const unsigned int bits[] = {G7_PAD_LEFT, G7_PAD_RIGHT, G7_PAD_UP, G7_PAD_DOWN, G7_PAD_BUTTON | G7_PAD_START};
        for (int b = 0; b < 5; b++)
            if (pad & bits[b])
                holdRaw(playerButtons[p] + b);
        if (pad & G7_PAD_SELECT)
            holdRaw(playerCoins[p]);
        if (pad & G7_PAD_TEST)
            holdRaw(0x239);
        if (pad & G7_PAD_SERVICE)
            holdRaw(0x23a);
    }
    for (size_t i = 0; i < INPUTS; i++)
    {
        uint32_t *rec = switches[i].record;
        size_t first = 0;
        int held;
        while (switches[first].raw != switches[i].raw)
            first++;
        if (first != i)
            continue;
        held = switches[i].held;
        rec[1] = held && !switches[i].wasHeld;
        rec[2] = !held && switches[i].wasHeld;
        if (debug && (rec[1] || rec[2]))
            lxLog("g7: centipede: raw %#x %s\n", switches[i].raw, rec[1] ? "pressed" : "released");
        switches[i].heldFor = held ? switches[i].heldFor + (uint32_t)dt + 1 : 0;
        rec[3] = switches[i].heldFor;
        switches[i].wasHeld = held;
    }
    return inputUpdateOrig(arg);
}

void g7CentipedeInstall(const G7Game *game)
{
    (void)game;
    if (*(uint8_t *)RIO_WAIT == 0xbb && *(uint32_t *)(RIO_WAIT + 1) == 20000)
        *(uint32_t *)(RIO_WAIT + 1) = 0;
    for (size_t i = 0; i < INPUTS; i++)
        switches[i].raw = inputs[i].raw;
    // holdRaw marks the first input of a raw id: the others share its record.
    *(void **)&inputUpdateOrig = g7HookNear(INPUT_UPDATE, 6, inputUpdate);
    g7HookNear(RAW_RECORD, 8, rawRecord);
    rawRecordOrig = (SYSV uint32_t * (*)(int))(RAW_RECORD + 8);
}
