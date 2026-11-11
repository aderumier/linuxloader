#ifndef IGS_GAME_H
#define IGS_GAME_H

#include <stdint.h>

// Everything the loader needs to know about one IGS (International Games
// System) PC game. Gold Captain (2009) and Pirate Revenge (2008): 32-bit
// Linux games on IGS's own distribution (IGS-Linux, kernel 2.4), drawing
// through a static SDL 1.2 on X11 and GL. Their cabinet: a PLX PCI9030 card
// whose shared RAM talks to the IGS036, the board's protected CPU (the
// cabinet's switches, coins, lamps, the operator's settings and the game's
// stage flow), a BIOS id check through /dev/roio and a security device the
// game only opens (/dev/pccard0).
typedef struct
{
    uint32_t crc32;     // loader-side id (partial CRC of the code segment)
    uint32_t fileCrc32; // CRC32 of the ELF file, as the launcher sees it

    // The cabinet's game directory (rootPath) is the dump's; its other
    // absolute directories (dataDirs, NULL-terminated) go there too.
    const char *rootPath;
    const char *dataDirs[4];

    // The motherboard id the BIOS check answers (its table's ids), and the
    // check itself: int (table, id).
    int biosBoard;
    uint32_t biosCheck;

    // The static SDL's SDL_SetVideoMode (the game asks for full screen), and
    // the size the game draws at (its cabinet's monitor).
    uint32_t setVideoMode;
    int frameWidth, frameHeight;

    // The cabinet's switches' bits in the IGS036's block (word 5): the left
    // and right buttons, the bomb button (0: none), test and service.
    uint8_t keyLeft, keyRight, keyBomb, keyTest, keyService;
    // The helm's potentiometer range (IGS036 word 6: 0..helmMax - 1); the
    // game reads it as 100 - value / 5.12 percent. Reversed: 0 is the right.
    int helmMax, helmReversed;
    // The helm's fastest turn (its full range is 2: per frame the game
    // shows), as the game takes a faster one for a dodge (then on buttons of
    // their own). 0: as fast as the controls.
    double helmSpeed;
    // The helm's calibration (VR Setting: its two ends and its centre, 3
    // ints) in the game's settings (an address) and in the operator's
    // settings it hands the IGS036 (a word), which the game's defaults
    // leave at 0 (no turn at all): it is the full range then. 0: none.
    uint32_t helmCalibration;
    int helmCalibrationWord;

    // The game's reads of the desktop keyboard (its debug keys, and its
    // controls' keys besides the cabinet's: KeyDown, KeyPress, KeyUp), none
    // pressed: the controls are the loader's. 0: left as they are.
    uint32_t keyboardReads[3];

    // The PLX SDK's device find, open and BAR map (PlxPciDeviceFind,
    // PlxPciDeviceOpen, PlxPciBarMap).
    uint32_t plxDeviceFind, plxDeviceOpen, plxBarMap;

    // The IGS036 exchange (one round of the shared RAM's mailbox, returning
    // the command the IGS036 acknowledged), the game's handling of the
    // IGS036's block, and their variables: the IGS036's block as read
    // (decrypted, 32-bit words), the PC's as sent, the pending command and
    // its argument.
    uint32_t mcuExchange, mcuProcess;
    uint32_t mcuIn, pcOut, pendingCommand, pendingArgument;

    // The stage's segments the IGS036 answers (command 5: 10 of start,
    // length, attribute), and the stage's objects as loaded from its SOD
    // file (their depth, an int, then the next object's).
    uint32_t sodObjectDepth;
    int sodObjects, sodObjectSize;

    // The game's stage (an int, the stage switch's index), the fight stage
    // whose loop leaves on its time up without naming the next stage, and
    // that fight's time left (frames), segment and count of segments: the
    // time up ends the segments, as its debug key does, so the stage ends.
    // fightTime 0: none (each fight ends and frees itself).
    uint32_t stage;
    int segmentFight;
    uint32_t fightTime, segment, segmentCount;

    // The fights' setups (each loads its fight; the segment fight's first),
    // the segment fight's teardown, which frees it only when its flag (an
    // int) is 1 (else the boss's fight goes on with it), and the weapons'
    // release every fight's teardown calls. 0: none (each fight frees what it
    // loaded).
    uint32_t fightSetups[3];
    uint32_t fightRelease, fightReleaseFlag, weaponsRelease;
} IgsGame;

const IgsGame *igsGetGame(uint32_t crc32);
const IgsGame *igsGetGameByFileCrc(uint32_t fileCrc32);

#endif // IGS_GAME_H
