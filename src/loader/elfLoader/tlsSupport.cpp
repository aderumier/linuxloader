#if defined(_WIN32) || defined(__MINGW32__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include "tlsSupport.hpp"
#include "../log/log.h"

namespace
{
    // i386 (psABI, as the kernel exec layout and glibc use it): the TCB
    // is 0x70 bytes at the start of the TLS block (self at +0, the DTV
    // pointer at +4), the executable's TLS follows at +0x70.
    const uint32_t TCB_SIZE = 0x70;
    const uint32_t TCB_ALIGN = 16;
    const uint32_t DTV_SURPLUS = 16;

    // DTV layout, as glibc's (elf/dl-tls.c): the array the TCB points
    // at (call it dtv[0]) holds the generation counter; one slot before
    // it holds the capacity; dtv[1..] hold the modules' block pointers.
    struct DtvEntry
    {
        uint32_t counter;
        uint32_t value;
    };

    struct Module
    {
        uint32_t modid;
        uint32_t tpVaddr;
        uint32_t filesz;
        uint32_t memsz;
        uint32_t align;
        const uint8_t *initImage; // mapped p_filesz bytes (or null)
        uint32_t tlsOffset;       // block start, relative to the TCB
        bool isMain;
        char name[256];
    };

    struct Symbol
    {
        std::string name;
        uint32_t modid;
        uint32_t offset;
    };

    std::vector<Module> g_modules;
    std::vector<Symbol> g_symbols;
    CRITICAL_SECTION g_lock;
    bool g_lockInit = false;
    uint32_t g_nextModid = 2; // the main executable takes 1
    uint32_t g_staticUsed = TCB_SIZE; // running end of the static layout
    DWORD g_tlsSlot = TLS_OUT_OF_INDEXES;

    void Lock()
    {
        if (!g_lockInit)
        {
            InitializeCriticalSection(&g_lock);
            g_lockInit = true;
        }
        EnterCriticalSection(&g_lock);
    }

    void Unlock()
    {
        if (g_lockInit)
            LeaveCriticalSection(&g_lock);
    }

    DWORD Slot()
    {
        if (g_tlsSlot == TLS_OUT_OF_INDEXES)
        {
            Lock();
            if (g_tlsSlot == TLS_OUT_OF_INDEXES)
            {
                g_tlsSlot = TlsAlloc();
                if (g_tlsSlot == TLS_OUT_OF_INDEXES)
                    log_error("TLS: TlsAlloc failed");
            }
            Unlock();
        }
        return g_tlsSlot;
    }

    uint32_t Roundup(uint32_t value, uint32_t align)
    {
        return align > 1 ? (value + align - 1) & ~(align - 1) : value;
    }

    const Module *FindModuleByModid(uint32_t modid)
    {
        for (const Module &m : g_modules)
            if (m.modid == modid)
                return &m;
        return nullptr;
    }

    // A static zeroed TCB + a DTV: the fallback when the real block
    // cannot be allocated, so __tls_get_addr never returns an unusable
    // pointer. The capacity is large enough that GetAddr never resizes
    // it; late modules still get their private blocks via the normal
    // path. Never stored in a TLS slot (ReleaseThreadTls only frees
    // what TlsGetValue returns, so this is never free()d).
    void *DummyTcb()
    {
        static uint8_t block[TCB_SIZE];
        static DtvEntry dtv[65]; // slots -1 .. 64
        static bool init = false;
        DtvEntry *d = dtv + 1;
        if (!init)
        {
            memset(block, 0, sizeof(block));
            memset(dtv, 0, sizeof(dtv));
            d[-1].counter = 64;
            d[0].counter = 1;
            d[1].value = (uint32_t)(block + TCB_SIZE);
            *(uint32_t *)(block + 0) = (uint32_t)block;
            *(uint32_t *)(block + 4) = (uint32_t)d;
            init = true;
        }
        return (void *)block;
    }

    // A private (dynamically allocated) copy of a module's TLS block for
    // a thread whose static block predates the module (a late dlopen),
    // as glibc does in tls_get_addr_tail.
    uint32_t AllocModuleBlock(const Module &m)
    {
        uint32_t align = m.align > 1 ? m.align : 1;
        void *raw = malloc((size_t)m.memsz + align);
        if (!raw)
            return 0;
        // The block base must satisfy the same congruence as the static
        // layout (dest = p_vaddr mod p_align), so offset it by that
        // residue (as glibc's firstbyte adjustment does).
        uint32_t r = (align > 1) ? (m.tpVaddr & (align - 1)) : 0;
        uint8_t *p = (uint8_t *)(((uintptr_t)raw + align - 1) & ~(uintptr_t)(align - 1)) + r;
        memset(p, 0, m.memsz);
        if (m.initImage && m.filesz)
            memcpy(p, m.initImage, m.filesz);
        return (uint32_t)p;
    }
}

uint32_t Tls::RegisterModule(const char *name, bool isMain, uint32_t tpVaddr, uint32_t filesz, uint32_t memsz, uint32_t align,
                             const uint8_t *initImage)
{
    Lock();
    uint32_t modid = 0;
    if (isMain)
    {
        modid = 1; // the executable always takes DTV index 1
    }
    else if (memsz > 0)
    {
        modid = g_nextModid++;
    }

    if (modid)
    {
        // glibc's _dl_determine_tlsoffset (the TLS_DTV_AT_TP branch,
        // used for the i386 exec layout).
        uint32_t firstbyte = 0;
        if (align > 1)
        {
            uint32_t r = tpVaddr & (align - 1);
            firstbyte = (-r) & (align - 1);
        }
        uint32_t off = Roundup(g_staticUsed, align);
        if (align > 1 && off - g_staticUsed < firstbyte)
            off += align;

        Module m = {};
        m.modid = modid;
        m.tpVaddr = tpVaddr;
        m.filesz = filesz;
        m.memsz = memsz;
        m.align = align;
        m.initImage = initImage;
        m.tlsOffset = off - firstbyte;
        m.isMain = isMain;
        snprintf(m.name, sizeof(m.name), "%s", name ? name : "?");
        g_staticUsed = off + memsz - firstbyte;
        g_modules.push_back(m);
        log_info("TLS: registered %s as module %u (offset 0x%x, size 0x%x, align 0x%x)", m.name, modid, m.tlsOffset, memsz, align);
    }
    Unlock();
    return modid;
}

void Tls::RegisterSymbol(const char *name, uint32_t modid, uint32_t offset)
{
    if (!name || !modid)
        return;
    Lock();
    for (const Symbol &s : g_symbols)
        if (s.name == name)
        {
            Unlock();
            return; // first definition wins
        }
    g_symbols.push_back({name, modid, offset});
    Unlock();
}

uint32_t Tls::FindSymbolModule(const char *name)
{
    if (!name)
        return 0;
    Lock();
    uint32_t modid = 0;
    for (const Symbol &s : g_symbols)
        if (s.name == name)
        {
            modid = s.modid;
            break;
        }
    Unlock();
    return modid;
}

uint32_t Tls::FindSymbolTlsOffset(const char *name)
{
    if (!name)
        return 0;
    Lock();
    uint32_t offset = 0;
    for (const Symbol &s : g_symbols)
        if (s.name == name)
        {
            offset = s.offset;
            break;
        }
    Unlock();
    return offset;
}

uint32_t Tls::GetModuleTlsOffset(uint32_t modid)
{
    Lock();
    uint32_t offset = 0;
    const Module *m = FindModuleByModid(modid);
    if (m)
        offset = m->tlsOffset;
    Unlock();
    return offset;
}

void *Tls::EnsureThreadTls()
{
    void *existing = TlsGetValue(Slot());
    if (existing)
        return existing;

    std::vector<Module> modules;
    uint32_t staticUsed;
    Lock();
    modules = g_modules;
    staticUsed = g_staticUsed;
    uint32_t maxModid = g_nextModid - 1;
    Unlock();

    uint32_t capacity = maxModid + DTV_SURPLUS;
    if (capacity < 2)
        capacity = 2;

    // The block base must satisfy every module's alignment (the layout
    // is computed relative to it, as glibc's max_align), so align it to
    // the largest one.
    uint32_t blockAlign = TCB_ALIGN;
    for (const Module &m : modules)
        if (m.align > 1 && m.align > blockAlign)
            blockAlign = m.align;

    void *raw = malloc(staticUsed + blockAlign);
    DtvEntry *dtv = (DtvEntry *)calloc(capacity + 2, sizeof(DtvEntry));
    if (!raw || !dtv)
    {
        free(raw);
        free(dtv);
        log_error("TLS: thread block allocation failed, using the fallback TCB");
        return DummyTcb();
    }

    uint8_t *block = (uint8_t *)(((uintptr_t)raw + blockAlign - 1) & ~(uintptr_t)(blockAlign - 1));
    memset(block, 0, staticUsed);

    // The TCB points at the logical DTV entry for index 0 (the generation
    // counter); one slot before it holds the capacity, and index 1.. hold
    // the modules' block pointers. dtv is the allocation base, so the
    // logical pointer is d = dtv + 1.
    DtvEntry *d = dtv + 1;
    d[-1].counter = capacity;
    d[0].counter = 1;

    for (const Module &m : modules)
    {
        if (m.modid > capacity || m.memsz == 0)
        {
            if (m.isMain)
                d[m.modid].value = (uint32_t)(block + TCB_SIZE);
            continue;
        }
        uint8_t *dest = block + m.tlsOffset;
        if (m.initImage && m.filesz)
            memcpy(dest, m.initImage, m.filesz);
        d[m.modid].value = (uint32_t)dest;
    }

    // TCB: self at +0, the DTV (index 0) at +4, the malloc base at +8
    // (the block is aligned inside the allocation; free() wants the base
    // at thread exit).
    *(uint32_t *)(block + 0) = (uint32_t)block;
    *(uint32_t *)(block + 4) = (uint32_t)d;
    *(uint32_t *)(block + 8) = (uint32_t)raw;

    TlsSetValue(Slot(), block);
    log_info("TLS: thread %lu TCB at 0x%08X (static block 0x%x, %zu modules, DTV capacity %u)",
             GetCurrentThreadId(), (uint32_t)(uintptr_t)block, staticUsed, modules.size(), capacity);
    return block;
}

void Tls::ReleaseThreadTls()
{
    void *tcb = TlsGetValue(Slot());
    if (!tcb)
        return;
    DtvEntry *dtv = *(DtvEntry **)((uint8_t *)tcb + 4);
    void *raw = (void *)*(uint32_t *)((uint8_t *)tcb + 8); // malloc base (see EnsureThreadTls)
    if (dtv && dtv[-1].counter < 0x100000) // sanity: capacity is small for a real DTV
        free(dtv - 1);
    // The late-dlopen module blocks (malloc'ed in GetAddr) leak until
    // process exit: the game is done, so they are not worth tracking.
    if (raw)
        free(raw);
    TlsSetValue(Slot(), NULL);
}

void *Tls::GetAddr(uint32_t modid, uint32_t offset)
{
    uint8_t *tcb = (uint8_t *)EnsureThreadTls();
    if (!tcb)
        return (void *)0;

    if (modid == 0)
    {
        // The pair was never filled (a broken/unused GOT slot): glibc
        // would read the generation counter as a pointer. Return the TCB
        // instead - harmless, never crashes.
        return (void *)tcb;
    }

    DtvEntry *dtv = *(DtvEntry **)(tcb + 4);

    // A module registered after this thread's block was made: grow the
    // DTV (glibc's _dl_resize_dtv). Slots 1..capacity are valid, so only
    // modid > capacity needs room.
    while (modid > dtv[-1].counter)
    {
        uint32_t oldcap = dtv[-1].counter;
        uint32_t newcap = oldcap + DTV_SURPLUS;
        if (newcap <= modid)
            newcap = modid + DTV_SURPLUS;
        // dtv is the logical pointer (base + 1); realloc the base.
        DtvEntry *nd = (DtvEntry *)realloc(dtv - 1, (size_t)(newcap + 2) * sizeof(DtvEntry));
        if (!nd)
            return (void *)tcb;
        memset(nd + 2 + oldcap, 0, (size_t)(newcap - oldcap) * sizeof(DtvEntry));
        nd[0].counter = newcap; // capacity lives at base[0] = logical[-1]
        dtv = nd + 1;
        *(DtvEntry **)(tcb + 4) = dtv;
    }

    DtvEntry &entry = dtv[modid];
    if (entry.value == 0)
    {
        // A late dlopen: allocate this module's block privately.
        Lock();
        const Module *m = FindModuleByModid(modid);
        Module copy = {};
        if (m)
            copy = *m;
        Unlock();

        if (!m)
        {
            static bool logged = false;
            if (!logged)
            {
                log_warn("TLS: __tls_get_addr with unknown module %u, returning the TCB", modid);
                logged = true;
            }
            return (void *)tcb;
        }
        entry.value = AllocModuleBlock(copy);
        if (!entry.value)
            return (void *)tcb;
    }

    return (void *)((uintptr_t)entry.value + offset);
}

namespace
{
    // The i386 GD/LD pair (glibc's tls_index): a GOT slot holding
    // {ti_module, ti_offset}, passed as a pointer in %eax (the GNU
    // regparm form) or on the stack (the "Sun" form).
    struct TlsIndex
    {
        uint32_t ti_module;
        uint32_t ti_offset;
    };
}

extern "C" __attribute__((regparm(1))) void *Tls::___tls_get_addr(void *ti)
{
    TlsIndex *pair = (TlsIndex *)ti;
    if (!pair)
        return GetAddr(0, 0);
    return GetAddr(pair->ti_module, pair->ti_offset);
}

extern "C" void *Tls::__tls_get_addr(void *ti)
{
    TlsIndex *pair = (TlsIndex *)ti;
    if (!pair)
        return GetAddr(0, 0);
    return GetAddr(pair->ti_module, pair->ti_offset);
}

#endif
