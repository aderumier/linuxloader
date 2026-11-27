// Wangan Midnight Maximum Tune 5DX+ (WMN5r, export 2.20.02): what only this
// game needs, after the Pacloader fork's wmmt5/ modules (its "5DX+ export
// 2.20.02" address table, the build axylol/fivedxp targets). The display, GL
// and cabinet LAN are WMMT4's (namcoEs1Wangan). Stripped, the HASP HL, BngRw
// card reader library and network diagnostic linked in: found addresses,
// valid for this build only (the launcher knows it by its file CRC).

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "namcoEs1.h"
#include "namcoEs1Wmmt4Pb.h"
#include "namcoEs1Wmmt4Server.h"
#include "../common/importHook.h"
#include "../config/config.h"
#include "../input/evdevFfb.h"
#include "../../minhook/include/MinHook.h"
#include "../log/log.h"

#define HOOK __attribute__((force_align_arg_pointer))

// ---------------------------------------------------------------------------
// HASP HL (E1910 without): a flat 0xd40-byte image, two identical licence
// records, the cabinet's serial at 0xd00 and a sum and its complement at the
// end. The serial: 2676, the market digit (2: export; another market's is
// refused, E1911), the unit (0 drive, 1 terminal), the cabinet's number.

#define HASP_SIZE 0xd40
#define HASP_SYSTEM 0xd00
#define HASP_SERIAL "267620542069"

static uint8_t hasp[HASP_SIZE];

static uint32_t crc32Mpeg(const uint8_t *data, size_t n)
{
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= (uint32_t)data[i] << 24;
        for (int bit = 0; bit < 8; bit++)
            crc = crc & 0x80000000 ? crc << 1 ^ 0x04c11db7 : crc << 1;
    }
    return crc;
}

static void putBe32(uint8_t *p, uint32_t value)
{
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}

// The NBLINE points (the cabinet's prepaid plays): a header (initialised),
// then two records, cost per play, limit and used, big-endian, an MPEG-2
// CRC32 each. The game wants used <= limit - cost (else E "NBLINE POINT
// balance error", 538) and asks for a charge below 50 plays left: nothing
// used of a large limit.
#define POINT_LIMIT 1000000

static void haspImage(void)
{
    uint8_t sum = 0;

    hasp[0] = 1;
    for (int i = 0; i < 2; i++)
    {
        uint8_t *point = hasp + 16 + 16 * i;
        putBe32(point, 1);
        putBe32(point + 4, POINT_LIMIT);
        putBe32(point + 8, 0);
        putBe32(point + 12, crc32Mpeg(point, 12));
    }
    memcpy(hasp + HASP_SYSTEM, HASP_SERIAL, 12);
    for (int i = 0; i < 62; i++)
        sum += hasp[HASP_SYSTEM + i];
    hasp[HASP_SYSTEM + 62] = sum;
    hasp[HASP_SYSTEM + 63] = ~sum;
}

HOOK static int haspLogin(uint32_t feature, const void *vendorCode, uint32_t *handle)
{
    (void)feature;
    (void)vendorCode;
    if (handle)
        *handle = 1;
    return 0;
}

HOOK static int haspNothing(void)
{
    return 0;
}

HOOK static int haspGetSize(uint32_t handle, uint32_t fileId, uint32_t *size)
{
    (void)handle;
    (void)fileId;
    if (size)
        *size = HASP_SIZE;
    return 0;
}

static int haspRange(uint32_t offset, uint32_t length)
{
    return offset <= HASP_SIZE && length <= HASP_SIZE - offset;
}

HOOK static int haspRead(uint32_t handle, uint32_t fileId, uint32_t offset, uint32_t length, void *buffer)
{
    (void)handle;
    (void)fileId;
    if (!buffer || !haspRange(offset, length))
        return 1; // HASP_MEM_RANGE
    memcpy(buffer, hasp + offset, length);
    return 0;
}

HOOK static int haspWrite(uint32_t handle, uint32_t fileId, uint32_t offset, uint32_t length, const void *buffer)
{
    (void)handle;
    (void)fileId;
    if (!buffer || !haspRange(offset, length))
        return 1;
    memcpy(hasp + offset, buffer, length);
    return 0;
}

// ---------------------------------------------------------------------------
// The game's three loggers (self, severity 1..4, format, ...), silent below
// 4 on the cabinet: on stderr with NAMCO_GAME_LOG set.

static int gameLog;

HOOK static void logMes(void *self, int severity, const char *format, ...)
{
    char message[2048];
    va_list args;

    (void)self;
    if (!gameLog || !format)
        return;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    size_t n = strlen(message);
    fprintf(stderr, "WMMT5 [%d]: %s%s", severity, message, n && message[n - 1] == '\n' ? "" : "\n");
}

// ---------------------------------------------------------------------------
// The network, as a cabinet alone on a healthy shop LAN, offline (there is
// no 5DX+ server here). Its checks ask the cabinet's monitoring daemon,
// which never answers (E0001 at boot): the verdicts are filled in.

// The content router: 192.168.92.254 (network byte order).
HOOK static uint32_t contentRouter(void)
{
    return 0xfe5ca8c0u;
}

// The cabinet link's status, an error_code returned through a hidden pointer
// the callee pops (ret $4), the object the second argument: the original
// runs, its verdict is cleared (no other cabinets), its category kept.
static void *realNetworkState;

// No stack realignment here: the object is read from the caller's frame.
__attribute__((stdcall, noinline)) static uint32_t *networkState(uint32_t *result)
{
    void *self = ((void **)__builtin_frame_address(0))[3];
    void *real = realNetworkState;

    if (!result)
        return result;
    __asm__ volatile("pushl %1\n\tpushl %0\n\tcall *%2\n\taddl $4, %%esp"
                     : "+r"(result), "+r"(self), "+r"(real)
                     :
                     : "eax", "ecx", "edx", "memory", "cc");
    result[0] = 0;
    return result;
}

HOOK static int returnZero(void)
{
    return 0;
}

HOOK static void doNothing(void)
{
}

// A token with a leading '@' passes unencrypted.
static int (*realDecryptToken)(char *, int *, char *, void *);

HOOK static int decryptToken(char *destination, int *destinationSize, char *source, void *context)
{
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT5: decrypt token %.40s (size %d)\n", source ? source : "(null)",
                destinationSize ? *destinationSize : -1);
    if (source && source[0] == '@')
    {
        size_t length = strlen(source + 1);
        memcpy(destination, source + 1, length);
        if (destinationSize)
            *destinationSize = (int)length;
        return 0;
    }
    return realDecryptToken(destination, destinationSize, source, context);
}

// The network monitor's steps: each answered as a healthy LAN would.
#define MONITOR_STEP 0x220
#define MONITOR_CABLE 0x1dc
#define MONITOR_GATEWAY 0x1e0
#define MONITOR_SHOP_ROUTER 0x178
#define MONITOR_HOPS 0x1f4
#define MONITOR_HOPS_STATE 0x1ec
#define MONITOR_NTP 0x1d8
#define MONITOR_ONLINE 0x1fc
#define MONITOR_RENEW 0x204
#define MONITOR_SERIAL 0xc0
#define VERDICT_GOOD 3

static int (*realMonitorStep)(uint8_t *, int);

HOOK static int monitorStep(uint8_t *monitor, int argument)
{
    if (monitor)
    {
#define FIELD(offset) (*(int *)(monitor + (offset)))
        if (getenv("WMMT5_NET_TRACE"))
        {
            static int last = -1;
            if (FIELD(MONITOR_STEP) != last)
                fprintf(stderr, "WMMT5: network monitor step %d\n", last = FIELD(MONITOR_STEP));
        }
        // The unit check starts the monitor with the dongle's serial
        // (std::string at +0xc0, its length 12 bytes before its text), which
        // authentication sends: until then it stays idle (else an empty
        // serial, std::out_of_range).
        const char *serial = *(const char **)(monitor + MONITOR_SERIAL);
        if (!serial || *(const int *)(serial - 12) <= 0)
            return 0;
        switch (FIELD(MONITOR_STEP))
        {
        case 0:
            FIELD(MONITOR_STEP) = 1;
            break;
        case 1: // the cable
            FIELD(MONITOR_STEP) = 2;
            FIELD(MONITOR_CABLE) = VERDICT_GOOD;
            break;
        case 3:
            FIELD(MONITOR_GATEWAY) = VERDICT_GOOD;
            break;
        case 4:
            FIELD(MONITOR_SHOP_ROUTER) = VERDICT_GOOD;
            break;
        case 5: // the shop router one hop away
            FIELD(MONITOR_HOPS) = 1;
            FIELD(MONITOR_HOPS_STATE) = VERDICT_GOOD;
            break;
        case 7:
            FIELD(MONITOR_NTP) = VERDICT_GOOD;
            break;
        case 14:
            FIELD(MONITOR_ONLINE) = 0;
            break;
        case 15:
            FIELD(MONITOR_CABLE) = 0;
            break;
        case 16:
            FIELD(MONITOR_RENEW) = 0;
            break;
        }
#undef FIELD
    }
    return realMonitorStep(monitor, argument);
}

// eth0's refresh (its name, address, mask, gateway and MAC, from the host's
// interfaces and routes): the cabinet's eth0 (192.168.92.11/24, gateway
// .254, the content router), the host's MAC (ALL.Net's authentication reads
// it: an empty one threw std::out_of_range).
typedef struct
{
    int32_t errorValue;
    uint32_t errorCategory;
    uint32_t name;
    uint32_t address, netmask, gateway;
    uint8_t dns[12]; // std::vector
    uint32_t mac;    // std::string, 12 hex digits
} Interface;

_Static_assert(__builtin_offsetof(Interface, mac) == 36, "WMMT5 interface layout");

static int (*realInterfaceUpdate)(Interface *);

HOOK static int interfaceUpdate(Interface *state)
{
    int updated = realInterfaceUpdate(state) & 0xff;
    if (!state)
        return updated;
    const uint8_t *eth0 = namcoEs1CurrentGame()->eth0;
    void (*assign)(void *, const char *) = (void (*)(void *, const char *))0x0805699c; // std::string::assign, PLT
    unsigned char address[4], mask[4], mac[6];
    char macText[13];
    namcoEs1HostInterface(address, mask, mac);
    snprintf(macText, sizeof(macText), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // Assigned only when different: the strings are the game's shared
    // (reference-counted) ones, read by its other threads (authentication).
    const char *name = *(const char **)&state->name, *current = *(const char **)&state->mac;
    if (!name || strcmp(name, "eth0"))
        assign(&state->name, "eth0");
    if (!current || strcmp(current, macText))
        assign(&state->mac, macText);
    state->address = (uint32_t)eth0[0] << 24 | eth0[1] << 16 | eth0[2] << 8 | eth0[3];
    state->netmask = (uint32_t)eth0[4] << 24 | eth0[5] << 16 | eth0[6] << 8 | eth0[7];
    state->gateway = (state->address & state->netmask) | 254u;
    state->errorValue = 0;
    return 1;
}

// The interfaces' list, which the refresh walks reading every address
// (a host interface without one, a VPN's, crashed it): lo and eth0, the
// cabinet's (192.168.92.11/24), in one block (libc's freeifaddrs frees it).
HOOK static int getInterfaces(struct ifaddrs **list)
{
    struct block
    {
        struct ifaddrs entry[2];
        struct sockaddr_in address[2], netmask[2], broadcast[2];
        char name[2][8];
    } *b = calloc(1, sizeof(*b));
    const uint8_t *eth0 = namcoEs1CurrentGame()->eth0;

    if (!list || !b)
        return -1;
    for (int i = 0; i < 2; i++)
    {
        uint32_t address = i ? (uint32_t)eth0[0] << 24 | eth0[1] << 16 | eth0[2] << 8 | eth0[3] : INADDR_LOOPBACK;
        uint32_t mask = i ? (uint32_t)eth0[4] << 24 | eth0[5] << 16 | eth0[6] << 8 | eth0[7] : 0xff000000u;
        strcpy(b->name[i], i ? "eth0" : "lo");
        b->address[i].sin_family = b->netmask[i].sin_family = b->broadcast[i].sin_family = AF_INET;
        b->address[i].sin_addr.s_addr = htonl(address);
        b->netmask[i].sin_addr.s_addr = htonl(mask);
        b->broadcast[i].sin_addr.s_addr = htonl(address | ~mask);
        b->entry[i].ifa_name = b->name[i];
        b->entry[i].ifa_flags = IFF_UP | IFF_RUNNING | (i ? IFF_BROADCAST | IFF_MULTICAST : IFF_LOOPBACK);
        b->entry[i].ifa_addr = (struct sockaddr *)&b->address[i];
        b->entry[i].ifa_netmask = (struct sockaddr *)&b->netmask[i];
        b->entry[i].ifa_broadaddr = (struct sockaddr *)&b->broadcast[i];
    }
    b->entry[0].ifa_next = &b->entry[1];
    *list = b->entry;
    return 0;
}

// The steering board ("STR3", /dev/ttyS0) is not the loader's: its protocol
// is not WMMT4's STR2 one, and is unknown (the Pacloader fork's never left
// its initial wait either). A board that does not answer stops the cabinet
// (E2212 "STR PCB Link error"); its driver made on a port that cannot be
// opened crashed some boots (boost::asio). Its initialisation (0x80e6020:
// opens the port, makes the driver, 1 when made) answers "none", as a
// cabinet without one (the Windows arcade loader turns the board off in the
// settings): STEERING CHECK NG at boot, then the game. The force feedback
// is the game's own forces (below).
HOOK static int str3Init(const char *device)
{
    (void)device;
    return 0;
}

// The wheel's force feedback, as the Windows arcade loader plays 5DX+'s:
// the game hands its drive board's forces to its steering controller each
// frame (0x80de440: tire slip -1..1, centering spring 0..1, friction 0..1,
// collision -1..1, the side positive from the right), whether the board is
// there or not. Played on the evdev wheel: the spring as the centering
// spring (a light one until the game sets one), a collision as a push away
// from its side, the road's joints and stripes (collisions of 0.209..0.311)
// and the tire slip as a vibration (a sine); friction (a steady 0.5) is not
// played, as on Windows. WMMT5_FFB_TRACE: the forces on stderr.
static void (*realSetForces)(void *, float, float, float, float);
static int ffbTrace, ffbSpringSeen;

HOOK static void setForces(void *self, float slip, float spring, float friction, float collision)
{
    static unsigned n;
    realSetForces(self, slip, spring, friction, collision);
    if (ffbTrace && n++ % 60 == 0)
        fprintf(stderr, "WMMT5 FFB: slip %.3f spring %.3f friction %.3f collision %.3f\n", slip, spring, friction,
                collision);
#define FINITE(v) ((v) == (v) && (v) <= 1.0f && (v) >= -1.0f ? (v) : 0.0f)
    spring = FINITE(spring);
    collision = FINITE(collision);
    slip = FINITE(slip);
#undef FINITE
    if (spring >= 0.001f)
        ffbSpringSeen = 1;
    float a = collision < 0 ? -collision : collision, s = slip < 0 ? -slip : slip;
    int joints = a >= 0.209f && a <= 0.311f;
    evdevFfbSpring(ffbSpringSeen ? (spring < 0 ? 0 : spring) : 0.3f);
    evdevFfbConstant(joints ? 0.0f : -collision);
    evdevFfbVibration(joints && a > s ? a : s);
}

// ---------------------------------------------------------------------------
// The Banapassport reader (BngRw, /dev/ttyS3; E0712 without): each command
// done at once, into the caller's block (result at +8). Online, a card is on
// it as soon as the game waits for one: WMMT4's card (namcoEs1Wmmt4Card.c,
// save/server/card.ini), handed to the game's callback as the library would
// (device, status, the 168-byte record, the caller's block: the record
// copied to +0x10, the status to +8).

#define BNG_OK 1
#define BNG_NO_CARD (-1)

static int done(uint8_t *block)
{
    if (block)
        *(int *)(block + 8) = 0;
    return BNG_OK;
}

HOOK static int banaAttach(int a, int b, int c, int d, void *e, uint8_t *block)
{
    (void)a, (void)b, (void)c, (void)d, (void)e;
    return done(block);
}

HOOK static int banaRequest(int a, int b, void *c, uint8_t *block)
{
    (void)a, (void)b, (void)c;
    return done(block);
}

HOOK static int banaCancel(void)
{
    return BNG_OK;
}

HOOK static int banaSendUrl(int a, int b, int c, int d, int e, int f, void *g, uint8_t *block)
{
    (void)a, (void)b, (void)c, (void)d, (void)e, (void)f, (void)g;
    return done(block);
}

HOOK static int banaReset(int a, void *b, uint8_t *block)
{
    (void)a, (void)b;
    return done(block);
}

typedef void (*BanaTouched)(int device, int status, const void *record, void *user);

// The callback's work done in place, as the Pacloader fork does (calling it
// here deadlocks: it takes a lock the caller holds): the record copied to
// the caller's block (+0x10), the status (+8) 0.
HOOK static int banaWaitTouch(int reader, int a, uint32_t b, BanaTouched touched, uint8_t *block)
{
    (void)a, (void)b, (void)touched;
    if (reader != 0 || !block || !getConfig()->wmmt4Server)
        return BNG_NO_CARD;
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT5 card: on the reader\n");
    memcpy(block + 0x10, namcoEs1Wmmt4CardRecord(), 168);
    return done(block);
}

// ---------------------------------------------------------------------------
// The card dispenser (STR400 frames: STX, command, 0, length, payload, sum,
// ETX), else E2301. A drive cabinet's holds no cards.

static uint8_t vendorReply[16];
static int vendorReplyLength, vendorReset;

static void vendorAnswer(uint8_t command, const uint8_t *payload, int n)
{
    uint8_t sum = (uint8_t)(command + n);
    vendorReply[0] = 0x02;
    vendorReply[1] = command;
    vendorReply[2] = 0;
    vendorReply[3] = (uint8_t)n;
    for (int i = 0; i < n; i++)
    {
        vendorReply[4 + i] = payload[i];
        sum += payload[i];
    }
    vendorReply[4 + n] = sum;
    vendorReply[5 + n] = 0x03;
    vendorReplyLength = 6 + n;
}

HOOK static int vendorSend(int fd, const uint8_t *data, int length)
{
    (void)fd;
    if (!data || length < 3 || data[0] != 0x02 || data[length - 1] != 0x03)
        return length;
    switch (data[1])
    {
    case 0x30: // reset
        vendorReset = 1;
        vendorAnswer(0x30, (const uint8_t[]){'1'}, 1);
        break;
    case 0x31: // carry
        vendorAnswer(0x31, (const uint8_t[]){'0'}, 1);
        break;
    case 0x32: // status: ready (0x40) or not (0x20)
        vendorAnswer(0x32, (const uint8_t[]){'1', vendorReset ? 0x40 : 0x20, '1', 0}, 4);
        break;
    case 0x34: // eject
    case 0x41: // carry drive
        vendorAnswer(data[1], (const uint8_t[]){'1'}, 1);
        break;
    }
    return length;
}

HOOK static int vendorReceive(int fd, uint8_t *data, int length)
{
    (void)fd;
    if (!data || !vendorReplyLength || (length > 0 && length < vendorReplyLength))
        return 0;
    memcpy(data, vendorReply, vendorReplyLength);
    int n = vendorReplyLength;
    vendorReplyLength = 0;
    return n;
}

// ---------------------------------------------------------------------------
// The store's Wangan terminal, after the Windows arcade loader's
// (wmmt5_terminal.rs): online, the drive cabinet links to it (the terminal
// link, which the Banapassport cards need) and sends its game server
// requests to its HTTPS proxy (its address, port 50765: the loader's
// server). Its packets go to the cabinet's socket (any address, 50765) on
// the loopback, from 127.0.0.4 (the cabinet's own come back from
// 127.0.0.1): every 100 ms, the link group (this build's: 2), the
// terminal's node (4), the length (header and message, LE), a
// wm5.protobuf.Message, the CRC-32/MPEG-2 of the rest (BE). The message: an
// idle terminal's race car and course, the heartbeat (frame, the terminal's
// serial: the cabinet's with its unit digit 1, online), the store settings
// (coin costs, free play WMMT5_FREEPLAY=1, no closing time, the cabinet's own
// software revision, the sync time: now), the information, the feature
// version. The cabinet links once the settings' revision is its own
// (0xbed6364, known after its update check).

#define TERMINAL_ADDRESS 0x7f000004 // 127.0.0.4
#define TERMINAL_NODE 4
#define LINK_GROUP 2
#define LAN_PORT 50765
#define SOFTWARE_REVISION 0x0bed6364

static volatile int terminalRunning;

static uint32_t crc32Mpeg2(const uint8_t *data, size_t n)
{
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < n; i++)
    {
        crc ^= (uint32_t)data[i] << 24;
        for (int bit = 0; bit < 8; bit++)
            crc = crc & 0x80000000 ? crc << 1 ^ 0x04c11db7 : crc << 1;
    }
    return crc;
}

static size_t terminalPacket(uint8_t *out, size_t size, uint32_t frame, int freePlay, uint32_t revision)
{
    static const char serial[] = HASP_SERIAL;
    char terminalSerial[sizeof(serial)];
    PbMsg m = {0}, car = {0}, course = {0}, beat = {0}, settings = {0}, info = {0}, feature = {0};

    memcpy(terminalSerial, serial, sizeof(serial));
    terminalSerial[5] = '1';
    pbAddBytes(&car, 1, "", 0);
    pbAddUint(&car, 2, 4);
    for (uint32_t f = 3; f <= 10; f++)
        pbAddUint(&car, f, 0);
    if (freePlay)
        pbAddBytes(&course, 11, "", 0);
    pbAddUint(&beat, 1, frame);
    pbAddBytes(&beat, 2, terminalSerial, strlen(terminalSerial));
    pbAddUint(&beat, 3, 1); // with the game server
    static const uint32_t costs[][2] = {{1, 1}, {2, 3}, {3, 2}, {4, 2}, {5, 4}, {7, 1}, {8, 0}, {9, 0}, {10, 2}};
    for (size_t i = 0; i < sizeof(costs) / sizeof(costs[0]); i++)
        pbAddUint(&settings, costs[i][0], costs[i][1]);
    pbAddUint(&settings, 6, freePlay);
    for (uint32_t f = 11; f <= 18; f++)
        pbAddUint(&settings, f, 96); // no closing time
    pbAddUint(&settings, 19, revision);
    pbAddUint(&settings, 20, (uint64_t)time(NULL));
    pbAddUint(&settings, 21, 4);
    pbAddUint(&settings, 22, 36);
    pbAddUint(&settings, 23, 0);
    pbAddUint(&info, 1, 1);
    pbAddUint(&info, 2, 1);
    pbAddUint(&info, 3, 0);
    pbAddUint(&info, 4, 0);
    pbAddUint(&feature, 1, 100);
    pbAddUint(&feature, 2, 2014);
    pbAddUint(&feature, 3, 5);
    pbAddUint(&feature, 4, 0);
    pbAddUint(&feature, 5, 0);
    pbAddMsg(&m, 2, &car);
    pbAddMsg(&m, 3, &course);
    pbAddMsg(&m, 5, &beat);
    if (freePlay)
        pbAddUint(&m, 6, 3);
    pbAddMsg(&m, 8, &settings);
    pbAddMsg(&m, 9, &info);
    pbAddMsg(&m, 10, &feature);
    size_t n;
    uint8_t *body = pbSerialize(&m, &n);
    size_t total = 4 + n + 4;
    if (!body || total > size)
        total = 0;
    else
    {
        out[0] = LINK_GROUP;
        out[1] = TERMINAL_NODE;
        out[2] = (uint8_t)(4 + n);
        out[3] = (uint8_t)((4 + n) >> 8);
        memcpy(out + 4, body, n);
        uint32_t crc = crc32Mpeg2(out, 4 + n);
        out[4 + n] = crc >> 24;
        out[5 + n] = crc >> 16;
        out[6 + n] = crc >> 8;
        out[7 + n] = crc;
    }
    free(body);
    pbFree(&m);
    pbFree(&car);
    pbFree(&course);
    pbFree(&beat);
    pbFree(&settings);
    pbFree(&info);
    pbFree(&feature);
    return total;
}

static void *terminalThread(void *arg)
{
    (void)arg;
    int freePlay = getenv("WMMT5_FREEPLAY") && atoi(getenv("WMMT5_FREEPLAY"));
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in from = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(TERMINAL_ADDRESS)};
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(LAN_PORT), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    uint8_t packet[512];
    uint32_t frame = 0, logged = 0;

    if (fd < 0 || bind(fd, (struct sockaddr *)&from, sizeof(from)) != 0)
    {
        log_error("WMMT5DX+: the terminal cannot send");
        return NULL;
    }
    for (;;)
    {
        uint32_t revision = *(volatile uint32_t *)SOFTWARE_REVISION;
        if (revision)
        {
            if (revision != logged && wmmt4ServerTrace)
                fprintf(stderr, "WMMT5 terminal: cabinet software revision %u\n", logged = revision);
            size_t n = terminalPacket(packet, sizeof(packet), ++frame, freePlay, revision);
            if (n)
                sendto(fd, packet, n, 0, (struct sockaddr *)&to, sizeof(to));
        }
        usleep(100000);
    }
    return NULL;
}

static void startTerminal(void)
{
    pthread_t thread;
    terminalRunning = pthread_create(&thread, NULL, terminalThread, NULL) == 0;
    if (terminalRunning)
        pthread_detach(thread);
}

// ---------------------------------------------------------------------------
// The unit check's steps asking the terminal cabinet about itself, which a
// lone drive cabinet skips (TERMINAL UNIT S/N CHECKING, then E2407).

#define UNIT_STEP 0x238
#define UNIT_TERMINAL_ANSWERED 0x448

static void (*realUnitCheck)(uint8_t *, int);

static uint8_t *volatile infoState;

static void *infoWatch(void *arg)
{
    (void)arg;
    char last[256] = "";
    for (;;)
    {
        uint8_t *s = infoState;
        char now[256];
        if (s)
        {
            snprintf(now, sizeof(now), "200=%d 21c=%d 21d=%d 222=%d 223=%d 368=%d step=%d link=%d err=%d", s[0x200], s[0x21c],
                     s[0x21d], s[0x222], s[0x223], s[0x368], *(int *)(s + 0x238), *(int *)(s + 0x418), *(int *)(s + 0x1cc));
            if (strcmp(now, last))
            {
                fprintf(stderr, "WMMT5 info: %s\n", now);
                strcpy(last, now);
            }
        }
        usleep(200000);
    }
    return NULL;
}

HOOK static void unitCheck(uint8_t *state, int argument)
{
    if (state && !infoState && getenv("WMMT5_INFO_TRACE"))
    {
        pthread_t t;
        infoState = state;
        pthread_create(&t, NULL, infoWatch, NULL);
    }
    if (state)
    {
        int *step = (int *)(state + UNIT_STEP);
        if (getenv("WMMT5_NET_TRACE"))
        {
            static int last = -1;
            if (*step != last)
                fprintf(stderr, "WMMT5: unit check step %d\n", last = *step);
        }
        if (terminalRunning)
            ; // the terminal answers: its steps as on a store's LAN
        else if (*step == 9) // ask the terminal
        {
            *step = 22;
            *(int *)(state + UNIT_TERMINAL_ANSWERED) = 1;
        }
        else if (*step == 20) // wait for it
            *step = 22;
    }
    realUnitCheck(state, argument);
}

HOOK static int returnOne(void)
{
    return 1;
}

// ---------------------------------------------------------------------------

void namcoEs1Wmmt5Install(void)
{
    static const uint8_t session[] = {0x83, 0xec, 0x14, 0x56, 0x53, 0x8b, 0x74, 0x24, 0x20};
    static const uint8_t cipher[] = {0x83, 0xec, 0x14, 0x56, 0x53, 0x8b, 0x74, 0x24, 0x28};
    static const uint8_t storage[] = {0x83, 0xec, 0x18, 0x53, 0x8b, 0x5c, 0x24, 0x20};
    static const uint8_t smallLog[] = {0x55, 0x89, 0xe5, 0x81, 0xec, 0x28, 0x08, 0x00, 0x00};
    static const uint8_t largeLog[] = {0x55, 0x89, 0xe5, 0x81, 0xec, 0x38, 0x20, 0x00, 0x00};
    static const uint8_t router[] = {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0x5d, 0x8b, 0x00};
    static const uint8_t netState[] = {0x55, 0x89, 0xe5, 0x56, 0x53, 0x83, 0xec, 0x10};
    static const uint8_t linkCheck[] = {0x55, 0x89, 0xe5, 0x53, 0x83, 0xec, 0x04, 0x8b, 0x45, 0x08};
    static const uint8_t peerCheck[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x18, 0x89, 0x5d, 0xf8};
    static const uint8_t billing[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0x81, 0xec};
    static const uint8_t decrypt[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0x81, 0xec, 0x3c, 0x14};
    static const uint8_t monitor[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0x81, 0xec, 0xfc, 0x01, 0x00, 0x00};
    static const uint8_t iface[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x53, 0x81, 0xec, 0x4c, 0x24, 0x00, 0x00};
    static const uint8_t offline[] = {0x55, 0x31, 0xc0, 0x89, 0xe5, 0x53, 0x83, 0xec, 0x14, 0x8b, 0x5d, 0x08};
    static const uint8_t frame28[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x28, 0x0f, 0xb6};
    static const uint8_t frame38[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x38, 0x0f, 0xb6};
    static const uint8_t frame18[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x18, 0x8b, 0x45};
    static const uint8_t sendUrl[] = {0x55, 0x89, 0xe5, 0x57, 0x56, 0x83, 0xec, 0x40};
    static const uint8_t transfer[] = {0x55, 0x89, 0xe5, 0x83, 0xec, 0x08, 0x8b, 0x45, 0x08};
    static const uint8_t unit[] = {0x55, 0x89, 0xe5, 0x81, 0xec};
    static const uint8_t serialKnown[] = {0x55, 0x89, 0xe5, 0x8b, 0x45, 0x08, 0x5d, 0x8b, 0x00, 0x0f, 0xb6, 0x80};

    gameLog = getenv("NAMCO_GAME_LOG") != NULL;
    namcoEs1Wangan("Wangan Midnight Maximum Tune 5DX+");
    haspImage();
    namcoEs1Detour(0x0a982740, session, sizeof(session), haspLogin);
    namcoEs1Detour(0x0a9827e0, session, sizeof(session), haspNothing); // hasp_logout
    namcoEs1Detour(0x0a9828cc, cipher, sizeof(cipher), haspNothing);   // hasp_encrypt
    namcoEs1Detour(0x0a9829b8, cipher, sizeof(cipher), haspNothing);   // hasp_decrypt
    namcoEs1Detour(0x0a9836d0, storage, sizeof(storage), haspGetSize);
    namcoEs1Detour(0x0a983538, storage, sizeof(storage), haspRead);
    namcoEs1Detour(0x0a983604, storage, sizeof(storage), haspWrite);
    namcoEs1Detour(0x080bc980, smallLog, sizeof(smallLog), logMes);
    namcoEs1Detour(0x080bca60, smallLog, sizeof(smallLog), logMes);
    namcoEs1Detour(0x080bcb40, largeLog, sizeof(largeLog), logMes);

    MH_Initialize(); // already done is fine
    namcoEs1Detour(0x082519d0, router, sizeof(router), contentRouter);
    namcoEs1HookOriginal(0x084ec560, netState, sizeof(netState), (void *)networkState, &realNetworkState);
    namcoEs1Detour(0x0aa75120, linkCheck, sizeof(linkCheck), returnZero);
    namcoEs1Detour(0x0a393be0, peerCheck, sizeof(peerCheck), returnZero);
    namcoEs1Detour(0x08401a70, billing, sizeof(billing), doNothing); // no billing server
    namcoEs1HookOriginal(0x0aa77780, decrypt, sizeof(decrypt), (void *)decryptToken, &realDecryptToken);
    namcoEs1HookOriginal(0x08288040, monitor, sizeof(monitor), (void *)monitorStep, &realMonitorStep);
    hookExecutableImport("getifaddrs", (void *)getInterfaces);
    namcoEs1Detour(0x080e6020, (const uint8_t[]){0x55, 0x31, 0xc0, 0x89, 0xe5, 0x83, 0xec, 0x78}, 8, str3Init);
    namcoEs1HookOriginal(0x082528d0, iface, sizeof(iface), (void *)interfaceUpdate, &realInterfaceUpdate);
    // Five days without Mucha authentication stop the cabinet (E0554).
    namcoEs1Detour(0x084eb000, offline, sizeof(offline), returnZero);
    // The content router check's two jumps: a local router accepted.
    namcoEs1Patch(0x0827f7e3, (const uint8_t[]){0x0f, 0x84}, (const uint8_t[]){0x0f, 0x85}, 2);
    namcoEs1Patch(0x0827f9fc, (const uint8_t[]){0x0f, 0x85}, (const uint8_t[]){0x0f, 0x84}, 2);

    namcoEs1Detour(0x0aa62c34, frame28, sizeof(frame28), doNothing); // BngRwInit
    namcoEs1Detour(0x0aa62764, frame38, sizeof(frame38), banaAttach);
    namcoEs1Detour(0x080ead50, frame18, sizeof(frame18), returnZero); // BngRwIsCmdExec
    namcoEs1Detour(0x0aa6200c, frame28, sizeof(frame28), banaRequest); // LED
    namcoEs1Detour(0x0aa61de6, frame28, sizeof(frame28), banaRequest); // action
    namcoEs1Detour(0x0aa61eec, frame28, sizeof(frame28), banaRequest); // beep
    namcoEs1Detour(0x0aa61a1a, frame28, sizeof(frame28), banaCancel);
    namcoEs1Detour(0x0aa6236e, sendUrl, sizeof(sendUrl), banaSendUrl);
    namcoEs1Detour(0x0aa62116, frame28, sizeof(frame28), banaWaitTouch);
    namcoEs1Detour(0x0aa62248, frame28, sizeof(frame28), banaReset);

    namcoEs1Detour(0x080eefc0, transfer, sizeof(transfer), vendorSend);
    namcoEs1Detour(0x080eef50, transfer, sizeof(transfer), vendorReceive);

    ffbTrace = getenv("WMMT5_FFB_TRACE") != NULL;
    namcoEs1HookOriginal(0x080de440, (const uint8_t[]){0x55, 0xd9, 0xe8, 0x89, 0xe5, 0xd9, 0xe0}, 7, (void *)setForces,
                         &realSetForces);
    evdevFfbOpen("WMMT5DX+");
    namcoEs1HookOriginal(0x084fa120, unit, sizeof(unit), (void *)unitCheck, &realUnitCheck);
    if (getConfig()->wmmt4Server)
        startTerminal();
    // The terminal's serial, which it would answer the cabinet's question with
    // (E2407 otherwise): known.
    namcoEs1Detour(0x083c1800, serialKnown, sizeof(serialKnown), returnOne);
    // Online: ALL.Net, Mucha and the game server answered by the loader
    // (WMMT4's network layer, 5DX+'s RPCs in namcoEs1Wmmt5Rpc.c).
    if (getConfig()->wmmt4Server)
        namcoEs1Wmmt4NetInstall();
    log_info("WMMT5DX+: dongle serial %s", HASP_SERIAL);
}
