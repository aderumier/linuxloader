// Calls from a game built for a 4-byte-aligned stack (Dead Heat, built with
// Intel's compiler) into libraries built for the 16-byte alignment the i386
// ABI has required since: the libraries keep SSE/AVX values on their stack
// with aligned moves, which fault on the game's stack (std::istream::seekg
// in the game's first CSV read). Each of the game's imports goes through a
// stub that aligns the stack, copies the arguments over and calls the real
// function.

#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "namcoEs1.h"
#include "../common/alignedCall.h"
#include "../log/log.h"

void *namcoEs1AlignedPointer(void *function)
{
    static struct
    {
        void *function, *stub;
    } cache[64];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    void *stub = NULL;

    if (!function)
        return NULL;
    pthread_mutex_lock(&lock);
    for (size_t i = 0; i < sizeof(cache) / sizeof(cache[0]) && !stub; i++)
        if (cache[i].function == function)
            stub = cache[i].stub;
        else if (!cache[i].function && (stub = alignedCallStub((uintptr_t)function)))
        {
            cache[i].function = function;
            cache[i].stub = stub;
        }
    pthread_mutex_unlock(&lock);
    return stub ? stub : function;
}

#ifdef _WIN32
// The loader records the game's import slots as it relocates them
// (common/elfImports.h): they hold the addresses the game's calls go to,
// which the stubs call on.
#include "../common/elfImports.h"

void namcoEs1AlignImports(void)
{
    static int done;
    const char *names[1024];
    uint32_t *slots[1024];
    int n, count = 0;

    if (done)
        return;
    done = 1;
    n = loaderImportSlots(names, slots, 1024);
    for (int i = 0; i < n; i++)
    {
        void *stub;
        if (alignedCallDirect(names[i]) || !slots[i])
            continue;
        stub = alignedCallStub((uintptr_t)*slots[i]);
        if (!stub)
        {
            log_error("Namco: no memory for the import stubs");
            return;
        }
        *slots[i] = (uint32_t)(uintptr_t)stub;
        count++;
    }
    log_info("Namco: the game: %d imports called on an aligned stack", count);
}
#else
#include <dlfcn.h>
#include <elf.h>
#include <link.h>

// Objects done (a library loaded again keeps its stubs).
static struct link_map *alignedObjects[16];

static int aligned(struct link_map *obj)
{
    const size_t n = sizeof(alignedObjects) / sizeof(alignedObjects[0]);
    for (size_t i = 0; i < n; i++)
        if (alignedObjects[i] == obj)
            return 1;
    for (size_t i = 0; i < n; i++)
        if (!alignedObjects[i])
        {
            alignedObjects[i] = obj;
            break;
        }
    return 0;
}

// The version the executable asks for (symbol index sym), or NULL.
static const char *symbolVersion(const Elf32_Half *versym, const Elf32_Verneed *verneed, const char *strtab, size_t sym)
{
    if (!versym || !verneed)
        return NULL;
    Elf32_Half index = versym[sym] & 0x7fff;
    for (const Elf32_Verneed *vn = verneed;; vn = (const Elf32_Verneed *)((const char *)vn + vn->vn_next))
    {
        const Elf32_Vernaux *aux = (const Elf32_Vernaux *)((const char *)vn + vn->vn_aux);
        for (int i = 0; i < vn->vn_cnt; i++, aux = (const Elf32_Vernaux *)((const char *)aux + aux->vna_next))
            if (aux->vna_other == index)
                return strtab + aux->vna_name;
        if (!vn->vn_next)
            return NULL;
    }
}

// What ld.so binds a slot of obj to: the loader's own definition first (it
// is preloaded), else the version asked for, from the libraries after the
// loader for the executable (not the executable itself: a function whose
// address the game takes is defined there, as its PLT entry, which jumps
// through the slot being bound), from its own scope for a loaded library.
static void *resolve(const char *name, const char *version, struct link_map *obj, int isExe)
{
    Dl_info self, def;
    void *p = dlsym(RTLD_DEFAULT, name);
    void *scope = isExe ? RTLD_NEXT : (void *)obj;

    if (p && dladdr((void *)namcoEs1AlignImports, &self) && dladdr(p, &def) && def.dli_fbase == self.dli_fbase)
        return p;
    if (version && (p = dlvsym(scope, name, version)))
        return p;
    return dlsym(scope, name);
}

static void *alignedDlopen(const char *file, int mode);

// The imports of one object (the executable, or a library it loads).
static void alignObject(struct link_map *obj, int isExe)
{
    const Elf32_Rel *rel = NULL;
    const Elf32_Sym *symtab = NULL;
    const Elf32_Half *versym = NULL;
    const Elf32_Verneed *verneed = NULL;
    const char *strtab = NULL;
    size_t relSize = 0, count = 0, done = 0;
    uint32_t pltStart = UINT32_MAX, pltEnd = 0;

    for (const Elf32_Dyn *dyn = obj->l_ld; dyn->d_tag != DT_NULL; dyn++)
    {
        if (dyn->d_tag == DT_JMPREL)
            rel = (const Elf32_Rel *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_PLTRELSZ)
            relSize = dyn->d_un.d_val;
        else if (dyn->d_tag == DT_SYMTAB)
            symtab = (const Elf32_Sym *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_STRTAB)
            strtab = (const char *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_VERSYM)
            versym = (const Elf32_Half *)dyn->d_un.d_ptr;
        else if (dyn->d_tag == DT_VERNEED)
            verneed = (const Elf32_Verneed *)dyn->d_un.d_ptr;
    }
    // ld.so rebased the others in place, not this one.
    if (verneed && (uintptr_t)verneed < obj->l_addr)
        verneed = (const Elf32_Verneed *)((uintptr_t)verneed + obj->l_addr);
    if (!rel || !symtab || !strtab)
        return;
    count = relSize / sizeof(*rel);

    // A slot not bound yet (lazy binding) still points at its PLT entry, in
    // the object's own code.
    Dl_info info;
    uint32_t base = 0;
    for (size_t i = 0; i < count; i++)
    {
        uint32_t v = obj->l_addr + rel[i].r_offset;
        v = *(uint32_t *)(uintptr_t)v;
        if (!base && dladdr(obj->l_ld, &info))
            base = (uint32_t)(uintptr_t)info.dli_fbase;
        if (dladdr((void *)(uintptr_t)v, &info) && (uint32_t)(uintptr_t)info.dli_fbase == base)
        {
            pltStart = v < pltStart ? v : pltStart;
            pltEnd = v > pltEnd ? v : pltEnd;
        }
    }

    for (size_t i = 0; i < count; i++)
    {
        size_t sym = ELF32_R_SYM(rel[i].r_info);
        const char *name = strtab + symtab[sym].st_name;
        uint32_t *slot = (uint32_t *)(uintptr_t)(obj->l_addr + rel[i].r_offset);
        uint32_t target = *slot;
        if (ELF32_R_TYPE(rel[i].r_info) != R_386_JMP_SLOT || alignedCallDirect(name))
            continue;
        if (target >= pltStart && target <= pltEnd)
        {
            void *p = resolve(name, symbolVersion(versym, verneed, strtab, sym), obj, isExe);
            if (!p)
                continue; // left to ld.so, unaligned
            target = (uint32_t)(uintptr_t)p;
        }
        if (!strcmp(name, "dlopen"))
            target = (uint32_t)(uintptr_t)alignedDlopen;
        void *stub = alignedCallStub(target);
        if (!stub)
        {
            log_error("Namco: no memory for the import stubs");
            break;
        }
        *slot = (uint32_t)(uintptr_t)stub;
        done++;
    }
    log_info("Namco: %s: %zu imports called on an aligned stack", obj->l_name[0] ? obj->l_name : "the game", done);
}

// The libraries the game loads itself (its sound driver, nsAdrv.dll, built
// as it is) get the same.
static void *alignedDlopen(const char *file, int mode)
{
    void *handle = dlopen(file, mode);
    struct link_map *obj;

    if (handle && file && dlinfo(handle, RTLD_DI_LINKMAP, &obj) == 0 && !aligned(obj))
        alignObject(obj, 0);
    return handle;
}

void namcoEs1AlignImports(void)
{
    struct link_map *exe = dlopen(NULL, RTLD_NOW);
    if (exe && !aligned(exe))
        alignObject(exe, 1);
}
#endif // _WIN32
