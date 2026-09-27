#ifndef RT_GAME_H
#define RT_GAME_H

#include <stddef.h>
#include <stdint.h>

// One slot of an envelope runtime GOT: slot index -> symbol name.
typedef struct
{
    int slot;
    const char *name;
} RtEnvelopeImport;

// One entry of the game's own GOT: absolute slot address -> symbol@version.
typedef struct
{
    uint32_t slot;
    const char *name;
    const char *version;
} RtGameImport;

// An entry of the engine's input map: source id -> game input id.
typedef struct
{
    uint16_t src;
    uint16_t dst;
} RtMap;

// Input map entries to drop: source and destination id ranges.
typedef struct
{
    uint16_t srcMin, srcMax;
    uint16_t dstMin, dstMax;
} RtMapFilter;

// Evdev input (the loader's JVS state) fed to an engine I/O slot each frame.
typedef enum
{
    RT_IO_END,
    RT_IO_ANALOG,          // source: JVS analogue channel
    RT_IO_ANALOG_INVERTED, // same, reversed
    RT_IO_ANALOG_SWITCH,   // source: JVS analogue channel, pressed past half travel
    RT_IO_SWITCH,          // source: JVS switch bits of player (SYSTEM for TEST)
    RT_IO_COIN,            // player: coin slot (0, 1)
} RtIoInputType;

typedef struct
{
    RtIoInputType type;
    int player;
    uint16_t io;
    uint32_t source;
} RtIoInput;

// A directory the cabinet links elsewhere (symlinks that copies of the game
// may have lost): paths relative to rootPath.
typedef struct
{
    const char *from;
    const char *to;
} RtPathAlias;

// A game function replaced by one returning a constant (0 or 1).
typedef struct
{
    const char *name;
    int value;
} RtStub;

// Everything the loader needs to know about one Raw Thrills game dump.
typedef struct
{
    uint32_t crc32;    // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the dumped ELF file, as the launcher sees it

    // Import tables to rebuild (see rtDump.c).
    uint32_t envelopeGot;
    const RtEnvelopeImport *envelopeImports;
    size_t envelopeImportCount;
    int envelopeSelfSlot; // slot pointing back into the envelope, or -1
    uint32_t envelopeSelfTarget;
    const RtGameImport *gameImports;
    size_t gameImportCount;
    const char *const *extraLibs; // libraries the game uses but does not list in DT_NEEDED

    // HASP feature the game logs in to (HASP_DEFAULT_FID 0, or the legacy
    // program-number feature 0xffff0000); every other login fails.
    uint32_t haspFeature;
    uint32_t haspMemoryFileId; // HASP file holding the dongle memory the game reads
    // Some dumps call a file-backed replacement of hasp_read, hasp_write and
    // hasp_get_sessioninfo (dongle memory under /pp) instead of the HASP
    // API: their addresses, emulated like the API functions (0: none).
    uint32_t haspReadPatch;
    uint32_t haspWritePatch;
    uint32_t haspSessionInfoPatch;

    // A library statically linked into the game whose dumped state is
    // unusable: the game's exported functions with this prefix are sent to
    // the system's copy instead (NULL: none).
    const char *replacedLib;
    const char *replacedLibPrefix;
    // The game's .bss, captured with its runtime state (the envelope's own
    // data follows it): cleared, except what ld.so copies in (R_386_COPY).
    uint32_t bssStart, bssEnd;

    // Cabinet hardware without an emulation: functions stubbed out.
    const RtStub *stubs;

    // Cabinet file layout: the game expects to live in this directory.
    const char *rootPath;
    // Directory holding encrypted scripts, relative to rootPath, and its
    // decrypted copy relative to the game directory (NULL if none).
    const char *encryptedScripts;
    const char *decryptedScripts;
    const RtPathAlias *pathAliases;
    const char *workDir; // working directory the game expects, relative to the game directory (NULL: itself)

    // Engine input (g5 engine). No GameInputMaps prologue: input hooks off.
    // After GameInputMaps, devMapFilters entries are dropped and extraMaps
    // added; with evdev input, evdevMapFilters entries are dropped too,
    // evdevMaps added, and ioInputs fed to the engine's I/O slots each frame.
    const char *gameInputMapsSymbol; // NULL: "GameInputMaps" (C games; C++ ones are mangled)
    uint32_t inputMap;           // InputMap[][2] (src, dst)
    uint32_t inputMapCount;      // number of entries
    uint32_t devInputFlag;       // gCLArgs field enabling the developer mouse/keyboard map, 0: off
    size_t gameInputMapsPrologue; // bytes of GameInputMaps to relocate into a trampoline
    // I/O backend loop the evdev input is fed after: it must be the last one
    // to update the slots used (NULL: "io_sdl_loop").
    const char *ioLoopSymbol;
    size_t ioLoopPrologue; // bytes of it to relocate into a trampoline
    const RtMapFilter *devMapFilters;
    const RtMap *extraMaps;
    const RtMapFilter *evdevMapFilters;
    const RtMap *evdevMaps;
    const RtIoInput *ioInputs;
    int ioRawRange; // set the analog slots' raw range to the evdev one (else the game's calibration)
    // Games reading the RIO board through its API (RIO_SW_State/RIO_SW_Count)
    // instead of the g5 engine: evdev input by switch number (RtIoInput.io).
    const RtIoInput *rioSwitches;

    // Video mode (g5 engine): gCLArgs width, height (int) and aspect (float),
    // set from the command line by ParseCommandLineArgs.
    const char *parseArgsSymbol; // NULL: "ParseCommandLineArgs"
    uint32_t resolution;          // width, then height
    uint32_t aspect;              // 0: none
    // Vertical games: flag drawing the picture rotated for a monitor turned
    // on its side, set from [Display] ROTATE_VERTICAL (0: none).
    uint32_t rotateFlag;
    // Games drawing in their monitor's pixels (landscape size, width then
    // height, 0: none) through a set_ortho(w, h) setting viewport and
    // projection for the window: the picture is scaled to fit the window.
    uint32_t monitorSize;
    const char *orthoSymbol;
    size_t orthoPrologue;
    size_t parseArgsPrologue; // bytes of ParseCommandLineArgs to relocate into a trampoline
} RtGame;

const RtGame *rtGetGame(uint32_t crc32);
const RtGame *rtGetGameByFileCrc(uint32_t fileCrc32);

// Launcher: run a patched copy of a Raw Thrills dump (see rtLaunch.c).
void rtPrepareCommand(char *command, size_t size, uint32_t fileCrc32);

#endif // RT_GAME_H
