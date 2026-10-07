#ifndef IMPORT_HOOK_H
#define IMPORT_HOOK_H

// Point the game executable's own import of name (its PLT's GOT slot) at
// replacement; returns what the import resolves to otherwise, NULL if the
// game does not import it. Only the executable's calls are diverted: the
// libraries' own calls to name are not.
void *hookExecutableImport(const char *name, void *replacement);

// What an import of name reaches, as the dynamic linker binds it past the
// executable: the loader's own function of that name (its interposers:
// glEnable, glXSwapBuffers, open...), else its libraries'. Not
// dlsym(RTLD_DEFAULT): an import whose address the game takes is defined in
// the executable, at its PLT entry, which jumps to the hooked slot.
void *importTarget(const char *name);

// Games built by compilers that keep the stack 4-byte aligned (Intel's C++
// for the Gaelco games) call today's libraries, built for SSE, with a stack
// they cannot use (movaps on a misaligned frame). Each import of the
// executable then goes through a stub of ours that aligns the stack to 16
// bytes and calls the import with a copy of its first 24 argument words
// (setjmp, longjmp and the like are left alone). Call it once the other
// import hooks are in place: their replacements are what the stubs call.
void alignExecutableImports(void);

#endif // IMPORT_HOOK_H
