// Hooks by name for the Namco N2 games (see namcoN2.h). The games export
// every function, so a hook is the game's own symbol looked up in its
// dynamic symbol table (never a host library's of the same name, which a
// plain dlsym could return), its first instruction made a jump.

#include <stdint.h>
#include <string.h>

#if !defined(_WIN32)
#define _GNU_SOURCE
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef SHN_UNDEF
#define SHN_UNDEF 0
#endif
#endif

// These read the game's ELF symbol table, which always uses the ELF
// encoding: a host header (Windows) may define STT_FUNC differently.
#define N2_STT_FUNC 2

#include "namcoN2.h"
#include "../log/log.h"
#include "../../minhook/src/hde/hde32.h"

#ifndef _WIN32
// The game's dynamic symbols (the main program: the first object).
static const Elf32_Sym *symtab;
static const char *strtab;
static const Elf32_Word *hashtab; // DT_HASH: nbucket, nchain, buckets, chains
static uint32_t symbolCount;
static uintptr_t writableCodeStart, writableCodeEnd; // the RWX segment

static int findGame(struct dl_phdr_info *info, size_t size, void *data)
{
    (void)size;
    (void)data;
    for (int i = 0; i < info->dlpi_phnum; i++)
    {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD && (ph->p_flags & (PF_W | PF_X)) == (PF_W | PF_X))
        {
            writableCodeStart = info->dlpi_addr + ph->p_vaddr;
            writableCodeEnd = writableCodeStart + ph->p_memsz;
        }
        if (ph->p_type != PT_DYNAMIC)
            continue;
        for (const ElfW(Dyn) *d = (const ElfW(Dyn) *)(info->dlpi_addr + ph->p_vaddr); d->d_tag != DT_NULL; d++)
        {
            if (d->d_tag == DT_SYMTAB)
                symtab = (const Elf32_Sym *)(uintptr_t)d->d_un.d_ptr;
            else if (d->d_tag == DT_STRTAB)
                strtab = (const char *)(uintptr_t)d->d_un.d_ptr;
            else if (d->d_tag == DT_HASH)
                hashtab = (const Elf32_Word *)(uintptr_t)d->d_un.d_ptr;
        }
    }
    return 1; // the main program is the first object: stop there
}

static int loadSymbols(void)
{
    if (!symtab)
    {
        dl_iterate_phdr(findGame, NULL);
        if (hashtab)
            symbolCount = hashtab[1];
        if (!symtab || !strtab || !hashtab)
            log_error("Namco N2: the game's dynamic symbols were not found");
    }
    return symtab && strtab && hashtab;
}

static unsigned long elfHash(const char *name)
{
    unsigned long h = 0, g;
    while (*name)
    {
        h = (h << 4) + (unsigned char)*name++;
        if ((g = h & 0xf0000000))
            h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

void *namcoN2Symbol(const char *name)
{
    if (!loadSymbols())
        return NULL;
    uint32_t nbucket = hashtab[0];
    const Elf32_Word *bucket = hashtab + 2, *chain = bucket + nbucket;
    for (Elf32_Word i = bucket[elfHash(name) % nbucket]; i != STN_UNDEF; i = chain[i])
        if (symtab[i].st_shndx != SHN_UNDEF && strcmp(strtab + symtab[i].st_name, name) == 0)
            return (void *)(uintptr_t)symtab[i].st_value;
    return NULL;
}

static int writable(void *address, size_t n)
{
    uintptr_t page = (uintptr_t)address & ~(uintptr_t)0xfff;
    size_t len = ((uintptr_t)address + n - page + 0xfff) & ~(uintptr_t)0xfff;
    return mprotect((void *)page, len, PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}
#else
// The loader's view of the game (the main executable): its dynamic symbols
// and the ranges of its segments (see elfLoader/elfLoader.cpp).
void *rtMainSymbol(const char *name);
int rtMainForeachSymbol(int (*callback)(const char *name, uint32_t value, uint32_t size, unsigned char type, unsigned shndx, void *arg),
                        void *arg);
int rtMainWritableCodeSegment(uint32_t *start, uint32_t *end);

static uint32_t writableCodeStart, writableCodeEnd; // the RWX segment

void *namcoN2Symbol(const char *name)
{
    return rtMainSymbol(name);
}

static int writable(void *address, size_t n)
{
    uintptr_t page = (uintptr_t)address & ~(uintptr_t)0xfff;
    size_t len = ((uintptr_t)address + n - page + 0xfff) & ~(uintptr_t)0xfff;
    DWORD old;
    return VirtualProtect((void *)page, len, PAGE_EXECUTE_READWRITE, &old) != 0;
}

struct EntryPointContext
{
    const char *prefix;
    size_t prefixLen;
    void *(*resolve)(const char *);
    int patched;
};

static int epCallback(const char *name, uint32_t value, uint32_t size, unsigned char type, unsigned shndx, void *arg)
{
    (void)size;
    struct EntryPointContext *ctx = arg;

    if (shndx == SHN_UNDEF || type != N2_STT_FUNC || strncmp(name, ctx->prefix, ctx->prefixLen) != 0 ||
        value < writableCodeStart || value >= writableCodeEnd)
        return 1;
    void *to = ctx->resolve(name);
    uint8_t *slot = (uint8_t *)(uintptr_t)value;
    if (!to || !writable(slot, 7))
        return 1;
    // mov $to,%eax; jmp *%eax (the slots are 32 bytes apart)
    slot[0] = 0xb8;
    *(uint32_t *)(slot + 1) = (uint32_t)(uintptr_t)to;
    slot[5] = 0xff;
    slot[6] = 0xe0;
    ctx->patched++;
    return 1;
}
#endif

static void writeJump(uint8_t *from, const void *to)
{
    from[0] = 0xe9;
    *(int32_t *)(from + 1) = (int32_t)((const uint8_t *)to - (from + 5));
}

int namcoN2Hook(const char *name, void *replacement)
{
    uint8_t *target = namcoN2Symbol(name);
    if (!target)
    {
        log_debug("Namco N2: %s is not the game's", name);
        return 0;
    }
    if (!writable(target, 5))
        return 0;
    writeJump(target, replacement);
    return 1;
}

// The trampolines: the moved instructions, then a jump back.
static uint8_t *trampolines;
static size_t trampolinesUsed;

int namcoN2HookOriginal(const char *name, void *replacement, void **original)
{
    uint8_t *target = namcoN2Symbol(name);
    size_t n = 0;

    if (!target)
    {
        log_debug("Namco N2: %s is not the game's", name);
        return 0;
    }
    if (!trampolines)
    {
#ifdef _WIN32
        trampolines = (uint8_t *)VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!trampolines)
            return 0;
#else
        trampolines = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (trampolines == MAP_FAILED)
        {
            trampolines = NULL;
            return 0;
        }
#endif
    }
    uint8_t *t = trampolines + trampolinesUsed;
    // Whole instructions covering the 5 bytes of the jump; a relative call
    // or jump among them is re-aimed, any other relative operand refused.
    while (n < 5)
    {
        hde32s hs;
        unsigned len = hde32_disasm(target + n, &hs);
        if (hs.flags & F_ERROR)
            break;
        memcpy(t + n, target + n, len);
        if (hs.flags & F_RELATIVE)
        {
            if ((hs.opcode != 0xe8 && hs.opcode != 0xe9) || len != 5)
            {
                log_error("Namco N2: %s begins with a relative operand, not hooked", name);
                return 0;
            }
            *(int32_t *)(t + n + 1) = (int32_t)(target + n + 5 + (int32_t)hs.imm.imm32 - (t + n + 5));
        }
        n += len;
    }
    if (n < 5 || trampolinesUsed + n + 5 > 0x10000)
    {
        log_error("Namco N2: %s cannot be hooked", name);
        return 0;
    }
    writeJump(t + n, target + n);
    trampolinesUsed += (n + 5 + 15) & ~(size_t)15;
    if (!writable(target, 5))
        return 0;
    if (original)
        *original = t;
    writeJump(target, replacement);
    return 1;
}

int namcoN2PatchEntryPoints(const char *prefix, void *(*resolve)(const char *))
{
#ifndef _WIN32
    size_t prefixLen = strlen(prefix);
    int patched = 0;

    if (!loadSymbols())
        return 0;
    for (uint32_t i = 1; i < symbolCount; i++)
    {
        const Elf32_Sym *s = &symtab[i];
        const char *name = strtab + s->st_name;
        if (s->st_shndx == SHN_UNDEF || ELF32_ST_TYPE(s->st_info) != STT_FUNC || strncmp(name, prefix, prefixLen) != 0 ||
            s->st_value < writableCodeStart ||
            s->st_value >= writableCodeEnd)
            continue;
        void *to = resolve(name);
        uint8_t *slot = (uint8_t *)(uintptr_t)s->st_value;
        if (!to || !writable(slot, 7))
            continue;
        // mov $to,%eax; jmp *%eax (the slots are 32 bytes apart)
        slot[0] = 0xb8;
        *(uint32_t *)(slot + 1) = (uint32_t)(uintptr_t)to;
        slot[5] = 0xff;
        slot[6] = 0xe0;
        patched++;
    }
    return patched;
}
#else
    struct EntryPointContext ctx = {prefix, strlen(prefix), resolve, 0};
    if (!rtMainWritableCodeSegment(&writableCodeStart, &writableCodeEnd))
    {
        log_error("Namco N2: the game's writable code segment was not found");
        return 0;
    }
    rtMainForeachSymbol(epCallback, &ctx);
    return ctx.patched;
}
#endif
