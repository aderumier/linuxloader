// Marvel Contest of Champions on Windows: what the players hold (unity.h's
// UnityInput, the game's GameInput indices, see unity/unityRio2.c and unityInput.c),
// from the keyboard while the game's window has it and two gamepads, sampled
// by a thread.
//
// The keyboard is MAME's layout, as on Linux: P1 the arrows and Left Ctrl,
// Left Alt, Space, Left Shift (light, medium, heavy, special attack); P2
// R F D G and A S Q W; 1 / 2 start, 5 / 6 coins, F2 test, 9 service, Page
// Up / Down the volume. The pads are SDL 3's gamepads (XInput, DualSense,
// DualShock, Switch...: the first two, players 1 and 2), as Batocera's
// generator maps them (_PAD_MCOC): the d-pad or the left stick, west light,
// north medium, east heavy, south special, Start, Back a coin; pad 1's
// shoulders the volume.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "../mcoc.h"

int mcocWinFocused(void);

enum
{
    IN_TEST = 0, IN_SERVICE = 1, IN_VOL_UP = 2, IN_VOL_DN = 3,
    IN_COIN = 10,  // + player
    IN_START = 13, // + player
    IN_UP = 17,    // + 8 * player: up, down, left, right, light, medium, heavy, special
};

static const struct { int in, vk; } keys[] = {
    {IN_TEST, VK_F2}, {IN_SERVICE, '9'}, {IN_VOL_UP, VK_PRIOR}, {IN_VOL_DN, VK_NEXT},
    {IN_COIN, '5'}, {IN_COIN + 1, '6'}, {IN_START, '1'}, {IN_START + 1, '2'},
    {IN_UP + 0, VK_UP}, {IN_UP + 1, VK_DOWN}, {IN_UP + 2, VK_LEFT}, {IN_UP + 3, VK_RIGHT},
    {IN_UP + 4, VK_LCONTROL}, {IN_UP + 5, VK_LMENU}, {IN_UP + 6, VK_SPACE}, {IN_UP + 7, VK_LSHIFT},
    {IN_UP + 8, 'R'}, {IN_UP + 9, 'F'}, {IN_UP + 10, 'D'}, {IN_UP + 11, 'G'},
    {IN_UP + 12, 'A'}, {IN_UP + 13, 'S'}, {IN_UP + 14, 'Q'}, {IN_UP + 15, 'W'},
};

static SRWLOCK lock = SRWLOCK_INIT;
static UnityInput state;
static SDL_Gamepad *gamepads[2];

// The pads, opened as they come (looked for once a second at most), players
// 1 and 2 in their order; one gone frees its player.
static void padsUpdate(void)
{
    static ULONGLONG lastScan;
    ULONGLONG now = GetTickCount64();
    int count = 0;
    SDL_JoystickID *ids;

    SDL_UpdateGamepads();
    for (int p = 0; p < 2; p++)
        if (gamepads[p] && !SDL_GamepadConnected(gamepads[p]))
        {
            unityLog("input: pad %d gone\n", p + 1);
            SDL_CloseGamepad(gamepads[p]);
            gamepads[p] = NULL;
        }
    if ((gamepads[0] && gamepads[1]) || now - lastScan < 1000)
        return;
    lastScan = now;
    ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; i++)
    {
        int open = 0;
        for (int p = 0; p < 2; p++)
            open |= gamepads[p] && SDL_GetGamepadID(gamepads[p]) == ids[i];
        for (int p = 0; p < 2 && !open; p++)
            if (!gamepads[p] && (gamepads[p] = SDL_OpenGamepad(ids[i])))
            {
                unityLog("input: pad %d is %s\n", p + 1, SDL_GetGamepadName(gamepads[p]));
                open = 1;
            }
    }
    SDL_free(ids);
}

static void pad(int p, UnityInput *in)
{
    SDL_Gamepad *g = gamepads[p];
    const int dead = 16000;
    int x, y, base = IN_UP + 8 * p;
    if (!g)
        return;
#define B(b) SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_##b)
    x = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
    y = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
    in->switches[base + 0] |= B(DPAD_UP) || y < -dead;
    in->switches[base + 1] |= B(DPAD_DOWN) || y > dead;
    in->switches[base + 2] |= B(DPAD_LEFT) || x < -dead;
    in->switches[base + 3] |= B(DPAD_RIGHT) || x > dead;
    in->switches[base + 4] |= B(WEST);
    in->switches[base + 5] |= B(NORTH);
    in->switches[base + 6] |= B(EAST);
    in->switches[base + 7] |= B(SOUTH);
    in->switches[IN_START + p] |= B(START);
    in->switches[IN_COIN + p] |= B(BACK);
    if (p == 0)
    {
        in->switches[IN_VOL_DN] |= B(LEFT_SHOULDER);
        in->switches[IN_VOL_UP] |= B(RIGHT_SHOULDER);
    }
#undef B
}

static DWORD WINAPI inputThread(void *arg)
{
    int pads;
    (void)arg;
    // The pads are read whether the game's window has the focus or not.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!(pads = SDL_Init(SDL_INIT_GAMEPAD)))
        unityLog("input: no gamepads (%s)\n", SDL_GetError());
    for (;;)
    {
        UnityInput in;
        memset(&in, 0, sizeof(in));
        for (int a = 0; a < 4; a++)
            in.analog[a] = -1.f;
        if (mcocWinFocused())
            for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
                if (GetAsyncKeyState(keys[i].vk) & 0x8000)
                    in.switches[keys[i].in] = 1;
        if (pads)
        {
            padsUpdate();
            pad(0, &in);
            pad(1, &in);
        }
        AcquireSRWLockExclusive(&lock);
        state = in;
        ReleaseSRWLockExclusive(&lock);
        Sleep(8);
    }
    return 0;
}

void unityInputInit(void)
{
    static LONG started;
    if (InterlockedExchange(&started, 1))
        return;
    for (int a = 0; a < 4; a++)
        state.analog[a] = -1.f;
    unityLog("input: the keyboard (MAME's layout) and gamepads 1 and 2\n");
    CloseHandle(CreateThread(NULL, 0, inputThread, NULL, 0, NULL));
}

const char *unityAnalogDevice(int a)
{
    (void)a;
    return NULL;
}

void unityInputSample(UnityInput *in)
{
    AcquireSRWLockShared(&lock);
    *in = state;
    ReleaseSRWLockShared(&lock);
}

// A player hidden while the card binder takes them: mcocGame.c's
// mcocInputHidePlayer / mcocInputSample, over unityInputSample.
