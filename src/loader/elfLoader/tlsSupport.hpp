#pragma once

#include <stdint.h>

// Linux i386 ELF TLS for the games running on Windows.
//
// Implements the parts of the glibc TLS ABI the 32-bit game ELFs use:
//  - the static TLS layout (TCB, then the modules' TLS blocks, with the
//    same offsets glibc computes in _dl_determine_tlsoffset),
//  - a per-thread TCB/DTV,
//  - __tls_get_addr / ___tls_get_addr (the GD/LD models),
//  - the R_386_TLS_* dynamic relocations (see ElfLoader).
//
// The %fs segment is not redirected (on Windows it is the TEB): only
// pointer-based TLS works, i.e. code that calls __tls_get_addr. A game's
// own Local-Exec accesses (mov fs:0x70+off) would still read the TEB;
// the Raw Thrills 32-bit engines do not use them (the main ELF logs a
// warning when it has a PT_TLS segment).

namespace Tls
{
    // Register a loaded ELF's PT_TLS segment (or the main executable,
    // which always takes DTV index 1, even without TLS). Call after the
    // segments are mapped; initImage is the mapped p_filesz bytes (null
    // for a pure .tbss). Returns the module's DTV index (0 when the
    // shared object has no TLS).
    uint32_t RegisterModule(const char *name, bool isMain, uint32_t tpVaddr, uint32_t filesz, uint32_t memsz, uint32_t align,
                            const uint8_t *initImage);

    // A TLS symbol (ELF STT_TLS) exported by the module just registered:
    // its st_value is the offset within the module's TLS block.
    void RegisterSymbol(const char *name, uint32_t modid, uint32_t offset);

    // The DTV index of the module defining the symbol (0 when none).
    uint32_t FindSymbolModule(const char *name);

    // The symbol's offset within its defining module (0 when unknown).
    uint32_t FindSymbolTlsOffset(const char *name);

    // The module's static TLS offset (its block start, relative to the
    // TCB), for the R_386_TLS_TPOFF* relocations. 0 when unknown.
    uint32_t GetModuleTlsOffset(uint32_t modid);

    // The per-thread TLS block (TCB + the static module blocks) and DTV
    // for the calling thread; returns the TCB. Idempotent. Called from
    // the game's main thread (ElfLoader::Execute) and each emulated
    // pthread before game code runs, and lazily from the first
    // __tls_get_addr on any other thread (e.g. the SDL audio thread
    // running a game callback).
    void *EnsureThreadTls();

    // Free the calling thread's TLS block (thread exit).
    void ReleaseThreadTls();

    // The __tls_get_addr body: modid/offset are the GOT pair the i386
    // GD/LD code passes (a tls_index {ti_module, ti_offset}).
    void *GetAddr(uint32_t modid, uint32_t offset);

    // The two spellings glibc exports (sysdeps/i386/dl-tls.h): the GNU
    // form takes the pair in %eax (regparm(1)), the "Sun" form passes
    // it on the stack. Both names must resolve (games import either).
    extern "C" void *___tls_get_addr(void *ti) __attribute__((regparm(1)));
    extern "C" void *__tls_get_addr(void *ti);
}
