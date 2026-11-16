// Diagnostics (see ll64.h): faults logged with where they happened in the
// Linux modules, and LL64_WATCH=<seconds>: every thread's place and the
// module return addresses on its stack, that often (a hang's picture).
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include "ll64.h"

static void describe(const char *what, uint64_t addr)
{
    LxModule *m = lxModuleAt((void *)addr);
    const char *sname = NULL;
    void *saddr = NULL;
    if (!m)
    {
        HMODULE h = NULL;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)addr, &h))
        {
            GetModuleFileNameA(h, name, sizeof(name));
            lxLog("  %s %#llx (%s+%#llx)\n", what, (unsigned long long)addr, strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name,
                  (unsigned long long)(addr - (uint64_t)h));
        }
        else
            lxLog("  %s %#llx\n", what, (unsigned long long)addr);
        return;
    }
    lxModuleAddr((void *)addr, &sname, &saddr);
    lxLog("  %s %#llx (%s+%#llx%s%s%s)\n", what, (unsigned long long)addr, lxModuleName(m),
          (unsigned long long)(addr - (uint64_t)lxModuleBase(m)), sname ? ", " : "", sname ? sname : "",
          sname ? "" : "");
}

static void stackScan(uint64_t rsp, int words)
{
    MEMORY_BASIC_INFORMATION mbi;
    int shown = 0;
    for (int i = 0; i < words && shown < 16; i++)
    {
        uint64_t *p = (uint64_t *)rsp + i, v;
        if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT)
            break;
        v = *p;
        if (lxModuleAt((void *)v))
        {
            char what[32];
            snprintf(what, sizeof(what), "[rsp+%#x]", i * 8);
            describe(what, v);
            shown++;
        }
    }
}

static LONG CALLBACK faultHandler(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    CONTEXT *c = ep->ContextRecord;
    static LONG reported;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&reported) > 3)
        return EXCEPTION_CONTINUE_SEARCH;
    lxLogEnabled = 1;   // a crash is always reported
    lxLog("fault %#lx in thread %lu%s%#llx\n", code, GetCurrentThreadId(),
          code == EXCEPTION_ACCESS_VIOLATION ? (ep->ExceptionRecord->ExceptionInformation[0] ? ", writing " : ", reading ") : ", ",
          (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    describe("rip", c->Rip);
    lxLog("  rax %#llx rbx %#llx rcx %#llx rdx %#llx rsi %#llx rdi %#llx rbp %#llx rsp %#llx\n", c->Rax, c->Rbx,
          c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rbp, c->Rsp);
    lxLog("  r8 %#llx r9 %#llx r12 %#llx r13 %#llx r14 %#llx r15 %#llx\n", c->R8, c->R9, c->R12, c->R13, c->R14,
          c->R15);
    stackScan(c->Rsp, 512);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI watchdog(void *arg)
{
    DWORD every = (DWORD)(uintptr_t)arg, self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (;;)
    {
        HANDLE snap;
        THREADENTRY32 te = {sizeof(te)};
        Sleep(every * 1000);
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        lxLog("watchdog: the threads\n");
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
        {
            HANDLE t;
            CONTEXT c = {.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER};
            if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
                continue;
            if (!(t = OpenThread(THREAD_ALL_ACCESS, FALSE, te.th32ThreadID)))
                continue;
            SuspendThread(t);
            if (GetThreadContext(t, &c))
            {
                lxLog(" thread %lu:\n", te.th32ThreadID);
                describe("rip", c.Rip);
                stackScan(c.Rsp, 2048);
            }
            ResumeThread(t);
            CloseHandle(t);
        }
        CloseHandle(snap);
    }
    return 0;
}

// LL64_SAMPLE=<seconds>: from then on, the game thread's module return
// addresses, 40 times every 250 ms.
DWORD lxGameThreadId;
static DWORD WINAPI sampler(void *arg)
{
    HANDLE t;
    Sleep((DWORD)(uintptr_t)arg * 1000);
    if (!(t = OpenThread(THREAD_ALL_ACCESS, FALSE, lxGameThreadId)))
        return 0;
    for (int n = 0; n < 40; n++)
    {
        CONTEXT c = {.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER};
        char line[1024];
        size_t o = 0;
        SuspendThread(t);
        if (GetThreadContext(t, &c))
        {
            LxModule *m = lxModuleAt((void *)c.Rip);
            o += (size_t)snprintf(line + o, sizeof(line) - o, "%s%#llx", m ? "" : "*", (unsigned long long)c.Rip);
            for (int i = 0; i < 1500 && o + 24 < sizeof(line); i++)
            {
                uint64_t v = ((uint64_t *)c.Rsp)[i];
                if (lxModuleAt((void *)v) && v < 0x2000000)
                    o += (size_t)snprintf(line + o, sizeof(line) - o, " %#llx", (unsigned long long)v);
            }
        }
        ResumeThread(t);
        lxLog("sample: %s\n", line);
        Sleep(250);
    }
    return 0;
}

void lxDiagInit(void)
{
    const char *w;
    AddVectoredExceptionHandler(1, faultHandler);
    if ((w = getenv("LL64_SAMPLE")) && atoi(w) > 0)
        CloseHandle(CreateThread(NULL, 0, sampler, (void *)(uintptr_t)atoi(w), 0, NULL));
    w = getenv("LL64_WATCH");
    if (w && atoi(w) > 0)
        CloseHandle(CreateThread(NULL, 0, watchdog, (void *)(uintptr_t)atoi(w), 0, NULL));
}
