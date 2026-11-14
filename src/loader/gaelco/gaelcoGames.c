// Gaelco game descriptors (see gaelcoGame.h).

#include <stddef.h>

#include "gaelcoGame.h"
#include "../config/config.h"

// Tokyo Cop's dump is its salidas directory, with the save partitions
// (save0, save1) in it.
static const GaelcoPathAlias tokyoCopPaths[] = {
    {"/home/joc2001/joc2001/salidas", "."},
    {"/home/save0", "save0"},
    {"/home/save1", "save1"},
    {NULL, NULL},
};

static const GaelcoGame games[] = {
    {
        // Tokyo Cop (gameport, the same as car in the dump).
        .crc32 = TOKYO_COP_GAELCO,
        .fileCrc32 = 0x7a518e3b,
        .paths = tokyoCopPaths,
        .frameWidth = 640,
        .frameHeight = 480,
        .io = GAELCO_IO_PCI_CONFIG,
        .controls = GAELCO_TOKYO_COP,
        // G3Dintf_mandos::IniControllers(int *, POTE *), its menus' and
        // games' too.
        .iniControllers = 0x080e6b90,
        .iniControllersPrologue = 5,
        .potentiometerCount = 4,
        .potentiometerMargin = 7,
    },
};

const GaelcoGame *gaelcoGetGame(uint32_t crc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].crc32 == crc32)
            return &games[i];
    return NULL;
}

const GaelcoGame *gaelcoGetGameByFileCrc(uint32_t fileCrc32)
{
    for (size_t i = 0; i < sizeof(games) / sizeof(games[0]); i++)
        if (games[i].fileCrc32 == fileCrc32)
            return &games[i];
    return NULL;
}
