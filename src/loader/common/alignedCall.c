// Calls from a game built for a 4-byte-aligned stack (Tux Racer and Dead
// Heat, built before the i386 ABI required 16-byte alignment) into
// libraries built for it: the libraries keep SSE/AVX values on their stack
// with aligned moves, which fault on the game's stack (libvorbisfile's in
// the music's Mix_LoadMUS, std::istream::seekg in Dead Heat's first CSV
// read). Each of the game's imports goes through a stub that aligns the
// stack, copies the arguments over and calls the real function.

#include <stdint.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#endif

#include "alignedCall.h"

// The stubs jump here with the real function's address pushed above the
// caller's return address. The first 64 words of arguments are copied to a
// 16-byte-aligned block for the call: as many as a variadic list may hold
// (Tux Racer's Tcl_VarEval builds commands of more than 16 strings, the
// NULL ending them lost when fewer were copied). A function may pop some of them
// itself (a struct returned through a hidden pointer: "ret $4"); as many
// are dropped on the way back. eax/edx (the return value) and the callee-
// saved registers are left as the function returns them. The CFI lets C++
// exceptions unwind through it.
#if defined(_WIN32)
// The PE assembler's symbol declaration (the ELF's .type is ignored by it),
// and the C naming convention's leading underscore on the label.
#define ALIGNED_CALL_HEAD ".def _alignedCallTrampoline; .scl 2; .type 32; .endef\n" \
                          "_alignedCallTrampoline:\n"
#else
#define ALIGNED_CALL_HEAD ".type alignedCallTrampoline, @function\n" \
                          "alignedCallTrampoline:\n"
#endif
__asm__(".text\n"
        ALIGNED_CALL_HEAD
        "    .cfi_startproc\n"
        "    .cfi_def_cfa %esp, 8\n"
        "    .cfi_offset %eip, -4\n"
        "    push %ebp\n"
        "    .cfi_def_cfa_offset 12\n"
        "    .cfi_offset %ebp, -12\n"
        "    mov %esp, %ebp\n"
        "    .cfi_def_cfa_register %ebp\n"
        "    sub $256, %esp\n"
        "    and $-16, %esp\n"
        "    mov $64, %ecx\n"
        "1:  mov 8(%ebp,%ecx,4), %eax\n"
        "    mov %eax, -4(%esp,%ecx,4)\n"
        "    dec %ecx\n"
        "    jnz 1b\n"
        "    call *4(%ebp)\n"
        // ecx: the bytes the function popped
        "    lea -256(%ebp), %ecx\n"
        "    and $-16, %ecx\n"
        "    neg %ecx\n"
        "    add %esp, %ecx\n"
        "    mov %ebp, %esp\n"
        "    pop %ebp\n"
        "    .cfi_def_cfa %esp, 8\n"
        // the return address moved over the popped arguments, the stub's
        // slot and those dropped
        "    pushl 4(%esp)\n"
        "    popl 4(%esp,%ecx,1)\n"
        "    lea 4(%esp,%ecx,1), %esp\n"
        "    .cfi_def_cfa_offset 4\n"
        "    ret\n"
        "    .cfi_endproc\n"
#if !defined(_WIN32)
        ".size alignedCallTrampoline, .-alignedCallTrampoline\n"
#endif
);

void alignedCallTrampoline(void);

#if defined(_WIN32)
// Single-threaded in practice (the stubs are made at the game's start-up,
// before its threads): a critical section anyway.
static CRITICAL_SECTION stubLock;
static long stubLockReady;

static void stubLockAcquire(void)
{
    if (!stubLockReady)
    {
        InitializeCriticalSection(&stubLock);
        stubLockReady = 1;
    }
    EnterCriticalSection(&stubLock);
}
static void stubLockRelease(void)
{
    LeaveCriticalSection(&stubLock);
}
#else
static pthread_mutex_t stubLock = PTHREAD_MUTEX_INITIALIZER;

static void stubLockAcquire(void)
{
    pthread_mutex_lock(&stubLock);
}
static void stubLockRelease(void)
{
    pthread_mutex_unlock(&stubLock);
}
#endif

// A stub calling target on an aligned stack: push $target; jmp alignedCallTrampoline.
void *alignedCallStub(uintptr_t target)
{
    static uint8_t *page;
    static size_t used = 4096;
    uint8_t *stub;

    stubLockAcquire();
#if defined(_WIN32)
    if (used + 10 > 4096)
    {
        page = (uint8_t *)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!page)
        {
            stubLockRelease();
            return NULL;
        }
        used = 0;
    }
#else
    if (used + 10 > 4096)
    {
        uint8_t *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
        {
            stubLockRelease();
            return NULL;
        }
        page = p;
        used = 0;
    }
#endif
    stub = page + used;
    used += 10;
    stub[0] = 0x68;
    memcpy(stub + 1, &target, 4);
    stub[5] = 0xe9;
    int32_t rel32 = (int32_t)((uint8_t *)alignedCallTrampoline - (stub + 10));
    memcpy(stub + 6, &rel32, 4);
    stubLockRelease();
    return stub;
}

// Functions that work on their caller's frame: not through a stub.
static const char *const direct[] = {
    "setjmp", "_setjmp", "sigsetjmp", "__sigsetjmp", "vfork", "getcontext", "swapcontext", "makecontext", "clone", NULL,
};

int alignedCallDirect(const char *name)
{
    for (int d = 0; direct[d]; d++)
        if (!strcmp(name, direct[d]))
            return 1;
    return 0;
}
