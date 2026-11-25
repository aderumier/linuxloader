// Minecraft Dungeons Arcade on Windows (see ../rawthrills/g7/g7Minecraft.c,
// whose addresses and reasons this follows): the four stations' joysticks
// made virtual, the cabinet switches answered through the I/O board's raw
// records, the cabinet's setup and the card dispenser as Ultra Loader answers
// them, the code hash made over the unpatched code, and the card binder
// (g7McdBinder.cpp) drawn over the frame, its hand scanned as if held to the
// station's webcam.
//
// Input: the keyboard (MAME's default layout, one block of keys per station)
// while the game's window has the focus, and XInput pads, pad n station n.
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glad/gl.h>
#include "ll64.h"
#include "../rawthrills/g7/g7.h"
#include "../rawthrills/g7/g7McdBinder.h"

#undef g7Log
#define g7Log(...) lxLog("g7: minecraft: " __VA_ARGS__)

// Writes those bytes over the game's code.
static void patchBytes(uintptr_t at, const char *hex)
{
    uint8_t *p = (uint8_t *)at;
    for (; hex[0] && hex[1]; hex += 2)
    {
        unsigned v;
        sscanf(hex, "%2x", &v);
        *p++ = (uint8_t)v;
    }
}

static void *execPage(size_t size)
{
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
}

// ---------------------------------------------------------------------------
// The cabinet's joysticks: SDL 2.0's Linux joystick driver in the game
// replaced by one that always has four (see g7Minecraft.c).
#define JOY_INIT 0x658b40
#define JOY_COUNT 0x658c10
#define JOY_DETECT 0x658c20
#define JOY_NAME 0x658c40
#define JOY_INSTANCE 0x658c80
#define JOY_OPEN 0x658cc0
#define JOY_ATTACHED 0x659160
#define JOY_UPDATE 0x659180
#define JOY_CLOSE 0x659200
#define JOY_QUIT 0x659270
#define JOY_GUID_INDEX 0x6592f0
#define JOY_GUID 0x659370
#define SDL_PRIVATE_AXIS 0x657560
#define SDL_PRIVATE_BUTTON 0x657810
#define JOY_AXES 2
#define JOY_BUTTONS 3

static volatile int stationAxis[MCD_STATIONS][JOY_AXES];
static volatile unsigned stationButtons[MCD_STATIONS];

typedef struct
{
    int fd;
    void *self;
    char pad[0x10];
    const char *name;
    int index, axis[JOY_AXES];
    unsigned buttons;
} JoyData;

static const char *const joyNames[MCD_STATIONS] = {"Arcade Joystick 1", "Arcade Joystick 2", "Arcade Joystick 3",
                                                   "Arcade Joystick 4"};

static SYSV const char *joyName(int index)
{
    return index >= 0 && index < MCD_STATIONS ? joyNames[index] : NULL;
}

static SYSV int joyInstance(int index)
{
    return index >= 0 && index < MCD_STATIONS ? index : -1;
}

static SYSV int joyOpen(uint8_t *joystick, int index)
{
    JoyData *d;
    if (!joystick || index < 0 || index >= MCD_STATIONS || !(d = calloc(1, sizeof *d)))
        return -1;
    d->fd = -1;
    d->self = d;
    d->name = joyNames[index];
    d->index = index;
    *(int *)(joystick + 0x00) = index;
    *(int *)(joystick + 0x10) = JOY_AXES;
    *(int *)(joystick + 0x20) = 0;
    *(int *)(joystick + 0x30) = 0;
    *(int *)(joystick + 0x40) = JOY_BUTTONS;
    *(JoyData **)(joystick + 0x50) = d;
    g7Log("joystick %d opened (virtual station)\n", index + 1);
    return 0;
}

static SYSV void joyUpdate(uint8_t *joystick)
{
    SYSV int (*axis)(void *, uint8_t, int16_t) = (void *)SDL_PRIVATE_AXIS;
    SYSV int (*button)(void *, uint8_t, uint8_t) = (void *)SDL_PRIVATE_BUTTON;
    JoyData *d = joystick ? *(JoyData **)(joystick + 0x50) : NULL;
    if (!d || d->index < 0 || d->index >= MCD_STATIONS)
        return;
    for (int a = 0; a < JOY_AXES; a++)
    {
        int v = stationAxis[d->index][a];
        if (v != d->axis[a])
        {
            axis(joystick, (uint8_t)a, (int16_t)v);
            d->axis[a] = v;
        }
    }
    unsigned b = stationButtons[d->index];
    for (int i = 0; i < JOY_BUTTONS; i++)
        if ((b ^ d->buttons) & (1u << i))
            button(joystick, (uint8_t)i, (b >> i) & 1);
    d->buttons = b;
}

static SYSV void joyClose(uint8_t *joystick)
{
    if (!joystick)
        return;
    free(*(JoyData **)(joystick + 0x50));
    *(JoyData **)(joystick + 0x50) = NULL;
}

static void installJoysticks(void)
{
    patchBytes(JOY_INIT, "b804000000c3");
    patchBytes(JOY_COUNT, "b804000000c3");
    patchBytes(JOY_DETECT, "c3");
    patchBytes(JOY_QUIT, "c3");
    patchBytes(JOY_ATTACHED, "b801000000c3");
    patchBytes(JOY_GUID_INDEX, "31c031d2c3");
    patchBytes(JOY_GUID, "31c031d2c3");
    g7Detour(JOY_NAME, (void *)joyName);
    g7Detour(JOY_INSTANCE, (void *)joyInstance);
    g7Detour(JOY_OPEN, (void *)joyOpen);
    g7Detour(JOY_UPDATE, (void *)joyUpdate);
    g7Detour(JOY_CLOSE, (void *)joyClose);
}

// The cabinet's setup as Ultra Loader answers it, and the engine's allocator
// as it sizes it (see g7Minecraft.c).
static void installCabinet(void)
{
    patchBytes(0x4199c0, "b801000000c39090");
    patchBytes(0x4a3790, "b801000000c3");
    patchBytes(0x439620, "b801000000c39090");
    patchBytes(0x430cf0, "b801000000c39090");
    patchBytes(0x4d9659, "0f");
    patchBytes(0x4d9671, "00800000");
    patchBytes(0x4d9693, "00800000");
    patchBytes(0x4d96ca, "00800000");
    patchBytes(0x4d96d0, "00800000");
    *(uint32_t *)0x16de184 = 0x180000;
    *(uint32_t *)0x16de188 = 0x100000;
    // FMOD's output: the game sets PulseAudio (System::setOutput(10) at
    // 0x5e6412, "mov $0xa,%esi"), which Windows has not; ALSA (11) instead,
    // played on waveOut (alsa64.c).
    if (*(uint8_t *)0x5e6412 == 0xbe && *(uint32_t *)0x5e6413 == 10)
        *(uint32_t *)0x5e6413 = 11;
}

// ---------------------------------------------------------------------------
// The virtual card dispenser (see g7Minecraft.c): the idle requests taken
// off the four queues before each run of the dispenser's update.
#define DISP_UPDATE 0x41b010
#define DISP_RESUME 0x41b01d
#define DISP_AUDITS 0x117c65b
#define DISP_QUEUES 0x178a600
static const uint8_t dispOrig[13] = {0x41, 0x57, 0x41, 0x56, 0x48, 0x8d, 0x3d, 0x40, 0x16, 0xd6, 0x00, 0x41, 0x55};
static SYSV uint64_t (*dispTramp)(void);
static volatile int dispensed[MCD_STATIONS];

static int plausible(uint64_t p)
{
    return p >= 0x10000 && p < 0x7fffffff0000ull && !(p & 7);
}

static void drainQueues(void)
{
    for (int q = 0; q < MCD_STATIONS; q++)
    {
        uint8_t *Q = (uint8_t *)(uintptr_t)(DISP_QUEUES + q * 0x40);
        uint64_t node = *(volatile uint64_t *)Q;
        int ok = 1, n = 0;
        if (!node || Q[0x28])
            continue;
        for (uint64_t v = node; v; v = *(volatile uint64_t *)v)
        {
            uint64_t d = *(volatile uint64_t *)(v + 0x10);
            if (!plausible(v) || ++n > 4096 || (d && (d < 0x10000 || d >= 0x7fffffff0000ull)))
            {
                ok = 0;
                break;
            }
        }
        if (!ok)
            continue;
        while (node)
        {
            uint64_t next = *(volatile uint64_t *)node, data = *(volatile uint64_t *)(node + 0x10);
            const volatile uint8_t *d = (const volatile uint8_t *)(uintptr_t)data;
            if (d && !d[7] && !d[8])
            {
                uint64_t prev = *(volatile uint64_t *)(node + 8);
                if (prev)
                    *(volatile uint64_t *)prev = next;
                else
                    *(volatile uint64_t *)Q = next;
                if (next)
                    *(volatile uint64_t *)(next + 8) = prev;
                else
                    *(volatile uint64_t *)(Q + 8) = prev;
                if (*(volatile uint32_t *)(Q + 0x10))
                    (*(volatile uint32_t *)(Q + 0x10))--;
                *(volatile uint64_t *)node = 0;
                *(volatile uint64_t *)(node + 8) = 0;
                dispensed[d[4] < MCD_STATIONS ? d[4] : q]++;
            }
            node = next;
        }
    }
}

static SYSV uint64_t dispUpdate(void)
{
    drainQueues();
    return dispTramp();
}

static void installDispenser(void)
{
    uint8_t *at = (uint8_t *)DISP_UPDATE, *t;
    uint64_t v;
    int n = 0;
    if (memcmp(at, dispOrig, sizeof dispOrig))
    {
        g7Log("card dispenser update not the C83 one, no virtual dispenser\n");
        return;
    }
    if (!(t = execPage(64)))
        return;
    t[n++] = 0x41; t[n++] = 0x57;              // push r15
    t[n++] = 0x41; t[n++] = 0x56;              // push r14
    t[n++] = 0x48; t[n++] = 0xbf;              // movabs rdi, "GameCardAudits"
    v = DISP_AUDITS;
    memcpy(t + n, &v, 8);
    n += 8;
    t[n++] = 0x41; t[n++] = 0x55;              // push r13
    t[n++] = 0xff; t[n++] = 0x25;              // jmp *0(%rip)
    memset(t + n, 0, 4);
    n += 4;
    v = DISP_RESUME;
    memcpy(t + n, &v, 8);
    *(void **)&dispTramp = t;
    at[0] = 0x48; at[1] = 0xb8;                // movabs rax, dispUpdate
    v = (uint64_t)(uintptr_t)dispUpdate;
    memcpy(at + 2, &v, 8);
    at[10] = 0xff; at[11] = 0xe0;              // jmp rax
    at[12] = 0x90;
    g7Log("virtual card dispenser installed\n");
}

// ---------------------------------------------------------------------------
// The cabinet switches: the I/O board's raw records (see g7Minecraft.c).
#define RAW_RECORD 0x638500
#define RAW_FIRST 0x239
#define RAW_COUNT 0x60
#define ALIAS_COUNT 0x17d13f0
#define ALIAS_PAIRS 0x17cf3f0
#define SW_START(s) (0x239 + (s) * 4)
#define SW_COIN(s) (0x257 + (s) * 8)
#define SW_DIAG 0x256
#define SW_SERVICE 0x288
#define SW_BUTTON(s, n) (0x239 + (s) * 4 + 1 + (n))
static uint32_t rawRecords[RAW_COUNT][20];
static SYSV uint32_t *(*rawRecordOrig)(int);

static SYSV uint32_t *rawRecord(int id)
{
    if (id < 0)
        return NULL;
    if (id >= RAW_FIRST && id < RAW_FIRST + RAW_COUNT)
        return rawRecords[id - RAW_FIRST];
    return rawRecordOrig(id);
}

enum { SWI_START, SWI_COIN = SWI_START + MCD_STATIONS, SWI_DIAG = SWI_COIN + MCD_STATIONS, SWI_SERVICE,
       SWI_BUTTON, SWI_COUNT = SWI_BUTTON + MCD_STATIONS * 3 };
static volatile unsigned char switchHeld[SWI_COUNT];

static int switchLogical(int i)
{
    if (i >= SWI_BUTTON)
        return SW_BUTTON((i - SWI_BUTTON) / 3, (i - SWI_BUTTON) % 3);
    return i < SWI_COIN ? SW_START(i - SWI_START) : i < SWI_DIAG ? SW_COIN(i - SWI_COIN) : i == SWI_DIAG ? SW_DIAG : SW_SERVICE;
}

static void fillRecord(uint32_t *r, int held, int was, uint32_t heldFor)
{
    r[1] = held && !was;
    r[2] = !held && was;
    r[3] = heldFor;
}

// Once a frame (from the swap): each driven switch's raw records filled.
static void switchFrame(void)
{
    static ULONGLONG last;
    static unsigned char was[SWI_COUNT];
    static uint32_t heldFor[SWI_COUNT];
    static int said;
    ULONGLONG now = GetTickCount64();
    uint32_t dt = last ? (uint32_t)(now - last) : 16;
    int count = *(volatile int *)ALIAS_COUNT;
    const int *pairs = (const int *)ALIAS_PAIRS;
    last = now;
    for (int i = 0; i < SWI_COUNT; i++)
    {
        int held = switchHeld[i], logical = switchLogical(i), raws = 0;
        heldFor[i] = held ? heldFor[i] + dt + 1 : 0;
        for (int k = 0; k < count && k < 0x400; k++)
        {
            int raw = pairs[k * 2];
            if (pairs[k * 2 + 1] != logical || raw < RAW_FIRST || raw >= RAW_FIRST + RAW_COUNT)
                continue;
            fillRecord(rawRecords[raw - RAW_FIRST], held, was[i], heldFor[i]);
            raws++;
            if (held && !was[i])
                g7Log("switch %#x (raw %#x) pressed\n", logical, raw);
        }
        // no alias: the switch test's id is a raw input itself (START)
        if (!raws && logical >= RAW_FIRST && logical < RAW_FIRST + RAW_COUNT)
        {
            fillRecord(rawRecords[logical - RAW_FIRST], held, was[i], heldFor[i]);
            raws++;
            if (held && !was[i])
                g7Log("switch %#x pressed\n", logical);
        }
        if (!raws && held && !was[i] && said++ < 8)
            g7Log("switch %#x has no raw input\n", logical);
        was[i] = (unsigned char)held;
    }
}

// ---------------------------------------------------------------------------
// The code hash over the code as the dump has it, its two six-NOP branches
// put back (see g7Minecraft.c: the hash keys the scene data's decryption).
#define CODE_HASH 0x78d230
#define CODE_HASH_IMPL 0xb1e4a0
#define CODE_START 0x416b20
#define CODE_END 0x117bdb9
static uint8_t *pristineCode;

static SYSV long long codeHash(void *out, int n)
{
    SYSV long long (*hash)(const void *, long long, void *, int) = (void *)CODE_HASH_IMPL;
    return hash(pristineCode, CODE_END - CODE_START, out, n);
}

static void installCodeHash(void)
{
    static const struct { uintptr_t at; const char *orig; } branches[] = {
        {0x4a4b37, "\x0f\x85\xb6\x02\x00\x00"},
        {0x4b1e6b, "\x0f\x85\xa7\x00\x00\x00"},
    };
    char path[MAX_PATH];
    FILE *f;
    int ok;
    snprintf(path, sizeof path, "%s\\gamedump.exec", g7GameDir);
    if (!(pristineCode = malloc(CODE_END - CODE_START)) || !(f = fopen(path, "rb")))
    {
        g7Log("cannot read %s, the code hash sees the patches\n", path);
        free(pristineCode);
        pristineCode = NULL;
        return;
    }
    ok = fseek(f, CODE_START - 0x400000, SEEK_SET) == 0 &&
         fread(pristineCode, 1, CODE_END - CODE_START, f) == CODE_END - CODE_START;
    fclose(f);
    if (!ok)
    {
        free(pristineCode);
        pristineCode = NULL;
        return;
    }
    for (size_t i = 0; i < LX_COUNT(branches); i++)
    {
        uint8_t *at = pristineCode + (branches[i].at - CODE_START);
        if (!memcmp(at, "\x90\x90\x90\x90\x90\x90", 6))
            memcpy(at, branches[i].orig, 6);
    }
    g7Detour(CODE_HASH, (void *)codeHash);
}

// ---------------------------------------------------------------------------
// A loaded script with no NUL in it copied into a game allocation ending
// with zeroes, before it is parsed (see g7Minecraft.c's startup bridges).
#define BRIDGE_PAGE 0x70000000
#define SCRIPT_CALL 0x540523
#define SCRIPT_PARSE 0x516ba0
#define GAME_MALLOC 0x4db580
#define GAME_FREE 0x4d9f90

static SYSV char *terminateScript(char *text, unsigned len)
{
    SYSV void *(*gameMalloc)(size_t) = (void *)GAME_MALLOC;
    SYSV void (*gameFree)(void *) = (void *)GAME_FREE;
    char *copy;
    if (!text || len - 1 >= 0xffffff || memchr(text, 0, len))
        return NULL;
    if (!(copy = gameMalloc((size_t)len + 0x20)))
        return NULL;
    memcpy(copy, text, len);
    memset(copy + len, 0, 0x20);
    gameFree(text);
    g7Log("script of %u bytes NUL-terminated\n", len);
    return copy;
}

static void emitAbs(uint8_t **p, uint8_t op, uint64_t to) // call/jmp *0(%rip)
{
    uint8_t *q = *p;
    *q++ = 0xff;
    *q++ = op;
    *q++ = 0x02; *q++ = 0; *q++ = 0; *q++ = 0;
    *q++ = 0xeb; *q++ = 0x08;
    memcpy(q, &to, 8);
    q += 8;
    *p = q;
}

static void installScriptBridge(void)
{
    static const uint8_t scriptCall[5] = {0xe8, 0x78, 0x66, 0xfd, 0xff};
    static const uint8_t pre[] = {0x8b, 0x74, 0x24, 0x24, 0x57};
    static const uint8_t post[] = {0x5f, 0x48, 0x85, 0xc0, 0x48, 0x0f, 0x45, 0xf8, 0x48, 0x89, 0x7c, 0x24, 0x30};
    uint8_t *page, *p;
    int32_t rel;
    if (memcmp((void *)SCRIPT_CALL, scriptCall, 5))
    {
        g7Log("script parse call not the C83 one, no bridge\n");
        return;
    }
    // In reach of the call's rel32.
    page = VirtualAlloc((void *)BRIDGE_PAGE, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!page)
    {
        g7Log("no page for the startup bridge\n");
        return;
    }
    // mov esi,[rsp+0x24]; push rdi; call terminateScript; pop rdi;
    // test rax,rax; cmovne rdi,rax; mov [rsp+0x30],rdi; jmp SCRIPT_PARSE
    p = page;
    memcpy(p, pre, sizeof pre);
    p += sizeof pre;
    emitAbs(&p, 0x15, (uint64_t)(uintptr_t)terminateScript);
    memcpy(p, post, sizeof post);
    p += sizeof post;
    emitAbs(&p, 0x25, SCRIPT_PARSE);
    rel = (int32_t)((intptr_t)page - (intptr_t)(SCRIPT_CALL + 5));
    *(uint8_t *)SCRIPT_CALL = 0xe8;
    memcpy((void *)(SCRIPT_CALL + 1), &rel, 4);
}

// ---------------------------------------------------------------------------
// The card reader: the binder's hand put in the station's scan slot one card
// after another, under the game's own mutex, only on a scan screen (see
// g7Minecraft.c).
typedef struct
{
    char code[8];
    uint8_t zero, pending, notify, force;
} McdScanSlot;
#define MCD_SCAN_SLOTS ((volatile McdScanSlot *)0x178cae0)
#define MCD_SCAN_MUTEX(p) ((void *)(uintptr_t)(0x1c6f120 + (p) * 0x28))
#define MCD_SCAN_ENABLED (*(volatile uint8_t *)0x178cb10)

static SRWLOCK queueLock = SRWLOCK_INIT;
static char queue[MCD_STATIONS][MCD_HAND_MAX][9];
static int queueLen[MCD_STATIONS], queuePos[MCD_STATIONS];
// The game's mutexes are the pthread bridge's.
static SYSV int (*gameTrylock)(void *), (*gameUnlock)(void *);

void mcdPresent(int station, const char (*codes)[9], int count)
{
    if (station < 0 || station >= MCD_STATIONS)
        return;
    AcquireSRWLockExclusive(&queueLock);
    if (count > MCD_HAND_MAX)
        count = MCD_HAND_MAX;
    memcpy(queue[station], codes, sizeof queue[0][0] * count);
    queueLen[station] = count;
    queuePos[station] = 0;
    ReleaseSRWLockExclusive(&queueLock);
}

int mcdScanScreen(void)
{
    return MCD_SCAN_ENABLED != 0;
}

int mcdWaiting(int station)
{
    int n;
    AcquireSRWLockExclusive(&queueLock);
    n = queueLen[station] - queuePos[station];
    ReleaseSRWLockExclusive(&queueLock);
    return n;
}

static void scanTick(void)
{
    static int saidWaiting[MCD_STATIONS];
    AcquireSRWLockExclusive(&queueLock);
    for (int p = 0; p < MCD_STATIONS; p++)
    {
        volatile McdScanSlot *s = &MCD_SCAN_SLOTS[p];
        if (queuePos[p] >= queueLen[p])
            continue;
        if (!MCD_SCAN_ENABLED)
        {
            if (!saidWaiting[p]++)
                g7Log("P%d: %d card(s) wait for a card scan screen\n", p + 1, queueLen[p] - queuePos[p]);
            continue;
        }
        saidWaiting[p] = 0;
        if (gameTrylock(MCD_SCAN_MUTEX(p)))
            continue;
        if (!s->pending)
        {
            const char *c = queue[p][queuePos[p]++];
            memcpy((void *)s->code, c, 8);
            s->zero = 0;
            s->notify = 1;
            s->force = 0;
            s->pending = 1;
            g7Log("P%d card %s scanned (%d/%d)\n", p + 1, c, queuePos[p], queueLen[p]);
        }
        gameUnlock(MCD_SCAN_MUTEX(p));
    }
    ReleaseSRWLockExclusive(&queueLock);
}

// ---------------------------------------------------------------------------
// The keyboard, MAME's default layout: each station's joystick, its three
// buttons, its START and coin (virtual keys). The numeric keypad's Enter is
// the main Enter to Windows: player 4's third button is the keypad's +.
static const struct
{
    int up, down, left, right, button[3], start, coin;
} stationKeys[MCD_STATIONS] = {
    {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, {VK_LCONTROL, VK_LMENU, VK_SPACE}, '1', '5'},
    {'R', 'F', 'D', 'G', {'A', 'S', 'Q'}, '2', '6'},
    {'I', 'K', 'J', 'L', {VK_RCONTROL, VK_RSHIFT, VK_RETURN}, '3', '7'},
    {VK_NUMPAD8, VK_NUMPAD2, VK_NUMPAD4, VK_NUMPAD6, {VK_NUMPAD0, VK_DECIMAL, VK_ADD}, '4', '8'},
};

// The binder: F3..F6 open it for station 1..4 (again: close it); while it is
// open the keys and the pads drive it and the stations rest.
static const int binderOpenKeys[MCD_STATIONS] = {VK_F3, VK_F4, VK_F5, VK_F6};
static const struct { int vk, in; } binderKeys[] = {
    {VK_UP, MCD_IN_UP}, {'W', MCD_IN_UP}, {VK_DOWN, MCD_IN_DOWN}, {'S', MCD_IN_DOWN},
    {VK_LEFT, MCD_IN_LEFT}, {'A', MCD_IN_LEFT}, {VK_RIGHT, MCD_IN_RIGHT}, {'D', MCD_IN_RIGHT},
    {VK_SPACE, MCD_IN_PICK}, {'Z', MCD_IN_PICK},
    {VK_BACK, MCD_IN_BACK}, {'X', MCD_IN_BACK}, // not Escape: it quits the game
    {'C', MCD_IN_PLAYER}, {'V', MCD_IN_PLAYER},
    {'Q', MCD_IN_TAB_PREV}, {VK_PRIOR, MCD_IN_TAB_PREV}, {'E', MCD_IN_TAB_NEXT}, {VK_NEXT, MCD_IN_TAB_NEXT},
    {VK_RETURN, MCD_IN_SCAN},
    {VK_TAB, MCD_IN_CLEAR}, {VK_DELETE, MCD_IN_CLEAR},
    {'1', MCD_IN_P1}, {'2', MCD_IN_P2}, {'3', MCD_IN_P3}, {'4', MCD_IN_P4},
};

static unsigned char keyState[256], keyBlocked[256];
static int inputDebug;

static void binderKey(int vk, int down)
{
    for (int p = 0; p < MCD_STATIONS; p++)
        if (vk == binderOpenKeys[p])
        {
            if (!down)
                return;
            if (!mcdBinderIsOpen())
                mcdBinderOpen(p);
            else
                mcdBinderClose();
            return;
        }
    if (!mcdBinderIsOpen())
        return;
    for (size_t i = 0; i < LX_COUNT(binderKeys); i++)
        if (binderKeys[i].vk == vk)
            mcdBinderButton(binderKeys[i].in, down, 0);
}

static void readKeyboard(void)
{
    int focused = lxGameFocused();
    for (int vk = 1; vk < 256; vk++)
    {
        int down = focused && (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (down == keyState[vk])
            continue;
        keyState[vk] = (unsigned char)down;
        if (inputDebug)
            g7Log("key %#x %s\n", vk, down ? "down" : "up");
        if (!down)
            keyBlocked[vk] = 0;
        binderKey(vk, down);
    }
}

// XInput pads: pad n is station n. A, B and X (or Y) its three buttons, the
// left stick or the d-pad its joystick, Start its START; Select+Start opens
// and closes the binder, Select alone empties the hand there.
enum { PIN_SOUTH, PIN_EAST, PIN_WEST, PIN_START, PIN_SELECT, PIN_TL, PIN_TR, PIN_COUNT };
typedef struct
{
    int connected;
    unsigned buttons;  // PIN_* bits
    int axis[2];       // -32767..32767
    int dirs;          // MCD_IN_UP..RIGHT bits
    int startDuring, latched;
} Pad;
static Pad pads[MCD_STATIONS];
static DWORD(WINAPI *xinputGetState)(DWORD, XINPUT_STATE *);

static int stick(SHORT v)
{
    int x = v < -32767 ? -32767 : v;
    return x > -8000 && x < 8000 ? 0 : x;
}

static void padButton(Pad *p, int pin, int down)
{
    int select = (p->buttons >> PIN_SELECT) & 1, start = (p->buttons >> PIN_START) & 1;
    if (p->latched)
    {
        // the presses of Select+Start do nothing more until both are up
        if (!select && !start)
            p->latched = 0;
        if (pin == PIN_SELECT || pin == PIN_START)
            return;
    }
    if (down && ((pin == PIN_START && select) || (pin == PIN_SELECT && start)))
    {
        if (mcdBinderIsOpen())
            mcdBinderClose();
        else
            mcdBinderOpen((int)(p - pads));
        p->latched = 1;
        return;
    }
    if (!mcdBinderIsOpen())
        return;
    switch (pin)
    {
    case PIN_SOUTH: mcdBinderButton(MCD_IN_PICK, down, 1); break;
    case PIN_EAST: mcdBinderButton(MCD_IN_BACK, down, 1); break;
    case PIN_WEST: mcdBinderButton(MCD_IN_PLAYER, down, 1); break;
    case PIN_TL: mcdBinderButton(MCD_IN_TAB_PREV, down, 1); break;
    case PIN_TR: mcdBinderButton(MCD_IN_TAB_NEXT, down, 1); break;
    case PIN_START: mcdBinderButton(MCD_IN_SCAN, down, 1); break;
    case PIN_SELECT:
        if (down)
            p->startDuring = 0;
        else if (!p->startDuring)
        {
            mcdBinderButton(MCD_IN_CLEAR, 1, 1);
            mcdBinderButton(MCD_IN_CLEAR, 0, 1);
        }
        break;
    }
    if (pin == PIN_START && down && select)
        p->startDuring = 1;
}

static void readPads(void)
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
    for (int i = 0; i < MCD_STATIONS; i++)
    {
        Pad *p = &pads[i];
        XINPUT_STATE st;
        unsigned b = 0;
        int x, y, dirs;
        WORD w;
        if (!xinputGetState || xinputGetState((DWORD)i, &st))
        {
            if (p->connected)
                g7Log("pad %d disconnected\n", i + 1);
            memset(p, 0, sizeof *p);
            continue;
        }
        if (!p->connected++)
            g7Log("pad %d connected: Select+Start opens the card binder\n", i + 1);
        w = st.Gamepad.wButtons;
        b |= (w & XINPUT_GAMEPAD_A) ? 1u << PIN_SOUTH : 0;
        b |= (w & XINPUT_GAMEPAD_B) ? 1u << PIN_EAST : 0;
        b |= (w & (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y)) ? 1u << PIN_WEST : 0;
        b |= (w & XINPUT_GAMEPAD_START) ? 1u << PIN_START : 0;
        b |= (w & XINPUT_GAMEPAD_BACK) ? 1u << PIN_SELECT : 0;
        b |= (w & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 1u << PIN_TL : 0;
        b |= (w & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1u << PIN_TR : 0;
        for (int pin = 0; pin < PIN_COUNT; pin++)
            if ((b ^ p->buttons) & (1u << pin))
            {
                int down = (b >> pin) & 1;
                p->buttons = (p->buttons & ~(1u << pin)) | ((unsigned)down << pin);
                if (inputDebug)
                    g7Log("pad %d button %d %s\n", i + 1, pin, down ? "down" : "up");
                padButton(p, pin, down);
            }
        x = stick(st.Gamepad.sThumbLX);
        y = -stick(st.Gamepad.sThumbLY);
        if (w & XINPUT_GAMEPAD_DPAD_LEFT) x = -32767;
        if (w & XINPUT_GAMEPAD_DPAD_RIGHT) x = 32767;
        if (w & XINPUT_GAMEPAD_DPAD_UP) y = -32767;
        if (w & XINPUT_GAMEPAD_DPAD_DOWN) y = 32767;
        p->axis[0] = x;
        p->axis[1] = y;
        dirs = (y < -16000) << MCD_IN_UP | (y > 16000) << MCD_IN_DOWN | (x < -16000) << MCD_IN_LEFT |
               (x > 16000) << MCD_IN_RIGHT;
        for (int d = MCD_IN_UP; d <= MCD_IN_RIGHT; d++)
            if ((dirs ^ p->dirs) & (1 << d) && mcdBinderIsOpen())
                mcdBinderButton(d, (dirs >> d) & 1, 1);
        p->dirs = dirs;
    }
}

static int noInput;

static DWORD WINAPI poller(void *arg)
{
    int wasOpen = 0, autoTick = 0;
    // MCD_AUTOSCAN=<seconds>: P1's hand scanned on that period (tests).
    const char *autoEnv = getenv("MCD_AUTOSCAN");
    int autoPeriod = autoEnv ? atoi(autoEnv) * 200 : 0;
    (void)arg;
    g7Log("the four stations on the keyboard (MAME's layout) and XInput pads 1..4\n");
    g7Log("card binder: F3..F6 open it for P1..P4, Select+Start on a pad\n");
    for (;;)
    {
        if (!noInput)
        {
            readKeyboard();
            readPads();
        }
        if (autoPeriod > 0 && ++autoTick >= autoPeriod)
        {
            char codes[MCD_HAND_MAX][9];
            int n = mcdBinderHand(0, codes, MCD_HAND_MAX);
            autoTick = 0;
            if (n)
                mcdPresent(0, (const char(*)[9])codes, n);
        }
        scanTick();

        // the binder closed: the keys held then kept from the game until let go
        int open = mcdBinderIsOpen();
        if (open != wasOpen)
        {
            g7Log("card binder %s\n", open ? "opened" : "closed");
            if (!open)
                memcpy(keyBlocked, keyState, sizeof keyBlocked);
            wasOpen = open;
        }
#define KEYDOWN(k) (keyState[k] && !keyBlocked[k])
        for (int s = 0; s < MCD_STATIONS; s++)
        {
            const Pad *p = &pads[s];
            int ax[JOY_AXES] = {0, 0}, start = 0;
            unsigned b = 0;
            if (!open)
            {
                if (p->connected)
                {
                    ax[0] = p->axis[0];
                    ax[1] = p->axis[1];
                    b = p->buttons & 7; // south, east, west: the three buttons
                    start = (p->buttons >> PIN_START) & 1 && !((p->buttons >> PIN_SELECT) & 1);
                }
                for (int n = 0; n < 3; n++)
                    if (KEYDOWN(stationKeys[s].button[n]))
                        b |= 1u << n;
                if (KEYDOWN(stationKeys[s].left) != KEYDOWN(stationKeys[s].right))
                    ax[0] = KEYDOWN(stationKeys[s].left) ? -32767 : 32767;
                if (KEYDOWN(stationKeys[s].up) != KEYDOWN(stationKeys[s].down))
                    ax[1] = KEYDOWN(stationKeys[s].up) ? -32767 : 32767;
                start |= KEYDOWN(stationKeys[s].start);
            }
            switchHeld[SWI_START + s] = (unsigned char)start;
            for (int n = 0; n < 3; n++)
                switchHeld[SWI_BUTTON + s * 3 + n] = (b >> n) & 1;
            // a cabinet's coin, one for two stations: 5 or 6 for P1/P2, 7 or 8
            // for P3/P4
            switchHeld[SWI_COIN + s] =
                s < 2 && !open && (KEYDOWN(stationKeys[2 * s].coin) || KEYDOWN(stationKeys[2 * s + 1].coin));
            stationAxis[s][0] = ax[0];
            stationAxis[s][1] = ax[1];
            stationButtons[s] = b;
        }
        switchHeld[SWI_SERVICE] = !open && (KEYDOWN('9') || KEYDOWN(VK_F1)); // 9: MAME's service
        switchHeld[SWI_DIAG] = !open && KEYDOWN(VK_F2);
#undef KEYDOWN
        static int dispensedSaid[MCD_STATIONS];
        for (int s = 0; s < MCD_STATIONS; s++)
            if (dispensed[s] != dispensedSaid[s])
            {
                g7Log("P%d's owed card dispensed (%d so far)\n", s + 1, dispensed[s]);
                dispensedSaid[s] = dispensed[s];
            }
        Sleep(5);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Each frame, on the GL thread: the switches, then the binder over the frame.

// The binder's GL entry points: opengl32's, else the context's.
static void *glProc(const char *name)
{
    static HMODULE gl;
    void *p = (void *)wglGetProcAddress(name);
    if ((uintptr_t)p <= 3 || p == (void *)-1)
    {
        if (!gl)
            gl = LoadLibraryA("opengl32.dll");
        p = (void *)GetProcAddress(gl, name);
    }
    return p;
}

// The pointer over the game's frame, for the binder: 1 when on it.
int mcdPointer(int width, int height, float *x, float *y, int *button)
{
    float fx, fy;
    if (!lxGameFocused() || !lxGamePointer(&fx, &fy))
        return 0;
    *x = fx * (float)width;
    *y = fy * (float)height;
    *button = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    return 1;
}

static void frame(int width, int height)
{
    static int glLoaded;
    switchFrame();
    if (!mcdBinderIsOpen())
        return;
    if (!glLoaded)
        glLoaded = gladLoadGL((GLADloadfunc)glProc) ? 1 : -1;
    if (glLoaded > 0)
        mcdBinderDraw(NULL, 0, width, height);
}

// ---------------------------------------------------------------------------
// The PC/SC card reader the game looks for: one reader, no card (see
// g7Minecraft.c). Linux's LONG and DWORD are 64 bits.
typedef int64_t SC_LONG;
typedef uint64_t SC_DWORD;
#define SCARD_S_SUCCESS 0
#define SCARD_E_TIMEOUT 0x8010000A
#define SCARD_E_NO_SMARTCARD 0x8010000C
#define SCARD_E_INSUFFICIENT_BUFFER 0x80100008
#define SCARD_STATE_CHANGED 0x0002
#define SCARD_STATE_EMPTY 0x0010
#define SCARD_AUTOALLOCATE ((SC_DWORD)-1)

typedef struct
{
    const char *szReader;
    void *pvUserData;
    SC_DWORD dwCurrentState, dwEventState, cbAtr;
    unsigned char rgbAtr[33];
} SC_READERSTATE;

static const char scReaderName[] = "ACS ACR122U PICC Interface 00 00";

static SYSV SC_LONG scEstablish(SC_DWORD scope, const void *r1, const void *r2, uint64_t *ctx)
{
    g7Log("SCardEstablishContext (stub)\n");
    if (ctx)
        *ctx = 1;
    return SCARD_S_SUCCESS;
}
static SYSV SC_LONG scOk(void) { return SCARD_S_SUCCESS; }

static SYSV SC_LONG scListReaders(uint64_t ctx, const char *groups, char *readers, SC_DWORD *len)
{
    SC_DWORD need = sizeof scReaderName + 1;
    if (!len)
        return SCARD_E_INSUFFICIENT_BUFFER;
    if (readers && *len == SCARD_AUTOALLOCATE)
    {
        SYSV void *(*gameMalloc)(size_t) = lxBridge("malloc");
        char *buf = gameMalloc ? gameMalloc(need) : NULL;
        if (!buf)
            return SCARD_E_INSUFFICIENT_BUFFER;
        memcpy(buf, scReaderName, sizeof scReaderName);
        buf[sizeof scReaderName] = 0;
        *(char **)readers = buf;
        *len = need;
        return SCARD_S_SUCCESS;
    }
    if (!readers || *len < need)
    {
        *len = need;
        return readers ? SCARD_E_INSUFFICIENT_BUFFER : SCARD_S_SUCCESS;
    }
    memcpy(readers, scReaderName, sizeof scReaderName);
    readers[sizeof scReaderName] = 0;
    *len = need;
    return SCARD_S_SUCCESS;
}

static SYSV SC_LONG scGetStatusChange(uint64_t ctx, SC_DWORD timeout, SC_READERSTATE *states, SC_DWORD n)
{
    SC_LONG ret = SCARD_E_TIMEOUT;
    for (SC_DWORD i = 0; i < n; i++)
    {
        SC_DWORD ev = SCARD_STATE_EMPTY;
        if (!(states[i].dwCurrentState & SCARD_STATE_EMPTY))
        {
            ev |= SCARD_STATE_CHANGED;
            ret = SCARD_S_SUCCESS;
        }
        states[i].dwEventState = ev;
        states[i].cbAtr = 0;
    }
    if (ret == SCARD_E_TIMEOUT)
        Sleep((DWORD)(timeout == (SC_DWORD)-1 || timeout > 400 ? 400 : timeout));
    return ret;
}
static SYSV SC_LONG scNoCard(void) { return SCARD_E_NO_SMARTCARD; }
static SYSV SC_LONG scTransmit(uint64_t card, const void *spci, const unsigned char *sbuf, SC_DWORD slen,
                               void *rpci, unsigned char *rbuf, SC_DWORD *rlen)
{
    if (rlen)
        *rlen = 0;
    return SCARD_E_NO_SMARTCARD;
}

static const struct { const char *name; uintptr_t slot; void *fn; } scard[] = {
    {"SCardEstablishContext", 0x16d2c28, (void *)scEstablish},
    {"SCardReleaseContext", 0x16d3600, (void *)scOk},
    {"SCardCancel", 0x16d2f70, (void *)scOk},
    {"SCardListReaders", 0x16d2af8, (void *)scListReaders},
    {"SCardGetStatusChange", 0x16d3850, (void *)scGetStatusChange},
    {"SCardConnect", 0x16d3d08, (void *)scNoCard},
    {"SCardDisconnect", 0x16d3c20, (void *)scOk},
    {"SCardTransmit", 0x16d3cd0, (void *)scTransmit},
};

static void *mcdDlsym(const char *name)
{
    for (size_t i = 0; i < LX_COUNT(scard); i++)
        if (!strcmp(name, scard[i].name))
            return scard[i].fn;
    return NULL;
}

// ---------------------------------------------------------------------------

void g7MinecraftInstall(const G7Game *game)
{
    char hand[MCD_HAND_MAX][9];
    (void)game;
    for (size_t i = 0; i < LX_COUNT(scard); i++)
        *(void **)scard[i].slot = scard[i].fn;
    g7GameDlsymHook = mcdDlsym;
    *(void **)&gameTrylock = lxBridge("pthread_mutex_trylock");
    *(void **)&gameUnlock = lxBridge("pthread_mutex_unlock");
    // MCD_CARDS: P1's hand to begin with (comma separated card codes)
    mcdBinderSetHand(0, getenv("MCD_CARDS"));
    if (getenv("MCD_AUTOSCAN") && !mcdBinderHand(0, hand, MCD_HAND_MAX))
        mcdBinderSetHand(0, "a30E0000,a0000000,a10f0000,a20r0000,a40Q0000");
    installJoysticks();
    installCabinet();
    installDispenser();
    g7HookNear(RAW_RECORD, 8, (void *)rawRecord);
    // the accessor goes on past its negative-id test, which rawRecord makes
    *(void **)&rawRecordOrig = (void *)(RAW_RECORD + 8);
    installCodeHash();
    installScriptBridge();
    lxSwapHook = frame;
    if (getenv("MCD_BINDER_OPEN"))
        mcdBinderOpen(0);
    // MCD_NO_INPUT=1: no keyboard or pad read (test runs beside a desktop)
    noInput = getenv("MCD_NO_INPUT") != NULL;
    inputDebug = getenv("MCD_INPUT_DEBUG") != NULL;
    CloseHandle(CreateThread(NULL, 0, poller, NULL, 0, NULL));
}
