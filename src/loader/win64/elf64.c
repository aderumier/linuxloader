// The ELF64 modules: the game's executable at its own addresses, its Linux
// libraries wherever Windows maps them, relocated against each other and the
// bridges (see ll64.h).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ll64.h"

typedef struct
{
    unsigned char e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct
{
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
} Elf64_Phdr;

typedef struct
{
    int64_t d_tag;
    uint64_t d_val;
} Elf64_Dyn;

typedef struct
{
    uint32_t st_name;
    unsigned char st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
} Elf64_Sym;

typedef struct
{
    uint64_t r_offset, r_info;
    int64_t r_addend;
} Elf64_Rela;

typedef struct
{
    uint16_t vd_version, vd_flags, vd_ndx, vd_cnt;
    uint32_t vd_hash, vd_aux, vd_next;
} Elf64_Verdef;

typedef struct
{
    uint32_t vda_name, vda_next;
} Elf64_Verdaux;

typedef struct
{
    uint16_t vn_version, vn_cnt;
    uint32_t vn_file, vn_aux, vn_next;
} Elf64_Verneed;

typedef struct
{
    uint32_t vna_hash;
    uint16_t vna_flags, vna_other;
    uint32_t vna_name, vna_next;
} Elf64_Vernaux;

enum
{
    PT_LOAD = 1, PT_DYNAMIC = 2, PT_PHDR = 6, PT_TLS = 7, PT_GNU_EH_FRAME = 0x6474e550,
    PF_X = 1,
    DT_NULL = 0, DT_NEEDED = 1, DT_PLTRELSZ = 2, DT_HASH = 4, DT_STRTAB = 5, DT_SYMTAB = 6, DT_RELA = 7,
    DT_RELASZ = 8, DT_STRSZ = 10, DT_INIT = 12, DT_FINI = 13, DT_SONAME = 14, DT_JMPREL = 23,
    DT_INIT_ARRAY = 25, DT_FINI_ARRAY = 26, DT_INIT_ARRAYSZ = 27, DT_FINI_ARRAYSZ = 28,
    DT_RELRSZ = 35, DT_RELR = 36, DT_GNU_HASH = 0x6ffffef5, DT_VERSYM = 0x6ffffff0, DT_VERDEF = 0x6ffffffc,
    DT_VERDEFNUM = 0x6ffffffd, DT_VERNEED = 0x6ffffffe, DT_VERNEEDNUM = 0x6fffffff,
    R_X86_64_NONE = 0, R_X86_64_64 = 1, R_X86_64_PC32 = 2, R_X86_64_COPY = 5, R_X86_64_GLOB_DAT = 6,
    R_X86_64_JUMP_SLOT = 7, R_X86_64_RELATIVE = 8, R_X86_64_32 = 10, R_X86_64_32S = 11,
    R_X86_64_DTPMOD64 = 16, R_X86_64_DTPOFF64 = 17, R_X86_64_TPOFF64 = 18, R_X86_64_IRELATIVE = 37,
    STB_LOCAL = 0, STB_WEAK = 2, STT_TLS = 6, STT_GNU_IFUNC = 10,
};

#define MAX_DEPS 32
#define MAX_VERSIONS 64

struct LxModule
{
    char name[128];
    char path[MAX_PATH];
    int isExe, tlsModid, initDone;
    uint8_t *bias;
    uint8_t *mapStart;
    size_t mapSize;
    Elf64_Phdr *phdr;
    int phnum;
    Elf64_Dyn *dyn;
    Elf64_Sym *symtab;
    const char *strtab;
    uint32_t *hash, *gnuhash;
    uint16_t *versym;
    const char *versions[MAX_VERSIONS];
    Elf64_Rela *rela, *jmprel;
    size_t relasz, jmprelsz;
    uint64_t *relr;
    size_t relrsz;
    uint64_t init, fini, *initArray, *finiArray;
    size_t initArraySz, finiArraySz;
    size_t tlsOffset, tlsSize, tlsAlign, tlsInitSize;
    void *tlsInit;
    void *ehFrameHdr;
    uint64_t entry;
    LxModule *deps[MAX_DEPS];
    int ndeps;
    LxModule *next;
};

static LxModule *modules, *lastModule;
static int nextModid = 1;

#define MAX_PATHS 8
static char libraryPaths[MAX_PATHS][MAX_PATH];
static int nPaths;

void lxAddLibraryPath(const char *dir)
{
    if (nPaths < MAX_PATHS)
        snprintf(libraryPaths[nPaths++], MAX_PATH, "%s", dir);
}

// The libraries the bridges are: never loaded from disk.
static const char *const bridgedLibs[] = {
    "libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1", "libGL.so.1", "libGLU.so.1",
    "libudev.so.1", "libudev.so.0", "ld-linux-x86-64.so.2", "libX11.so.6", "libXext.so.6",
};

static int isBridged(const char *name)
{
    for (size_t i = 0; i < LX_COUNT(bridgedLibs); i++)
        if (!strcmp(name, bridgedLibs[i]))
            return 1;
    return 0;
}

const char *lxModuleName(LxModule *m) { return m->name; }
const char *lxModulePath(LxModule *m) { return m->path; }
uint8_t *lxModuleBase(LxModule *m) { return m->bias; }
uintptr_t lxModuleDynamic(LxModule *m) { return (uintptr_t)m->dyn; }

LxModule *lxFindModule(const char *name)
{
    for (LxModule *m = modules; m; m = m->next)
        if (!strcmp(m->name, name))
            return m;
    return NULL;
}

LxModule *lxModuleAt(const void *addr)
{
    for (LxModule *m = modules; m; m = m->next)
        if ((const uint8_t *)addr >= m->mapStart && (const uint8_t *)addr < m->mapStart + m->mapSize)
            return m;
    return NULL;
}

static uint32_t elfHash(const char *s)
{
    uint32_t h = 0, g;
    while (*s)
    {
        h = (h << 4) + (unsigned char)*s++;
        if ((g = h & 0xf0000000))
            h ^= g >> 24;
        h &= ~g;
    }
    return h;
}

static uint32_t gnuHash(const char *s)
{
    uint32_t h = 5381;
    while (*s)
        h = h * 33 + (unsigned char)*s++;
    return h;
}

static size_t symbolCount(LxModule *m)
{
    if (m->hash)
        return m->hash[1];
    if (m->gnuhash)
    {
        uint32_t nbuckets = m->gnuhash[0], symoffset = m->gnuhash[1], bloomSize = m->gnuhash[2];
        uint32_t *buckets = m->gnuhash + 4 + bloomSize * 2, *chain = buckets + nbuckets, last = 0;
        for (uint32_t i = 0; i < nbuckets; i++)
            if (buckets[i] > last)
                last = buckets[i];
        if (last < symoffset)
            return symoffset;
        while (!(chain[last - symoffset] & 1))
            last++;
        return last + 1;
    }
    return 0;
}

// Is symbol i of m a definition of name that version (NULL: any) accepts?
// 2: its version, 1: its default version, 0: no.
static int matches(LxModule *m, uint32_t i, const char *name, const char *version)
{
    Elf64_Sym *s = &m->symtab[i];
    if (!s->st_shndx || (s->st_info >> 4) == STB_LOCAL || strcmp(m->strtab + s->st_name, name))
        return 0;
    if (!m->versym)
        return 1;
    uint16_t v = m->versym[i];
    int hidden = v & 0x8000;
    v &= 0x7fff;
    if (version && v < MAX_VERSIONS && m->versions[v] && !strcmp(m->versions[v], version))
        return 2;
    return hidden ? 0 : 1;
}

static Elf64_Sym *findIn(LxModule *m, const char *name, const char *version)
{
    Elf64_Sym *fallback = NULL;
    if (m->gnuhash)
    {
        uint32_t nbuckets = m->gnuhash[0], symoffset = m->gnuhash[1], bloomSize = m->gnuhash[2];
        uint32_t *buckets = m->gnuhash + 4 + bloomSize * 2, *chain = buckets + nbuckets;
        uint32_t h = gnuHash(name), i = buckets[h % nbuckets];
        if (i < symoffset)
            return NULL;
        for (;; i++)
        {
            uint32_t ch = chain[i - symoffset];
            if ((ch | 1) == (h | 1))
            {
                int r = matches(m, i, name, version);
                if (r == 2)
                    return &m->symtab[i];
                if (r && !fallback)
                    fallback = &m->symtab[i];
            }
            if (ch & 1)
                break;
        }
        return fallback;
    }
    if (m->hash)
    {
        uint32_t nbucket = m->hash[0], *bucket = m->hash + 2, *chain = bucket + nbucket;
        for (uint32_t i = bucket[elfHash(name) % nbucket]; i; i = chain[i])
        {
            int r = matches(m, i, name, version);
            if (r == 2)
                return &m->symtab[i];
            if (r && !fallback)
                fallback = &m->symtab[i];
        }
        return fallback;
    }
    return NULL;
}

static void *symbolAddress(LxModule *m, Elf64_Sym *s)
{
    void *p = m->bias + s->st_value;
    if ((s->st_info & 0xf) == STT_GNU_IFUNC)
        p = ((SYSV void *(*)(void))p)();
    return p;
}

void *lxModuleSymbol(LxModule *m, const char *name)
{
    Elf64_Sym *s = findIn(m, name, NULL);
    return s ? symbolAddress(m, s) : NULL;
}

// The global scope: the executable, the libraries in load order, then the
// bridges. skip: a module left out (copy relocations).
static void *lookup(const char *name, const char *version, LxModule *skip, LxModule **def, Elf64_Sym **defSym)
{
    for (LxModule *m = modules; m; m = m->next)
    {
        Elf64_Sym *s;
        if (m == skip || !(s = findIn(m, name, version)))
            continue;
        if (def)
            *def = m;
        if (defSym)
            *defSym = s;
        return (s->st_info & 0xf) == STT_TLS ? NULL : symbolAddress(m, s);
    }
    if (def)
        *def = NULL;
    return lxBridge(name);
}

void *lxGlobalSymbol(const char *name)
{
    return lookup(name, NULL, NULL, NULL, NULL);
}

int lxModuleAddr(const void *addr, const char **sname, void **saddr)
{
    LxModule *m = lxModuleAt(addr);
    size_t n, best = 0;
    uintptr_t bestAddr = 0;
    if (!m)
        return 0;
    n = symbolCount(m);
    for (size_t i = 1; i < n; i++)
    {
        Elf64_Sym *s = &m->symtab[i];
        uintptr_t a = (uintptr_t)(m->bias + s->st_value);
        if (s->st_shndx && s->st_value && a <= (uintptr_t)addr && a > bestAddr)
            best = i, bestAddr = a;
    }
    *sname = best ? m->strtab + m->symtab[best].st_name : NULL;
    *saddr = (void *)bestAddr;
    return 1;
}

// ---------------------------------------------------------------------------
// Mapping.

static int findLibrary(const char *name, char *path)
{
    for (int i = 0; i < nPaths; i++)
    {
        snprintf(path, MAX_PATH, "%s\\%s", libraryPaths[i], name);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            return 1;
    }
    return 0;
}

static void *readFile(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    if (!f)
        return NULL;
    _fseeki64(f, 0, SEEK_END);
    *size = (size_t)_ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    data = malloc(*size);
    if (data && fread(data, 1, *size, f) != *size)
    {
        free(data);
        data = NULL;
    }
    fclose(f);
    return data;
}

static void parseDynamic(LxModule *m)
{
    uint8_t *verdef = NULL, *verneed = NULL;
    size_t verdefnum = 0, verneednum = 0;
    for (Elf64_Dyn *d = m->dyn; d->d_tag != DT_NULL; d++)
    {
        uint64_t v = d->d_val;
        switch (d->d_tag)
        {
        case DT_STRTAB: m->strtab = (const char *)(m->bias + v); break;
        case DT_SYMTAB: m->symtab = (Elf64_Sym *)(m->bias + v); break;
        case DT_HASH: m->hash = (uint32_t *)(m->bias + v); break;
        case DT_GNU_HASH: m->gnuhash = (uint32_t *)(m->bias + v); break;
        case DT_RELA: m->rela = (Elf64_Rela *)(m->bias + v); break;
        case DT_RELASZ: m->relasz = v; break;
        case DT_JMPREL: m->jmprel = (Elf64_Rela *)(m->bias + v); break;
        case DT_PLTRELSZ: m->jmprelsz = v; break;
        case DT_RELR: m->relr = (uint64_t *)(m->bias + v); break;
        case DT_RELRSZ: m->relrsz = v; break;
        case DT_INIT: m->init = v; break;
        case DT_FINI: m->fini = v; break;
        case DT_INIT_ARRAY: m->initArray = (uint64_t *)(m->bias + v); break;
        case DT_INIT_ARRAYSZ: m->initArraySz = v / 8; break;
        case DT_FINI_ARRAY: m->finiArray = (uint64_t *)(m->bias + v); break;
        case DT_FINI_ARRAYSZ: m->finiArraySz = v / 8; break;
        case DT_VERSYM: m->versym = (uint16_t *)(m->bias + v); break;
        case DT_VERDEF: verdef = m->bias + v; break;
        case DT_VERDEFNUM: verdefnum = v; break;
        case DT_VERNEED: verneed = m->bias + v; break;
        case DT_VERNEEDNUM: verneednum = v; break;
        }
    }
    for (size_t i = 0; verdef && i < verdefnum; i++)
    {
        Elf64_Verdef *vd = (Elf64_Verdef *)verdef;
        Elf64_Verdaux *aux = (Elf64_Verdaux *)(verdef + vd->vd_aux);
        if (vd->vd_ndx < MAX_VERSIONS)
            m->versions[vd->vd_ndx] = m->strtab + aux->vda_name;
        if (!vd->vd_next)
            break;
        verdef += vd->vd_next;
    }
    for (size_t i = 0; verneed && i < verneednum; i++)
    {
        Elf64_Verneed *vn = (Elf64_Verneed *)verneed;
        uint8_t *a = verneed + vn->vn_aux;
        for (int j = 0; j < vn->vn_cnt; j++)
        {
            Elf64_Vernaux *aux = (Elf64_Vernaux *)a;
            if ((aux->vna_other & 0x7fff) < MAX_VERSIONS)
                m->versions[aux->vna_other & 0x7fff] = m->strtab + aux->vna_name;
            if (!aux->vna_next)
                break;
            a += aux->vna_next;
        }
        if (!vn->vn_next)
            break;
        verneed += vn->vn_next;
    }
}

static LxModule *mapModule(const char *name, const char *path, int isExe)
{
    size_t size;
    uint8_t *file = readFile(path, &size);
    Elf64_Ehdr *eh = (Elf64_Ehdr *)file;
    Elf64_Phdr *ph;
    uint64_t lo = UINT64_MAX, hi = 0;
    LxModule *m;

    if (!file)
    {
        lxLog("cannot read %s\n", path);
        return NULL;
    }
    if (size < sizeof(*eh) || memcmp(eh->e_ident, "\x7f" "ELF\x02", 5) || eh->e_machine != 62)
    {
        lxLog("%s: not an x86-64 ELF\n", path);
        free(file);
        return NULL;
    }
    ph = (Elf64_Phdr *)(file + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_LOAD)
        {
            if (ph[i].p_vaddr < lo)
                lo = ph[i].p_vaddr;
            if (ph[i].p_vaddr + ph[i].p_memsz > hi)
                hi = ph[i].p_vaddr + ph[i].p_memsz;
        }
    lo &= ~0xfffull;
    hi = (hi + 0xfff) & ~0xfffull;

    m = calloc(1, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "%s", name);
    snprintf(m->path, sizeof(m->path), "%s", path);
    m->isExe = isExe;
    m->mapSize = hi - lo;
    if (isExe)
    {
        // The executable's own addresses, reserved in this process before
        // Windows put anything there (main64.c).
        m->mapStart = VirtualAlloc((void *)lo, m->mapSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!m->mapStart)
            m->mapStart = VirtualAlloc((void *)lo, m->mapSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (m->mapStart != (uint8_t *)lo)
        {
            lxLog("%s: cannot map %#llx..%#llx (error %lu)\n", path, (unsigned long long)lo,
                  (unsigned long long)hi, GetLastError());
            free(file);
            free(m);
            return NULL;
        }
        m->bias = 0;
    }
    else
    {
        m->mapStart = VirtualAlloc(NULL, m->mapSize, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!m->mapStart)
        {
            lxLog("%s: cannot map %zu bytes\n", path, m->mapSize);
            free(file);
            free(m);
            return NULL;
        }
        m->bias = m->mapStart - lo;
    }
    for (int i = 0; i < eh->e_phnum; i++)
    {
        Elf64_Phdr *p = &ph[i];
        if (p->p_type == PT_LOAD)
        {
            size_t n = p->p_filesz;
            if (p->p_offset + n > size)
                n = p->p_offset < size ? size - p->p_offset : 0;
            memcpy(m->bias + p->p_vaddr, file + p->p_offset, n);
        }
        else if (p->p_type == PT_DYNAMIC)
            m->dyn = (Elf64_Dyn *)(m->bias + p->p_vaddr);
        else if (p->p_type == PT_GNU_EH_FRAME)
            m->ehFrameHdr = m->bias + p->p_vaddr;
        else if (p->p_type == PT_TLS)
        {
            m->tlsSize = p->p_memsz;
            m->tlsAlign = p->p_align ? p->p_align : 1;
            m->tlsInitSize = p->p_filesz;
            m->tlsInit = m->bias + p->p_vaddr;
        }
    }
    // The program headers, where the image has them (else a copy).
    m->phnum = eh->e_phnum;
    m->phdr = NULL;
    for (int i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_PHDR)
            m->phdr = (Elf64_Phdr *)(m->bias + ph[i].p_vaddr);
    if (!m->phdr)
        for (int i = 0; i < eh->e_phnum; i++)
            if (ph[i].p_type == PT_LOAD && ph[i].p_offset <= eh->e_phoff &&
                eh->e_phoff + eh->e_phnum * sizeof(Elf64_Phdr) <= ph[i].p_offset + ph[i].p_filesz)
                m->phdr = (Elf64_Phdr *)(m->bias + ph[i].p_vaddr + eh->e_phoff - ph[i].p_offset);
    if (!m->phdr)
    {
        m->phdr = malloc(eh->e_phnum * sizeof(Elf64_Phdr));
        memcpy(m->phdr, ph, eh->e_phnum * sizeof(Elf64_Phdr));
    }
    m->entry = eh->e_entry;
    free(file);

    if (m->tlsSize)
    {
        m->tlsModid = nextModid++;
        m->tlsOffset = lxTlsReserve(m->tlsSize, m->tlsAlign);
        lxTlsAddImage(m->tlsOffset, m->tlsInit, m->tlsInitSize, m->tlsSize);
    }
    if (m->dyn)
        parseDynamic(m);
    for (Elf64_Dyn *d = m->dyn; d && d->d_tag != DT_NULL; d++)
        if (d->d_tag == DT_SONAME && !isExe)
            snprintf(m->name, sizeof(m->name), "%s", m->strtab + d->d_val);

    if (lastModule)
        lastModule->next = m;
    else
        modules = m;
    lastModule = m;
    lxLog("mapped %s at %p..%p%s\n", m->name, (void *)m->mapStart, (void *)(m->mapStart + m->mapSize),
          m->tlsSize ? " (TLS)" : "");
    return m;
}

// Maps the module's dependencies, breadth first as ld.so does.
static void mapDependencies(LxModule *first)
{
    for (LxModule *m = first; m; m = m->next)
    {
        for (Elf64_Dyn *d = m->dyn; d && d->d_tag != DT_NULL; d++)
        {
            const char *need;
            char path[MAX_PATH];
            LxModule *dep;
            if (d->d_tag != DT_NEEDED)
                continue;
            need = m->strtab + d->d_val;
            if (isBridged(need))
                continue;
            if (!(dep = lxFindModule(need)))
            {
                if (!findLibrary(need, path))
                {
                    lxLog("%s: %s not found, its symbols left to the bridges\n", m->name, need);
                    continue;
                }
                dep = mapModule(need, path, 0);
            }
            if (dep && m->ndeps < MAX_DEPS)
                m->deps[m->ndeps++] = dep;
        }
    }
}

// ---------------------------------------------------------------------------
// Relocation.

static int unresolvedCount;

// Passes: 0 the relocations, 1 IRELATIVE and COPY (once every module's data
// is relocated: a copy takes the defining library's relocated values).
static void relocateOne(LxModule *m, Elf64_Rela *r, int irelative)
{
    uint32_t type = (uint32_t)r->r_info, symi = (uint32_t)(r->r_info >> 32);
    uint64_t *where = (uint64_t *)(m->bias + r->r_offset);
    Elf64_Sym *sym = symi ? &m->symtab[symi] : NULL;
    const char *name = sym ? m->strtab + sym->st_name : NULL;
    const char *version = NULL;
    LxModule *def = NULL;
    Elf64_Sym *defSym = NULL;
    void *value = NULL;

    if (type == R_X86_64_IRELATIVE)
    {
        if (irelative)
            *where = (uint64_t)((SYSV void *(*)(void))(m->bias + r->r_addend))();
        return;
    }
    if (type == R_X86_64_NONE || (irelative != (type == R_X86_64_COPY)))
        return;
    if (type == R_X86_64_RELATIVE)
    {
        *where = (uint64_t)(m->bias + r->r_addend);
        return;
    }
    if (sym && m->versym)
    {
        uint16_t v = m->versym[symi] & 0x7fff;
        if (v >= 2 && v < MAX_VERSIONS)
            version = m->versions[v];
    }
    if (sym && (sym->st_info >> 4) == STB_LOCAL)
    {
        def = m;
        defSym = sym;
        value = m->bias + sym->st_value;
    }
    else if (sym)
    {
        value = lookup(name, version, type == R_X86_64_COPY ? m : NULL, &def, &defSym);
        if (!value && !def && (sym->st_info >> 4) != STB_WEAK && type != R_X86_64_DTPMOD64 &&
            type != R_X86_64_DTPOFF64 && type != R_X86_64_TPOFF64)
        {
            value = lxUnresolvedStub(name);
            unresolvedCount++;
            lxTraceLog("%s: %s%s%s unresolved\n", m->name, name, version ? "@" : "", version ? version : "");
        }
    }

    switch (type)
    {
    case R_X86_64_64:
        *where = (uint64_t)value + r->r_addend;
        break;
    case R_X86_64_GLOB_DAT:
    case R_X86_64_JUMP_SLOT:
        *where = (uint64_t)value;
        break;
    case R_X86_64_PC32:
        *(uint32_t *)where = (uint32_t)((uint64_t)value + r->r_addend - (uint64_t)where);
        break;
    case R_X86_64_32:
    case R_X86_64_32S:
        *(uint32_t *)where = (uint32_t)((uint64_t)value + r->r_addend);
        break;
    case R_X86_64_COPY:
        if (value && sym)
            memcpy(where, value, sym->st_size);
        break;
    case R_X86_64_DTPMOD64:
        *where = def ? (uint64_t)def->tlsModid : (uint64_t)m->tlsModid;
        break;
    case R_X86_64_DTPOFF64:
        *where = (defSym ? defSym->st_value : 0) + r->r_addend;
        break;
    case R_X86_64_TPOFF64:
        *where = (def ? def->tlsOffset : m->tlsOffset) + (defSym ? defSym->st_value : 0) + r->r_addend;
        break;
    default:
        lxLog("%s: relocation type %u at %#llx not handled\n", m->name, type, (unsigned long long)r->r_offset);
    }
}

static void relocateTable(LxModule *m, Elf64_Rela *table, size_t size, int irelative)
{
    for (size_t i = 0; table && i < size / sizeof(Elf64_Rela); i++)
        relocateOne(m, &table[i], irelative);
}

static void relocateRelr(LxModule *m)
{
    uint64_t *where = NULL;
    for (size_t i = 0; m->relr && i < m->relrsz / 8; i++)
    {
        uint64_t e = m->relr[i];
        if (!(e & 1))
        {
            where = (uint64_t *)(m->bias + e);
            *where++ += (uint64_t)m->bias;
            continue;
        }
        for (int b = 0; (e >>= 1); b++)
            if (e & 1)
                where[b] += (uint64_t)m->bias;
        where += 63;
    }
}

static void relocate(LxModule *m)
{
    relocateRelr(m);
    relocateTable(m, m->rela, m->relasz, 0);
    relocateTable(m, m->jmprel, m->jmprelsz, 0);
}

static void patchModule(LxModule *m)
{
    size_t n = 0;
    for (int i = 0; i < m->phnum; i++)
        if (m->phdr[i].p_type == PT_LOAD && (m->phdr[i].p_flags & PF_X))
            n += lxPatchFs(m->bias + m->phdr[i].p_vaddr, m->phdr[i].p_memsz);
    lxTraceLog("%s: %zu stack protector reads moved to the TEB\n", m->name, n);
}

typedef SYSV void (*InitFn)(int, char **, char **);
static int initArgc;
static char **initArgv, **initEnvp;

static void runInit(LxModule *m)
{
    if (m->initDone)
        return;
    m->initDone = 1;
    for (int i = 0; i < m->ndeps; i++)
        runInit(m->deps[i]);
    if (m->isExe)
        return;   // its own, from __libc_start_main
    lxTraceLog("init %s\n", m->name);
    if (m->init)
        ((InitFn)(m->bias + m->init))(initArgc, initArgv, initEnvp);
    for (size_t i = 0; i < m->initArraySz; i++)
        if (m->initArray[i] && m->initArray[i] != (uint64_t)-1)
            ((InitFn)m->initArray[i])(initArgc, initArgv, initEnvp);
}

// Relocates the modules from first on, patches them, and runs their
// constructors (but the executable's).
static void finishLoad(LxModule *first)
{
    unresolvedCount = 0;
    for (LxModule *m = first; m; m = m->next)
        relocate(m);
    for (LxModule *m = first; m; m = m->next)
    {
        relocateTable(m, m->rela, m->relasz, 1);
        relocateTable(m, m->jmprel, m->jmprelsz, 1);
        patchModule(m);
    }
    lxTlsThreadInit();
    if (unresolvedCount)
        lxLog("%d symbols unresolved (LL64_TRACE=1 lists them)\n", unresolvedCount);
}

LxModule *lxLoadExecutable(const char *path)
{
    const char *base = strrchr(path, '\\');
    LxModule *exe = mapModule(base ? base + 1 : path, path, 1);
    if (!exe)
        return NULL;
    mapDependencies(exe);
    finishLoad(exe);
    return exe;
}

LxModule *lxLoadLibrary(const char *name)
{
    char path[MAX_PATH];
    LxModule *m = lxFindModule(name), *first;
    if (m)
        return m;
    if (strchr(name, '/') || strchr(name, '\\'))
    {
        const char *b = strrchr(name, '/');
        snprintf(path, sizeof(path), "%s", name);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
            return NULL;
        name = b ? b + 1 : name;
        if ((m = lxFindModule(name)))
            return m;
    }
    else if (!findLibrary(name, path))
        return NULL;
    if (!(first = mapModule(name, path, 0)))
        return NULL;
    mapDependencies(first);
    finishLoad(first);
    runInit(first);
    return first;
}

int lxIteratePhdr(LxPhdrCallback cb, void *data)
{
    struct
    {
        uint64_t addr;
        const char *name;
        const Elf64_Phdr *phdr;
        uint16_t phnum;
        unsigned long long adds, subs;
        size_t tlsModid;
        void *tlsData;
    } info;
    int r = 0;
    for (LxModule *m = modules; m && !r; m = m->next)
    {
        memset(&info, 0, sizeof(info));
        info.addr = (uint64_t)m->bias;
        info.name = m->isExe ? "" : m->path;
        info.phdr = m->phdr;
        info.phnum = (uint16_t)m->phnum;
        info.adds = nextModid;
        info.tlsModid = m->tlsModid;
        info.tlsData = m->tlsSize ? lxTlsAddress(m->tlsOffset) : NULL;
        r = cb(&info, sizeof(info), data);
    }
    return r;
}

int lxFindObject(void *pc, void **mapStart, void **mapEnd, void **ehFrame, LxModule **mod)
{
    LxModule *m = lxModuleAt(pc);
    if (!m || !m->ehFrameHdr)
        return -1;
    *mapStart = m->mapStart;
    *mapEnd = m->mapStart + m->mapSize;
    *ehFrame = m->ehFrameHdr;
    *mod = m;
    return 0;
}

void *lxTlsModuleAddress(size_t modid, size_t offset)
{
    for (LxModule *m = modules; m; m = m->next)
        if (m->tlsModid == (int)modid)
            return (uint8_t *)lxTlsAddress(m->tlsOffset) + offset;
    lxLog("__tls_get_addr: no module %zu\n", modid);
    return NULL;
}

// The entry point, on a stack laid out as the kernel does: argc, argv, envp,
// the auxiliary vector.
extern void lxEnterEntry(void *entry, void *sp) __attribute__((noreturn));
__asm__(".globl lxEnterEntry\n"
        "lxEnterEntry:\n"
        "    mov %rdx, %rsp\n"
        "    xor %ebp, %ebp\n"
        "    xor %edx, %edx\n"
        "    jmp *%rcx\n");

void lxRunExecutable(LxModule *exe, int argc, char **argv, char **envp)
{
    int envc = 0;
    static uint8_t randomBytes[16];
    uint64_t *sp, *p;
    size_t words;

    initArgc = argc;
    initArgv = argv;
    initEnvp = envp;
    runInit(exe);
    while (envp[envc])
        envc++;
    words = 1 + argc + 1 + envc + 1 + 12;
    words = (words + 1) & ~(size_t)1;
    sp = (uint64_t *)(((uintptr_t)__builtin_alloca(words * 8 + 64) + 15) & ~(uintptr_t)15);
    p = sp;
    *p++ = (uint64_t)argc;
    for (int i = 0; i < argc; i++)
        *p++ = (uint64_t)argv[i];
    *p++ = 0;
    for (int i = 0; i < envc; i++)
        *p++ = (uint64_t)envp[i];
    *p++ = 0;
    for (int i = 0; i < 16; i++)
        randomBytes[i] = (uint8_t)(rand() ^ (i * 37));
    *p++ = 3; *p++ = (uint64_t)exe->phdr;   // AT_PHDR
    *p++ = 5; *p++ = (uint64_t)exe->phnum;  // AT_PHNUM
    *p++ = 6; *p++ = 4096;                  // AT_PAGESZ
    *p++ = 9; *p++ = (uint64_t)(exe->bias + exe->entry);   // AT_ENTRY
    *p++ = 25; *p++ = (uint64_t)randomBytes; // AT_RANDOM
    *p++ = 0; *p++ = 0;
    lxLog("entering %s at %p\n", exe->name, (void *)(exe->bias + exe->entry));
    lxEnterEntry(exe->bias + exe->entry, sp);
}

// The executable's constructors, for a __libc_start_main given none.
void lxRunExecutableInit(LxModule *exe)
{
    if (exe->init)
        ((InitFn)(exe->bias + exe->init))(initArgc, initArgv, initEnvp);
    for (size_t i = 0; i < exe->initArraySz; i++)
        if (exe->initArray[i] && exe->initArray[i] != (uint64_t)-1)
            ((InitFn)exe->initArray[i])(initArgc, initArgv, initEnvp);
}

// The libraries' destructors, at exit (newest first).
void lxRunFini(void)
{
    LxModule *list[64];
    int n = 0;
    for (LxModule *m = modules; m && n < 64; m = m->next)
        if (!m->isExe && m->initDone)
            list[n++] = m;
    while (n--)
    {
        LxModule *m = list[n];
        for (size_t i = m->finiArraySz; i-- > 0;)
            if (m->finiArray[i] && m->finiArray[i] != (uint64_t)-1)
                ((SYSV void (*)(void))m->finiArray[i])();
        if (m->fini)
            ((SYSV void (*)(void))(m->bias + m->fini))();
    }
}
