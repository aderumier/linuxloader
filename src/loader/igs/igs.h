#ifndef IGS_H
#define IGS_H

#include <stddef.h>

#include "igsGame.h"

// IGS games (Gold Captain): see igsGame.h. They keep their own window (a
// static SDL 1.2 on X11): none of the Lindbergh path. Their input is the
// loader's (the desktop keyboard, or evdev's JVS state), through the
// IGS036 emulated here.

// Game descriptor of the running game, or NULL if it is not an IGS game.
const IgsGame *igsCurrentGame(void);
int isIgsGame(void);

// Called from initMain(), in place of the Lindbergh set-up.
int igsInit(void);

// The cabinet's paths (rootPath, dataDirs, its devices) mapped to the
// game's directory: path, or buf holding the mapped one.
const char *igsRedirectPath(const char *path, char *buf, size_t size);

// Paletted textures, expanded to RGBA (igsPalette.c).
void igsPaletteInstall(void);

// A display's Xlib locks taken away (igsX11.c).
void igsUnlockDisplay(void *display);
// The X screen's size: 0 when there is no display.
int igsScreenSize(int *width, int *height);

// The frames the game has shown (its buffer swaps).
unsigned int igsFrames(void);

// The PLX card and the IGS036 behind it (igsIo.c): their functions
// replaced.
void igsIoInstall(const IgsGame *game);

#endif // IGS_H
