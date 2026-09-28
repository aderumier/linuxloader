#include "rtGame.h"
#include "../config/config.h"
#include "../hardware/lindbergh/jvs.h"
#include "cruisnblast/cbImports.h"
#include "galagaassault/gaImports.h"
#include "walkingdead/twdImports.h"
#include "jurassicpark/jpImports.h"

// SDL 1.2 key codes (engine input ids of the keyboard).
enum
{
    KEY_RETURN = 13,
    KEY_ESCAPE = 27,
    KEY_UP = 273,
    KEY_DOWN = 274,
    KEY_F1 = 282,
    KEY_F2 = 283,
};

// ---------------------------------------------------------------------------
// Jurassic Park. Engine ids from g5engine/src/g5/core_input_enums.h.

static const char *const jpExtraLibs[] = {"libNxCooking.so", NULL};

enum
{
    JP_MOUSE_X = 0x144,
    JP_MOUSE_Y = 0x145,
    JP_MOUSE_RIGHT = 0x147,
    JP_GUN0_X = 0x14b,
    JP_GUN0_Y = 0x14c,
    JP_GUN0_TRIGGER0 = 0x14d,
    JP_GUN0_TRIGGER1 = 0x14e,
    JP_GUN1_X = 0x155,
    JP_GUN1_Y = 0x156,
    JP_GUN1_TRIGGER0 = 0x157,
    JP_GUN1_TRIGGER1 = 0x158,
    JP_GUN3_TRIGGER7 = 0x172,
    JP_START0 = 0x173,
    JP_START1 = 0x174,
    JP_SERVICE = 0x179,
    JP_DIAG = 0x17a,
    JP_COIN0 = 0x17b,
    JP_COIN1 = 0x17c,
    JP_MENU_UP = 0x17f,
    JP_MENU_DOWN = 0x180,
    JP_MENU_SELECT = 0x181,
    JP_MENU_CANCEL = 0x182,
    JP_HOVER_X_P1 = 0x183,
    JP_HOVER_Y_P1 = 0x184,
    JP_HOVER_X_P2 = 0x185,
    JP_HOVER_Y_P2 = 0x186,
    JP_ANALOG_SELECT = 0x187,
    JP_ANALOG_SELECT_P1 = 0x188,
    JP_ANALOG_SELECT_P2 = 0x189,
};

// I/O slots: the cabinet board's switches (routed by the game's cabinet map)
// and free ones (hardware not present here) for the guns.
enum
{
    JP_IO_COIN0 = 0x14c,
    JP_IO_COIN1 = 0x14d,
    JP_IO_DIAG = 0x150,
    JP_IO_SERVICE = 0x151,
    JP_IO_START0 = 0x154,
    JP_IO_START1 = 0x155,
    JP_IO_GUN0_X = 0x15f,
    JP_IO_GUN0_Y = 0x160,
    JP_IO_GUN0_TRIGGER0 = 0x163,
    JP_IO_GUN0_TRIGGER1 = 0x164,
    JP_IO_GUN1_TRIGGER0 = 0x166,
    JP_IO_GUN1_TRIGGER1 = 0x167,
    JP_IO_GUN1_X = 0x169,
    JP_IO_GUN1_Y = 0x16a,
};

// The developer map drives all four guns (and P2's trigger) from the mouse:
// keep it on P1.
static const RtMapFilter jpDevMapFilters[] = {
    {JP_MOUSE_X, JP_MOUSE_Y, JP_GUN1_X, JP_GUN3_TRIGGER7},
    {JP_MOUSE_RIGHT, JP_MOUSE_RIGHT, JP_GUN1_X, JP_GUN3_TRIGGER7},
    {JP_MOUSE_RIGHT, JP_MOUSE_RIGHT, JP_ANALOG_SELECT_P2, JP_ANALOG_SELECT_P2},
    {0, 0, 0, 0},
};

static const RtMap jpExtraMaps[] = {
    {JP_MOUSE_RIGHT, JP_GUN0_TRIGGER1},
    {'1', JP_START0},
    {'2', JP_START1},
    {'5', JP_COIN0},
    {'6', JP_COIN1},
    {KEY_F1, JP_SERVICE},
    {KEY_F2, JP_DIAG},
    {KEY_UP, JP_MENU_UP},
    {KEY_DOWN, JP_MENU_DOWN},
    {KEY_RETURN, JP_MENU_SELECT},
    {KEY_ESCAPE, JP_MENU_CANCEL},
    {0, 0},
};

// Guns come from evdev: the desktop pointer must not drive them too.
static const RtMapFilter jpEvdevMapFilters[] = {{JP_MOUSE_X, JP_MOUSE_RIGHT, 0, 0xffff}, {0, 0, 0, 0}};

static const RtMap jpEvdevMaps[] = {
    {JP_IO_GUN0_X, JP_GUN0_X},
    {JP_IO_GUN0_Y, JP_GUN0_Y},
    {JP_IO_GUN0_X, JP_HOVER_X_P1},
    {JP_IO_GUN0_Y, JP_HOVER_Y_P1},
    {JP_IO_GUN0_TRIGGER0, JP_GUN0_TRIGGER0},
    {JP_IO_GUN0_TRIGGER0, JP_ANALOG_SELECT_P1},
    {JP_IO_GUN0_TRIGGER0, JP_ANALOG_SELECT},
    {JP_IO_GUN0_TRIGGER1, JP_GUN0_TRIGGER1},
    {JP_IO_GUN1_X, JP_GUN1_X},
    {JP_IO_GUN1_Y, JP_GUN1_Y},
    {JP_IO_GUN1_X, JP_HOVER_X_P2},
    {JP_IO_GUN1_Y, JP_HOVER_Y_P2},
    {JP_IO_GUN1_TRIGGER0, JP_GUN1_TRIGGER0},
    {JP_IO_GUN1_TRIGGER0, JP_ANALOG_SELECT_P2},
    {JP_IO_GUN1_TRIGGER0, JP_ANALOG_SELECT},
    {JP_IO_GUN1_TRIGGER1, JP_GUN1_TRIGGER1},
    {0, 0},
};

// ANALOGUE_1/2 and 3/4 are the P1/P2 guns (the cabinet guns report Y from
// the bottom of the screen), PLAYER_n_BUTTON_1/2 their triggers.
static const RtIoInput jpIoInputs[] = {
    {RT_IO_ANALOG, 0, JP_IO_GUN0_X, ANALOGUE_1},
    {RT_IO_ANALOG_INVERTED, 0, JP_IO_GUN0_Y, ANALOGUE_2},
    {RT_IO_ANALOG, 0, JP_IO_GUN1_X, ANALOGUE_3},
    {RT_IO_ANALOG_INVERTED, 0, JP_IO_GUN1_Y, ANALOGUE_4},
    {RT_IO_SWITCH, PLAYER_1, JP_IO_GUN0_TRIGGER0, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, JP_IO_GUN0_TRIGGER1, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_2, JP_IO_GUN1_TRIGGER0, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_2, JP_IO_GUN1_TRIGGER1, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_1, JP_IO_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_2, JP_IO_START1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, JP_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, PLAYER_2, JP_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, JP_IO_DIAG, BUTTON_TEST},
    {RT_IO_COIN, 0, JP_IO_COIN0, 0},
    {RT_IO_COIN, 1, JP_IO_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};
static const char *const cbExtraLibs[] = {"libNxCooking.so", NULL};

// ---------------------------------------------------------------------------
// Cruis'n Blast. Its core_input_enums.h adds VIEW and TUNES after START3, so
// the cabinet ids differ from Jurassic Park's; the wheel and gas pedal are
// the GUN0 X/Y inputs.

enum
{
    CB_START0 = 0x173,
    CB_VIEW = 0x177,
    CB_TUNES = 0x178,
    CB_SERVICE = 0x17b,
    CB_DIAG = 0x17c,
    CB_COIN0 = 0x17d,
    CB_COIN1 = 0x17e,
    CB_MENU_UP = 0x19d,
    CB_MENU_DOWN = 0x19e,
    CB_MENU_SELECT = 0x19f,
    CB_MENU_CANCEL = 0x1a0,
};

// The cabinet board's inputs, as routed by the game's cabinet map (the brake
// pedal is a switch, read through START1).
enum
{
    CB_IO_COIN0 = 0x14c,
    CB_IO_COIN1 = 0x14d,
    CB_IO_DIAG = 0x150,
    CB_IO_SERVICE = 0x151,
    CB_IO_START0 = 0x154,
    CB_IO_BRAKE = 0x155,
    CB_IO_VIEW = 0x156,
    CB_IO_TUNES = 0x159,
    CB_IO_WHEEL = 0x173,
    CB_IO_GAS = 0x174,
};

static const RtMap cbExtraMaps[] = {
    {'1', CB_START0},
    {'5', CB_COIN0},
    {'6', CB_COIN1},
    {'v', CB_VIEW},
    {'m', CB_TUNES},
    {KEY_F1, CB_SERVICE},
    {KEY_F2, CB_DIAG},
    {KEY_UP, CB_MENU_UP},
    {KEY_DOWN, CB_MENU_DOWN},
    {KEY_RETURN, CB_MENU_SELECT},
    {KEY_ESCAPE, CB_MENU_CANCEL},
    {0, 0},
};

// ANALOGUE_1 steers, ANALOGUE_2 is the gas pedal and ANALOGUE_3 the brake
// pedal (or PLAYER_1_BUTTON_1); BUTTON_2 changes the view, BUTTON_3 the music.
static const RtIoInput cbIoInputs[] = {
    {RT_IO_ANALOG, 0, CB_IO_WHEEL, ANALOGUE_1},
    {RT_IO_ANALOG, 0, CB_IO_GAS, ANALOGUE_2},
    {RT_IO_ANALOG_SWITCH, 0, CB_IO_BRAKE, ANALOGUE_3},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_BRAKE, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_VIEW, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_TUNES, BUTTON_3},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, CB_IO_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, CB_IO_DIAG, BUTTON_TEST},
    {RT_IO_COIN, 0, CB_IO_COIN0, 0},
    {RT_IO_COIN, 1, CB_IO_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};

// On the cabinet the game's user data directory links to /pm/pmuser.
static const RtPathAlias cbPathAliases[] = {{"g5/race/pmuser", "pmuser"}, {NULL, NULL}};

// Force feedback wheel driver board on the parallel port.
static const RtStub cbStubs[] = {
    {"Wheel_Init", 0},       {"Wheel_EnablePWM", 0},   {"Wheel_DisablePWM", 0}, {"Wheel_Shutdown", 0},
    {"Wheel_Set", 0},        {"Wheel_EnableWDT", 0},   {"Wheel_DisableWDT", 0}, {"Wheel_TwiddleWDT", 0},
    {"Wheel_SetData", 0},    {"Wheel_SetControl", 0},  {NULL, 0},
};

// ---------------------------------------------------------------------------
// Galaga Assault (statically linked SDL 2.0.3, not the g5 engine).

// RIO switch numbers (from the switch test).
enum
{
    GA_SW_COIN1 = 0x00,
    GA_SW_COIN2 = 0x01,
    GA_SW_TEST = 0x03,
    GA_SW_SERVICE = 0x04,
    GA_SW_VOL_UP = 0x05,
    GA_SW_VOL_DOWN = 0x06,
    GA_SW_START_FIRE = 0x07,
    GA_SW_UP = 0x0d,
    GA_SW_DOWN = 0x0e,
    GA_SW_LEFT = 0x0f,
    GA_SW_RIGHT = 0x10,
};

// One button starts and fires: START or BUTTON_1.
static const RtIoInput gaRioSwitches[] = {
    {RT_IO_SWITCH, PLAYER_1, GA_SW_START_FIRE, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_START_FIRE, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_DOWN, BUTTON_DOWN},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_LEFT, BUTTON_LEFT},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_RIGHT, BUTTON_RIGHT},
    {RT_IO_SWITCH, PLAYER_1, GA_SW_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, GA_SW_TEST, BUTTON_TEST},
    {RT_IO_COIN, 0, GA_SW_COIN1, 0},
    {RT_IO_COIN, 1, GA_SW_COIN2, 0},
    {RT_IO_END, 0, 0, 0},
};

// The RIO board is reported connected.
static const RtStub gaStubs[] = {{"RIO_Connected", 0}, {NULL, 0}};

// ---------------------------------------------------------------------------
// The Walking Dead (g6 engine; the dump exports no game function). Found from
// the HASP library's trace strings and by matching the dongle layer against
// the g5 games.

static const RtSymbol twdSymbols[] = {
    {"hasp_login", 0x0837bbc0},
    {"hasp_logout", 0x0837bc60},
    {"hasp_read", 0x0837c9b8},
    {"hasp_write", 0x0837ca84},
    {"hasp_get_sessioninfo", 0x0837c740},
    {"DongleEncrypt", 0x081d7200},
    {"DongleDecrypt", 0x081d7380},
    {NULL, 0},
};

// ---------------------------------------------------------------------------
// Terminator Salvation (g3 engine), v01.25.00. Not a dump: a stripped but
// normally linked binary, with the HASP HL library linked in statically.
// Functions found from the game's call sites and the HASP API's argument
// checks. The data files are encrypted with keys the dongle encrypts: its
// answers are read from TeknoParrot's recording ("hasp" folder).

static const RtSymbol t4Symbols[] = {
    {"hasp_login", 0x083bbe60},
    {"hasp_logout", 0x083bbe00},
    {"hasp_read", 0x083bac80},
    {"hasp_write", 0x083bad30},
    {"hasp_get_sessioninfo", 0x083bae80},
    {"hasp_free", 0x083baab0},
    {"hasp_encrypt", 0x083bbd60},
    {"hasp_decrypt", 0x083bbce0},
    // Thread keeping the dongle busy with random encryptions.
    {"DongleNoise", 0x080ac230},
    // Forks a tracer: the game runs as a child the parent ptraces, so that
    // no debugger can attach. Returns 1 in that child.
    {"TracerGuard", 0x082f2eda},
    // Boot-time check of the data files against their stored checksums.
    // Game copies ship a re-encrypted, fixed eshaders/include/frag_shadmap.gls
    // (the original does not compile on current drivers), which fails it
    // and stops the game on "Game file errors detected".
    {"DiagCheckAllFiles", 0x08190960},
    // Online licensing: sets a record's lockout reason (offline too long,
    // clock error, account delinquent, on too long without connection, not
    // registered) from the operator and unit registration the network
    // provides (op.aud, GameUnit.aud); a lockout stops the game on "Please
    // stand by". Returns 1 when the record has none.
    {"LicenseLockout", 0x081adad0},
    // Whether a player's light gun is connected (the IR gun manager found
    // it on a USB serial port): players join only then.
    {"GunConnected", 0x08159f00},
    // The IR gun manager's aim of a player's gun (reticle, ...), and whether
    // it lost the gun's signal (the aim is then off the screen).
    {"GunAim", 0x0842e488},
    {"GunNoSignal", 0x0842e476},
    // Input: the JAMMA board's poll, run each frame, the board API
    // (request, ...) and the input event queue (event, data).
    {"JammaPoll", 0x08063630},
    {"JammaOp", 0x081d70a3},
    {"PostInputEvent", 0x08062320},
    // Video: select the mode table entry, open the glut window.
    {"SetVideoMode", 0x080c6fc0},
    {"OpenWindow", 0x080c48b0},
    {NULL, 0},
};

// JAMMA board switches, numbered as in the games' switch table (which maps
// them to input events; the same in Big Buck World), and gun events.
enum
{
    T4_GUN0_TRIGGER = 1,
    T4_GUN1_TRIGGER = 2,
    T4_GUN0_RELOAD = 3,
    T4_GUN1_RELOAD = 4,
    T4_START0 = 5,
    T4_START1 = 6,
    T4_COIN0 = 7,
    T4_COIN1 = 8,
    T4_SERVICE = 10,
    T4_TEST = 11,
    T4_VOLUME_UP = 12, // also moves in the test menus
    T4_VOLUME_DOWN = 13,
    // Light gun board messages: a shot.
    T4_GUN0_SHOT = 0x10,
    T4_GUN1_SHOT = 0x28,
};

static const RtIoInput jammaSwitches[] = {
    {RT_IO_SWITCH, PLAYER_1, T4_GUN0_TRIGGER, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_1, T4_GUN0_TRIGGER, BUTTON_3}, // a shot off the screen
    {RT_IO_SWITCH, PLAYER_1, T4_GUN0_RELOAD, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_2, T4_GUN1_TRIGGER, BUTTON_1},
    {RT_IO_SWITCH, PLAYER_2, T4_GUN1_TRIGGER, BUTTON_3},
    {RT_IO_SWITCH, PLAYER_2, T4_GUN1_RELOAD, BUTTON_2},
    {RT_IO_SWITCH, PLAYER_1, T4_START0, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_2, T4_START1, BUTTON_START},
    {RT_IO_SWITCH, PLAYER_1, T4_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, PLAYER_2, T4_SERVICE, BUTTON_SERVICE},
    {RT_IO_SWITCH, SYSTEM, T4_TEST, BUTTON_TEST},
    {RT_IO_SWITCH, PLAYER_1, T4_VOLUME_UP, BUTTON_UP},
    {RT_IO_SWITCH, PLAYER_1, T4_VOLUME_DOWN, BUTTON_DOWN},
    {RT_IO_COIN, 0, T4_COIN0, 0},
    {RT_IO_COIN, 1, T4_COIN1, 0},
    {RT_IO_END, 0, 0, 0},
};

static const RtJammaGun t4Guns[] = {
    {PLAYER_1, ANALOGUE_1, ANALOGUE_2, T4_GUN0_SHOT},
    {PLAYER_2, ANALOGUE_3, ANALOGUE_4, T4_GUN1_SHOT},
};

static const RtStub t4Stubs[] = {{"TracerGuard", 1}, {"DiagCheckAllFiles", 0}, {"LicenseLockout", 1}, {"GunConnected", 1}, {"GunNoSignal", 0}, {NULL, 0}};

static const RtPathAlias t4RootAliases[] = {{"/T4User", "T4User"}, {NULL, NULL}};

// ---------------------------------------------------------------------------
// Big Buck World (g3 engine), v1.20: built like Terminator Salvation, with
// its HASP library's trace strings naming the API functions. Its recorded
// dongle answers are named by the first 32 input bytes.

static const RtSymbol bbwSymbols[] = {
    {"hasp_login", 0x0839d3d0},
    {"hasp_logout", 0x0839d470},
    {"hasp_encrypt", 0x0839d55c},
    {"hasp_decrypt", 0x0839d648},
    {"hasp_free", 0x0839dc8c},
    {"hasp_get_sessioninfo", 0x0839df50},
    {"hasp_read", 0x0839e1c8},
    {"hasp_write", 0x0839e294},
    {"TracerGuard", 0x082dea6a},
    // Checked from the main loop: the tracer parent is alive and tracing.
    {"TracerCheck", 0x082ded09},
    {"DiagCheckAllFiles", 0x080ff8c0},
    // Opens the glut window: (width, height, fullscreen).
    {"OpenWindow", 0x0807fa00},
    // Input: the JAMMA board's poll, run each frame, and the board API.
    {"JammaPoll", 0x080587d0},
    {"JammaOp", 0x08209462},
    {NULL, 0},
};

static const RtStub bbwStubs[] = {
    {"TracerGuard", 1},
    {"TracerCheck", 0},
    {"DiagCheckAllFiles", 0},
    {NULL, 0},
};

static const RtPathAlias bbwRootAliases[] = {{"/bbwuser", "bbwuser"}, {NULL, NULL}};

static const RtGame rtGames[] = {
    {
        .crc32 = JURASSIC_PARK_RT,
        .fileCrc32 = 0xf4a8eca8,
        .envelopeGot = JP_ENVELOPE_GOT,
        .envelopeImports = jpEnvelopeImports,
        .envelopeImportCount = sizeof(jpEnvelopeImports) / sizeof(jpEnvelopeImports[0]),
        .envelopeSelfSlot = 38,
        .envelopeSelfTarget = 0x09048d30,
        .gameImports = jpGameImports,
        .gameImportCount = sizeof(jpGameImports) / sizeof(jpGameImports[0]),
        .extraLibs = jpExtraLibs,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .rootPath = "/pm",
        .encryptedScripts = "g5/dino/data/programs_enc",
        .decryptedScripts = "programs_dec",
        .inputMap = 0x08db6e80,
        .inputMapCount = 0x08b505f8,
        .devInputFlag = 0x08db4e38,
        // "sub $0x2c,%esp; mov 0x8db4e38,%edx"
        .gameInputMapsPrologue = 9,
        // io_sdl_loop: "push %ebp; mov %esp,%ebp; push %esi; push %ebx; sub $0x2aa40,%esp"
        .ioLoopPrologue = 11,
        .devMapFilters = jpDevMapFilters,
        .extraMaps = jpExtraMaps,
        .evdevMapFilters = jpEvdevMapFilters,
        .evdevMaps = jpEvdevMaps,
        .ioInputs = jpIoInputs,
        .ioRawRange = 1,
        .resolution = 0x08db4e20,
        .aspect = 0x08db4e28,
        // "push %ebp; xor %ebp,%ebp; push %edi; push %esi; push %ebx; mov $0x1,%ebx"
        .parseArgsPrologue = 11,
    },
    {
        .crc32 = CRUISN_BLAST_RT,
        .fileCrc32 = 0xb919dbd6,
        .envelopeGot = CB_ENVELOPE_GOT,
        .envelopeImports = cbEnvelopeImports,
        .envelopeImportCount = sizeof(cbEnvelopeImports) / sizeof(cbEnvelopeImports[0]),
        .envelopeSelfSlot = -1,
        .gameImports = cbGameImports,
        .gameImportCount = sizeof(cbGameImports) / sizeof(cbGameImports[0]),
        .extraLibs = cbExtraLibs,
        .haspFeature = 0,
        .haspMemoryFileId = 0xfff4,
        .haspReadPatch = 0x0804829a,
        .haspWritePatch = 0x0804833c,
        .haspSessionInfoPatch = 0x0804842a,
        .stubs = cbStubs,
        .rootPath = "/pm",
        .encryptedScripts = "g5/race/data/programs_enc",
        .decryptedScripts = "programs_dec",
        .pathAliases = cbPathAliases,
        .gameInputMapsSymbol = "_Z13GameInputMapsv",
        .inputMap = 0x09d45ce0,
        .inputMapCount = 0x09c28544,
        // "push %ebx; sub $0x28,%esp; mov 0x8bdd9c8,%edx"
        .gameInputMapsPrologue = 10,
        // The cabinet board backend runs after the SDL one and would clear
        // the switch edges: "push %ebp; push %edi; push %esi; push %ebx; sub $0x1c,%esp"
        .ioLoopSymbol = "io_rio_loop",
        .ioLoopPrologue = 7,
        .extraMaps = cbExtraMaps,
        .ioInputs = cbIoInputs,
        .parseArgsSymbol = "_Z20ParseCommandLineArgsiPPc",
        .resolution = 0x08bdd960,
        .aspect = 0x08bdd974,
        // "push %ebp; push %edi; push %esi; push %ebx; mov $0x1,%ebx"
        .parseArgsPrologue = 9,
    },
    {
        .crc32 = GALAGA_ASSAULT_RT,
        .fileCrc32 = 0xb2452857,
        .envelopeGot = GA_ENVELOPE_GOT,
        .envelopeImports = gaEnvelopeImports,
        .envelopeImportCount = sizeof(gaEnvelopeImports) / sizeof(gaEnvelopeImports[0]),
        .envelopeSelfSlot = -1,
        .gameImports = gaGameImports,
        .gameImportCount = sizeof(gaGameImports) / sizeof(gaGameImports[0]),
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff0,
        .bssStart = 0x083484bc,
        .bssEnd = 0x08633cfc,
        .replacedLib = "libSDL2-2.0.so.0",
        .replacedLibPrefix = "SDL_",
        .workDir = "data",
        // Game_PreInit sets the defaults (1920x1080, rotated), then the
        // command line: "push %ebp; mov %esp,%ebp; sub $0x28,%esp"
        .parseArgsSymbol = "_Z19Main_ProcessCmdLineiPPc",
        .parseArgsPrologue = 6,
        .resolution = 0x083313e0,
        .rotateFlag = 0x085a4f74,
        .monitorSize = 0x083313e8,
        .rioSwitches = gaRioSwitches,
        .stubs = gaStubs,
        // "push %ebp; mov %esp,%ebp; sub $0x58,%esp"
        .orthoSymbol = "set_ortho",
        .orthoPrologue = 6,
        .rootPath = "/pm",
    },
    {
        .crc32 = WALKING_DEAD_RT,
        .fileCrc32 = 0x3fcf1642,
        .envelopeGot = TWD_ENVELOPE_GOT,
        .envelopeImports = twdEnvelopeImports,
        .envelopeImportCount = sizeof(twdEnvelopeImports) / sizeof(twdEnvelopeImports[0]),
        .envelopeSelfSlot = -1,
        .gameImports = twdGameImports,
        .gameImportCount = sizeof(twdGameImports) / sizeof(twdGameImports[0]),
        .symbols = twdSymbols,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .rootPath = "/pm",
    },
    {
        .crc32 = TERMINATOR_SALVATION_RT,
        .envelopeSelfSlot = -1,
        .symbols = t4Symbols,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .haspAnswers = "hasp",
        .stubs = t4Stubs,
        .rootPath = "/g3",
        .rootAliases = t4RootAliases,
        // Both "push %ebp; mov %esp,%ebp; sub $imm8,%esp"
        .setModeSymbol = "SetVideoMode",
        .setModePrologue = 6,
        .modePointer = 0x088b5e70,
        .windowOpenSymbol = "OpenWindow",
        .windowOpenPrologue = 6,
        .jammaPollSymbol = "JammaPoll",
        // "push %ebp; mov %esp,%ebp; push %edi; push %esi; push %ebx; xor %ebx,%ebx"
        .jammaPollPrologue = 8,
        .jammaOpSymbol = "JammaOp",
        // "push %ebp; mov %esp,%ebp; push %ebx; sub $0x74,%esp"
        .jammaOpPrologue = 7,
        .postEventSymbol = "PostInputEvent",
        .jammaSwitches = jammaSwitches,
        .jammaGuns = t4Guns,
        .jammaGunCount = sizeof(t4Guns) / sizeof(t4Guns[0]),
        .gunAimSymbol = "GunAim",
        .gunAimWidth = 800,
        .gunAimHeight = 600,
    },
    {
        .crc32 = BIG_BUCK_WORLD_RT,
        .envelopeSelfSlot = -1,
        .symbols = bbwSymbols,
        .haspFeature = 0xffff0000,
        .haspMemoryFileId = 0xfff2,
        .haspAnswers = "hasp",
        .haspAnswerKeySize = 32,
        .stubs = bbwStubs,
        .rootPath = "/g3",
        .rootAliases = bbwRootAliases,
        // "push %ebp; mov %esp,%ebp; push %edi; push %esi; push %ebx"
        .windowOpenSymbol = "OpenWindow",
        .windowOpenPrologue = 6,
        // Switches only for now: the gun events are not known yet.
        .jammaPollSymbol = "JammaPoll",
        // "push %ebp; mov %esp,%ebp; push %edi; push %esi; push %ebx; mov $0x8baba40,%edi"
        .jammaPollPrologue = 11,
        .jammaOpSymbol = "JammaOp",
        // "push %ebp; mov %esp,%ebp; push %esi; push %ebx; sub $0x70,%esp"
        .jammaOpPrologue = 8,
        .jammaSwitches = jammaSwitches,
    },
};

const RtGame *rtGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(rtGames) / sizeof(rtGames[0]); i++)
        if (rtGames[i].crc32 == crc32)
            return &rtGames[i];
    return NULL;
}

const RtGame *rtGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(rtGames) / sizeof(rtGames[0]); i++)
        if (rtGames[i].fileCrc32 && rtGames[i].fileCrc32 == fileCrc32)
            return &rtGames[i];
    return NULL;
}
