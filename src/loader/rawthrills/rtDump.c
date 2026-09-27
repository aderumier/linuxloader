// Rebuild the import tables of a Raw Thrills game dump and patch its code.
//
// The dumps were taken from running HASP Envelope-protected binaries: their
// GOTs still hold the cabinet's library addresses, and the envelope stripped
// or encrypted the relocation info that would let ld.so fix them. Each slot
// is resolved again by name (rtGame.h tables) before the game code runs.

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "rawthrills.h"
#include "../log/log.h"

#define MAX_PRELOADED 8

// Old glibc entry points (__xstat, __strtol_internal, ...) only survive as
// versioned compat symbols, which plain dlsym() does not return.
static void *lookupCompat(const char *name)
{
    static const char *const versions[] = {"GLIBC_2.0", "GLIBC_2.1", "GLIBC_2.2", "GLIBC_2.3"};
    void *p = dlsym(RTLD_DEFAULT, name);
    for (size_t i = 0; !p && i < sizeof(versions) / sizeof(versions[0]); i++)
        p = dlvsym(RTLD_DEFAULT, name, versions[i]);
    return p;
}

// Handles of the LD_PRELOAD libraries (the loader itself, ...). Their symbols
// must win over a versioned lookup, which would bind straight to glibc: that
// is what ld.so would do for a normally linked game.
static size_t getPreloaded(void **handles, size_t max)
{
    const char *env = getenv("LD_PRELOAD");
    size_t count = 0;
    char *list, *save, *tok;

    if (!env || !(list = strdup(env)))
        return 0;
    for (tok = strtok_r(list, ": ", &save); tok && count < max; tok = strtok_r(NULL, ": ", &save))
    {
        void *h = dlopen(tok, RTLD_NOW | RTLD_NOLOAD);
        if (h)
            handles[count++] = h;
    }
    free(list);
    return count;
}

// The loader's GL wrappers patch Lindbergh games (resolution, shaders) and
// need state set up by the Lindbergh init; Raw Thrills games get the real
// GL/GLU/glut/GLX functions, apart from the loader's Raw Thrills shader fix.
static int bypassPreloaded(const char *name)
{
    return !strncmp(name, "gl", 2) && strcmp(name, "glShaderSource") != 0;
}

static void *resolveImport(const char *name, const char *version, void **preloaded, size_t npreloaded)
{
    void *def = dlsym(RTLD_DEFAULT, name);
    for (size_t i = 0; def && i < npreloaded; i++)
    {
        if (dlsym(preloaded[i], name) != def)
            continue;
        if (!bypassPreloaded(name))
            return def;
        void *next = dlsym(RTLD_NEXT, name);
        if (next)
            return next;
    }

    void *p = NULL;
    if (version && version[0])
        p = dlvsym(RTLD_DEFAULT, name, version);
    return p ? p : lookupCompat(name);
}

int rtFixImports(const RtGame *game)
{
    void *preloaded[MAX_PRELOADED];
    size_t npreloaded = getPreloaded(preloaded, MAX_PRELOADED);
    int missing = 0;

    for (const char *const *lib = game->extraLibs; lib && *lib; lib++)
        if (!dlopen(*lib, RTLD_NOW | RTLD_GLOBAL))
            log_warn("Raw Thrills: %s: %s", *lib, dlerror());

    uint32_t *envelopeGot = (uint32_t *)(uintptr_t)game->envelopeGot;
    for (size_t i = 0; i < game->envelopeImportCount; i++)
    {
        const RtEnvelopeImport *imp = &game->envelopeImports[i];
        void *p = resolveImport(imp->name, NULL, preloaded, npreloaded);
        if (!p)
        {
            log_warn("Raw Thrills: envelope import %s not found", imp->name);
            missing++;
            continue;
        }
        envelopeGot[imp->slot] = (uint32_t)(uintptr_t)p;
    }
    if (game->envelopeSelfSlot >= 0)
        envelopeGot[game->envelopeSelfSlot] = game->envelopeSelfTarget;

    for (size_t i = 0; i < game->gameImportCount; i++)
    {
        const RtGameImport *imp = &game->gameImports[i];
        void *p = resolveImport(imp->name, imp->version, preloaded, npreloaded);
        if (!p)
        {
            // Weak hooks such as __gmon_start__ are legitimately absent.
            if (strcmp(imp->name, "__gmon_start__") != 0)
            {
                log_warn("Raw Thrills: game import %s@%s not found", imp->name, imp->version);
                missing++;
            }
            continue;
        }
        *(uint32_t *)(uintptr_t)imp->slot = (uint32_t)(uintptr_t)p;
    }

    log_info("Raw Thrills: import tables rebuilt (%d missing)", missing);
    return missing;
}

// The game exports its symbols and its code segment is mapped RWE, so hooks
// simply overwrite a function entry with a jmp to the replacement.
void rtDetourAddress(uint32_t address, void *replacement)
{
    uint8_t *target = (uint8_t *)(uintptr_t)address;
    target[0] = 0xe9;
    *(int32_t *)(target + 1) = (int32_t)((uint8_t *)replacement - (target + 5));
}

int rtDetour(const char *name, void *replacement)
{
    void *target = dlsym(RTLD_DEFAULT, name);
    if (!target)
    {
        log_warn("Raw Thrills: hook target %s not found", name);
        return -1;
    }
    rtDetourAddress((uint32_t)(uintptr_t)target, replacement);
    return 0;
}

// Copy the first prologueLength bytes of a function (whole, position
// independent instructions) and jump back after them, so the original can
// still be called once its entry is detoured.
void *rtTrampoline(const char *name, size_t prologueLength)
{
    uint8_t *target = dlsym(RTLD_DEFAULT, name);
    if (!target)
        return NULL;
    uint8_t *tramp = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tramp == MAP_FAILED)
        return NULL;
    memcpy(tramp, target, prologueLength);
    tramp[prologueLength] = 0xe9;
    *(int32_t *)(tramp + prologueLength + 1) =
        (int32_t)((target + prologueLength) - (tramp + prologueLength + 5));
    return tramp;
}

// A dump taken once the game was running also holds the runtime state of
// its .bss (e.g. an initialized SDL with pointers into a heap that no longer
// exists). This runs before the game's constructors, as the kernel would
// hand it over: zero it, keeping the objects ld.so copied in.
void rtClearBss(const RtGame *game)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
    const Elf32_Dyn *dyn;
    const Elf32_Rel *rel = NULL;
    const Elf32_Sym *symtab = NULL;
    size_t relSize = 0;
    struct
    {
        uint32_t address;
        uint32_t size;
        uint8_t data[64];
    } saved[16];
    int nsaved = 0;

    if (!game->bssStart || !exe)
        return;
    for (dyn = exe->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_REL)
            rel = (const Elf32_Rel *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_RELSZ)
            relSize = dyn->d_un.d_val;
        else if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
    }
    for (size_t i = 0; rel && symtab && i < relSize / sizeof(*rel); i++)
    {
        const Elf32_Sym *sym = &symtab[ELF32_R_SYM(rel[i].r_info)];
        if (ELF32_R_TYPE(rel[i].r_info) != R_386_COPY || rel[i].r_offset < game->bssStart ||
            rel[i].r_offset >= game->bssEnd)
            continue;
        if (nsaved == (int)(sizeof(saved) / sizeof(saved[0])) || sym->st_size > sizeof(saved[0].data))
        {
            log_error("Raw Thrills: cannot keep the copy relocation at %#x, .bss left as dumped", rel[i].r_offset);
            return;
        }
        saved[nsaved].address = rel[i].r_offset;
        saved[nsaved].size = sym->st_size;
        memcpy(saved[nsaved].data, (void *)(uintptr_t)rel[i].r_offset, sym->st_size);
        nsaved++;
    }
    memset((void *)(uintptr_t)game->bssStart, 0, game->bssEnd - game->bssStart);
    for (int i = 0; i < nsaved; i++)
        memcpy((void *)(uintptr_t)saved[i].address, saved[i].data, saved[i].size);
}

// Number of dynamic symbols, from DT_HASH (nchain) or DT_GNU_HASH.
static uint32_t dynSymCount(const Elf32_Dyn *dyn)
{
    for (; dyn->d_tag != DT_NULL; dyn++)
        if (dyn->d_tag == DT_HASH)
            return ((const uint32_t *)dyn->d_un.d_ptr)[1];
    return 0;
}

int rtReplaceLib(const RtGame *game)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
    const Elf32_Sym *symtab = NULL;
    const char *strtab = NULL;
    size_t prefixLen;
    void *lib;
    int replaced = 0, missing = 0;

    if (!game->replacedLib)
        return 0;
    if (!(lib = dlopen(game->replacedLib, RTLD_NOW | RTLD_LOCAL)))
    {
        log_error("Raw Thrills: cannot load %s: %s", game->replacedLib, dlerror());
        return -1;
    }
    for (const Elf32_Dyn *dyn = exe->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_STRTAB)
            strtab = (const char *)dyn->d_un.d_ptr;
    }
    prefixLen = strlen(game->replacedLibPrefix);
    for (uint32_t i = 1; symtab && strtab && i < dynSymCount(exe->l_ld); i++)
    {
        const Elf32_Sym *sym = &symtab[i];
        const char *name = strtab + sym->st_name;
        void *target;

        if (sym->st_shndx == SHN_UNDEF || ELF32_ST_TYPE(sym->st_info) != STT_FUNC ||
            strncmp(name, game->replacedLibPrefix, prefixLen))
            continue;
        if (!(target = dlsym(lib, name)))
        {
            missing++;
            continue;
        }
        rtDetourAddress(sym->st_value, target);
        replaced++;
    }
    log_info("Raw Thrills: %d %s functions sent to %s (%d not found)", replaced, game->replacedLibPrefix,
             game->replacedLib, missing);
    return 0;
}
