// The bridges' symbol table, the stubs of the symbols nothing defines, and the
// log (see ll64.h).
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ll64.h"

int lxTrace;
// Off once the settings say so ([System] DEBUG_MSGS, see main64.c).
int lxLogEnabled = 1;

void lxLog(const char *fmt, ...)
{
    static CRITICAL_SECTION lock;
    static LONG init;
    va_list ap;
    if (!lxLogEnabled)
        return;
    if (!InterlockedExchange(&init, 1))
        InitializeCriticalSection(&lock);
    static ULONGLONG start;
    EnterCriticalSection(&lock);
    if (!start)
        start = GetTickCount64();
    fprintf(stderr, "ll64 %6.2f: ", (double)(GetTickCount64() - start) / 1000.0);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
    LeaveCriticalSection(&lock);
}

#define MAX_SYMS 8192
static struct
{
    const char *name;
    void *fn;
    size_t order;
} table[MAX_SYMS];
static size_t count;
static int sorted;

void lxRegister(const LxSym *syms, size_t n)
{
    for (size_t i = 0; i < n && count < MAX_SYMS; i++)
    {
        table[count].name = syms[i].name;
        table[count].fn = syms[i].fn;
        table[count].order = count;
        count++;
    }
    sorted = 0;
}

// Later registrations win: sorted after the earlier ones.
static int bySymName(const void *a, const void *b)
{
    const typeof(table[0]) *x = a, *y = b;
    int r = strcmp(x->name, y->name);
    return r ? r : x->order < y->order ? -1 : 1;
}

void *lxBridge(const char *name)
{
    size_t lo = 0, hi;
    if (!sorted)
    {
        qsort(table, count, sizeof(table[0]), bySymName);
        sorted = 1;
    }
    hi = count;
    while (lo < hi)
    {
        size_t mid = (lo + hi) / 2;
        int r = strcmp(table[mid].name, name);
        if (r <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    // lo: past the last entry <= name.
    return lo && !strcmp(table[lo - 1].name, name) ? table[lo - 1].fn : NULL;
}

// Each stub: sub $40,%rsp; mov $name,%rcx; mov $hit,%rax; call *%rax;
// add $40,%rsp; ret (the shadow space a Windows function may write).
static void unresolvedHit(const char *name)
{
    lxLog("unresolved %s called, returns 0\n", name);
}

void *lxUnresolvedStub(const char *name)
{
    static uint8_t *page;
    static size_t used;
    uint8_t *p;
    uint64_t a;
    char *copy;

    if (!page || used + 48 > 0x10000)
    {
        page = VirtualAlloc(NULL, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        used = 0;
    }
    p = page + used;
    used += 48;
    copy = _strdup(name);
    p[0] = 0x48; p[1] = 0x83; p[2] = 0xec; p[3] = 0x28;   // sub $40,%rsp
    p[4] = 0x48; p[5] = 0xb9;                             // movabs $name,%rcx
    a = (uint64_t)copy;
    memcpy(p + 6, &a, 8);
    p[14] = 0x48; p[15] = 0xb8;                           // movabs $hit,%rax
    a = (uint64_t)unresolvedHit;
    memcpy(p + 16, &a, 8);
    p[24] = 0xff; p[25] = 0xd0;                           // call *%rax
    p[26] = 0x48; p[27] = 0x83; p[28] = 0xc4; p[29] = 0x28; // add $40,%rsp
    p[30] = 0x31; p[31] = 0xc0;                           // xor %eax,%eax
    p[32] = 0xc3;                                         // ret
    return p;
}

void lxTlsRegister(void);
void lxUcontextInit(void);
void lxAlsaInit(void);

void lxBridgesInit(void)
{
    lxTlsRegister();
    lxLibcInit();
    lxStdioInit();
    lxMathInit();
    lxPthreadInit();
    lxDlInit();
    lxMiscInit();
    lxUcontextInit();
    lxX11Init();
    lxGlInit();
    lxAlsaInit();
}
