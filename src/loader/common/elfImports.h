#ifndef ELF_IMPORTS_H
#define ELF_IMPORTS_H

#include <stdint.h>

// The function imports of the running game (the R_386_JMP_SLOT
// relocations of its executable), recorded by the loader as it resolves
// them (see elfLoader/symbolResolver.cpp). The games that stand between
// the game's calls and their targets — a hook on one of them (Roxor's
// SDL_SetVideoMode, roxor/roxor.c), aligned stubs on the rest — work on
// these slots, which already hold the resolved addresses.
//
// At most max names and slots, the number recorded.
int loaderImportSlots(const char **names, uint32_t **slots, int max);

#endif // ELF_IMPORTS_H
