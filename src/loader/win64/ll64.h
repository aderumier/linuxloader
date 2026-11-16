#ifndef LL64_H
#define LL64_H

// linuxloader64.exe: the 64-bit Linux games (the g7 ones) on Windows. The
// game's ELF and its own Linux libraries (libstdc++, libgcc_s, lapack...) are
// mapped into this process; libc, libpthread, libdl, libGL, libX11 and the
// others are the bridges here. The Linux code calls with the System V ABI, so
// every bridge is SYSV, and every Linux function the bridges call (callbacks,
// thread starts, init arrays) goes through a SYSV pointer.

#include <stddef.h>
#include <stdint.h>

#define SYSV __attribute__((sysv_abi))

// Linux x86-64's long is 64 bits, Windows' 32.
typedef int64_t lx_long;
typedef uint64_t lx_ulong;

typedef struct
{
    const char *name;
    void *fn;
} LxSym;

#define LX_COUNT(a) (sizeof(a) / sizeof((a)[0]))

// The bridges' symbols, looked up by name when a module is relocated.
void lxRegister(const LxSym *syms, size_t count);
void *lxBridge(const char *name);
// A stub for a symbol nothing defines: logs its first calls, returns 0.
void *lxUnresolvedStub(const char *name);

// Each bridge file's table (bridges.c registers them all).
void lxLibcInit(void);
void lxStdioInit(void);
void lxMathInit(void);
void lxPthreadInit(void);
void lxDlInit(void);
void lxMiscInit(void);
void lxX11Init(void);
void lxGlInit(void);

// Logging (stderr, unbuffered), and the verbose trace (LL64_TRACE=1).
void lxLog(const char *fmt, ...) __attribute__((format(gnu_printf, 1, 2)));
extern int lxTrace, lxLogEnabled;
#define lxTraceLog(...) do { if (lxTrace) lxLog(__VA_ARGS__); } while (0)

// The ELF modules (elf64.c).
typedef struct LxModule LxModule;
LxModule *lxLoadExecutable(const char *path);
// A loaded module by its soname (NULL: not loaded, or a bridged library).
LxModule *lxFindModule(const char *name);
// Loads a Linux library (and its dependencies), relocates and initialises it.
LxModule *lxLoadLibrary(const char *name);
void *lxModuleSymbol(LxModule *m, const char *name);
// Looks a symbol up in every module, then the bridges.
void *lxGlobalSymbol(const char *name);
// The module whose mapping holds addr.
LxModule *lxModuleAt(const void *addr);
const char *lxModuleName(LxModule *m);
const char *lxModulePath(LxModule *m);
uint8_t *lxModuleBase(LxModule *m);
uintptr_t lxModuleDynamic(LxModule *m);
// The nearest symbol below addr (dladdr), or 0.
int lxModuleAddr(const void *addr, const char **sname, void **saddr);
// Runs the executable's entry point; does not return.
void lxRunExecutable(LxModule *exe, int argc, char **argv, char **envp) __attribute__((noreturn));
// Every module, in load order (dl_iterate_phdr).
typedef SYSV int (*LxPhdrCallback)(void *info, size_t size, void *data);
int lxIteratePhdr(LxPhdrCallback cb, void *data);
// The module that covers pc and its unwind table (_dl_find_object).
int lxFindObject(void *pc, void **mapStart, void **mapEnd, void **ehFrame, LxModule **m);
// Directories searched for the Linux libraries.
void lxAddLibraryPath(const char *dir);

// Static TLS in the TEB's TLS slots (tls64.c): %fs is replaced by %gs.
int lxTlsInit(void);
// The offset from the TEB of size bytes of TLS (align: a power of two).
size_t lxTlsReserve(size_t size, size_t align);
// Copies the modules' initial TLS images into the calling thread's slots.
void lxTlsThreadInit(void);
void lxTlsAddImage(size_t offset, const void *init, size_t initSize, size_t size);
// Rewrites the stack protector's %fs:0x28 reads in [start, start+size).
size_t lxPatchFs(uint8_t *start, size_t size);
void *lxTlsAddress(size_t offset);

// errno, Linux's numbers.
int *lxErrnoLocation(void);
void lxSetErrno(int linuxErrno);
// Windows CRT errno to Linux's.
int lxErrnoFromCrt(int e);
void lxSetErrnoFromCrt(void);

// Paths: the game's Linux paths (/pm/..., /proc/...) to Windows ones.
const char *lxMapPath(const char *path, char *buf, size_t size);

// The game's directory (its working directory) and the loader's.
extern char lxGameDir[];
extern char lxExeDir[];

// The process exit, running the atexit handlers (libc64.c).
void lxExit(int code) __attribute__((noreturn));

// The g7 runtime (g7win.c): hooks applied after the executable is relocated,
// before its constructors.
void lxG7Setup(LxModule *exe);
// The config file (iniParser's IniConfig), or NULL.
void *lxIni(void);
const char *lxIniValue(const char *section, const char *key);
int lxIniInt(const char *section, const char *key, int def);

// The X11/GLX window (x11win.c).
int lxKeyHeld(int vk);
int lxGameFocused(void);
// The cursor on the game's picture, 0..1 from its top left: 0 when off it.
int lxGamePointer(float *x, float *y);

#endif
