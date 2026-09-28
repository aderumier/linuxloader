#ifndef RAWTHRILLS_H
#define RAWTHRILLS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "rtGame.h"

// Raw Thrills games (g5 engine, e.g. Jurassic Park Arcade) are dumps of HASP
// Envelope-protected Linux binaries. They do not use any Lindbergh hardware:
// the dongle, the RIO I/O board and the sound device are emulated here.

// Game descriptor of the running game, or NULL if it is not a Raw Thrills game.
const RtGame *rtCurrentGame(void);
int isRawThrillsGame(void);
// Directory the game was started from, standing in for the cabinet's root.
const char *rtGameDir(void);

// Called from initMain() instead of the Lindbergh hardware setup.
int rtInit(void);

// Filesystem: map cabinet paths (/pm, /mnt/usbflash, /etc/udev, encrypted
// scripts) to their host location. Returns path or buf.
const char *rtRedirectPath(const char *path, char *buf, size_t size);

// OSS /dev/dsp emulation on top of SDL3 audio.
int rtDspIsPath(const char *path);
int rtDspOpen(void);
int rtDspIsFd(int fd);
ssize_t rtDspWrite(const void *buf, size_t count);
int rtDspIoctl(unsigned long request, void *arg);
void rtDspClose(void);

// Helpers shared by the rawthrills modules.
// Game function by name: exported, or listed in the game's descriptor.
void *rtSymbol(const char *name);
int rtDetour(const char *name, void *replacement);
void rtDetourAddress(uint32_t address, void *replacement);
void *rtTrampoline(const char *name, size_t prologueLength);
int rtFixImports(const RtGame *game);
void rtClearBss(const RtGame *game);
int rtReplaceLib(const RtGame *game);
void rtInstallDongle(const RtGame *game);
void rtInstallIo(const RtGame *game);
void rtInstallInput(const RtGame *game);
void rtInstallJamma(const RtGame *game);
// Evdev input (the loader's JVS state): whether a switch input is held.
struct JVSIO;
int rtIoSwitchState(const RtIoInput *in, struct JVSIO *io);
void rtInstallVideo(const RtGame *game);

#endif // RAWTHRILLS_H
