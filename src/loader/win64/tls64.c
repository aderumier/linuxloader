// Static TLS (see ll64.h). Linux x86-64 code finds its thread through %fs,
// which Windows does not let a process set: the TLS of the modules lives in a
// run of the TEB's own TLS slots instead (gs:0x1480 + 8 * slot), every thread
// having its TEB. TPOFF64 relocations are TEB offsets there, __tls_get_addr
// hands their address, and the stack protector's %fs:0x28 reads become %gs
// reads of the first slot, the canary (0 in every thread: it only has to be
// the same at a function's entry and exit).
#include <windows.h>
#include <string.h>
#include "ll64.h"

#define TEB_TLS_SLOTS 0x1480
#define WANTED_SLOTS 40

static size_t slotsStart, slotsEnd, used;

#define MAX_IMAGES 16
static struct
{
    size_t offset, initSize, size;
    const void *init;
} images[MAX_IMAGES];
static int nImages;

int lxTlsInit(void)
{
    DWORD first = TLS_OUT_OF_INDEXES, last = 0;
    // TlsAlloc hands the lowest free slot: take them until a run long enough
    // (within the TEB's 64), the rest left taken.
    for (int tries = 0; tries < 64; tries++)
    {
        DWORD i = TlsAlloc();
        if (i == TLS_OUT_OF_INDEXES || i >= 64)
            break;
        if (first == TLS_OUT_OF_INDEXES || i != last + 1)
            first = i;
        last = i;
        if (last - first + 1 >= WANTED_SLOTS)
            break;
    }
    if (first == TLS_OUT_OF_INDEXES || last - first + 1 < 8)
    {
        lxLog("no TLS slots for the Linux TLS\n");
        return 0;
    }
    slotsStart = TEB_TLS_SLOTS + 8 * first;
    slotsEnd = TEB_TLS_SLOTS + 8 * (last + 1);
    used = 8;   // the canary
    lxTraceLog("TLS in TEB slots %lu..%lu\n", first, last);
    return 1;
}

size_t lxTlsReserve(size_t size, size_t align)
{
    size_t off = (slotsStart + used + align - 1) & ~(align - 1);
    if (off + size > slotsEnd)
    {
        lxLog("out of TLS slots for %zu bytes of TLS\n", size);
        return slotsStart;
    }
    used = off + size - slotsStart;
    return off;
}

void lxTlsAddImage(size_t offset, const void *init, size_t initSize, size_t size)
{
    if (nImages < MAX_IMAGES)
    {
        images[nImages].offset = offset;
        images[nImages].init = init;
        images[nImages].initSize = initSize;
        images[nImages].size = size;
        nImages++;
    }
}

void *lxTlsAddress(size_t offset)
{
    return (uint8_t *)NtCurrentTeb() + offset;
}

void lxTlsThreadInit(void)
{
    uint8_t *teb = (uint8_t *)NtCurrentTeb();
    memset(teb + slotsStart, 0, 8);
    for (int i = 0; i < nImages; i++)
    {
        memcpy(teb + images[i].offset, images[i].init, images[i].initSize);
        memset(teb + images[i].offset + images[i].initSize, 0, images[i].size - images[i].initSize);
    }
}

// fs-prefixed reads of an absolute offset: 64 [REX.W] op modrm(00 reg 100)
// sib(0x25) disp32. %fs:0x28 is the canary, %fs:0 the thread pointer (the
// TEB's own address is at gs:0x30).
size_t lxPatchFs(uint8_t *start, size_t size)
{
    static const uint8_t ops[] = {0x8b, 0x33, 0x2b, 0x3b, 0x39, 0x31, 0x29, 0x03};
    size_t n = 0;
    for (size_t i = 0; i + 9 <= size; i++)
    {
        uint8_t *p = start + i, *q;
        uint32_t disp;
        int ok = 0;
        if (p[0] != 0x64)
            continue;
        q = p + 1;
        if ((q[0] & 0xf0) == 0x40)
            q++;
        for (size_t k = 0; k < sizeof(ops); k++)
            ok |= q[0] == ops[k];
        if (!ok || (q[1] & 0xc7) != 0x04 || q[2] != 0x25)
            continue;
        memcpy(&disp, q + 3, 4);
        if (disp == 0x28)
            disp = (uint32_t)slotsStart;
        else if (disp == 0)
            disp = 0x30;
        else
            continue;
        p[0] = 0x65;
        memcpy(q + 3, &disp, 4);
        n++;
        i += (size_t)(q + 6 - p);
    }
    return n;
}

void *lxTlsModuleAddress(size_t modid, size_t offset);

SYSV void *lxTlsGetAddr(size_t *index)
{
    return lxTlsModuleAddress(index[0], index[1]);
}

static const LxSym syms[] = {
    {"__tls_get_addr", lxTlsGetAddr},
};

void lxTlsRegister(void)
{
    lxRegister(syms, LX_COUNT(syms));
}
