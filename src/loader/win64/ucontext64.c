// getcontext, setcontext, swapcontext and makecontext (see ll64.h): the g7
// games run their scripts as coroutines on stacks of their own. glibc's
// x86-64 ucontext_t:
//   +16 uc_stack (ss_sp, +24 ss_flags, +32 ss_size), +40 the registers
//   (gregs[REG_*]), +224 the FP state's address, +424 the FP state itself
//   (+0 control word, +24 MXCSR); +232 mcontext.s __reserved1: here the TEB.s stack base and
//   limit for that stack, so that Windows sees the stack the thread is on.
#include <windows.h>
#include <string.h>
#include "ll64.h"

#define UC_LINK 8
#define UC_SS_SP 16
#define UC_SS_SIZE 32
#define UC_GREGS 40
#define REG(n) (UC_GREGS + 8 * (n))
enum { R8, R9, R10, R11, R12, R13, R14, R15, RDI, RSI, RBP, RBX, RDX, RAX, RCX, RSP, RIP };
#define UC_FPREGS 224
#define UC_FPMEM 424
#define UC_STACKBASE 232
#define UC_STACKLIMIT 240

extern void lxGetcontext(void), lxSetcontext(void), lxSwapcontext(void), lxStartContext(void);

// rdi: the context. Saves the callee-saved registers, the argument ones,
// the caller's stack and return address, the FP control words and the TEB's
// stack bounds; returns 0.
#define SAVE                                                                                                           \
    "    mov %rbx, 128(%rdi)\n"                                                                                        \
    "    mov %rbp, 120(%rdi)\n"                                                                                        \
    "    mov %r12, 72(%rdi)\n"                                                                                         \
    "    mov %r13, 80(%rdi)\n"                                                                                         \
    "    mov %r14, 88(%rdi)\n"                                                                                         \
    "    mov %r15, 96(%rdi)\n"                                                                                         \
    "    mov %rdi, 104(%rdi)\n"                                                                                        \
    "    mov %rsi, 112(%rdi)\n"                                                                                        \
    "    mov %rdx, 136(%rdi)\n"                                                                                        \
    "    mov %rcx, 152(%rdi)\n"                                                                                        \
    "    mov %r8, 40(%rdi)\n"                                                                                          \
    "    mov %r9, 48(%rdi)\n"                                                                                          \
    "    mov (%rsp), %rcx\n"                                                                                           \
    "    mov %rcx, 168(%rdi)\n"                                                                                        \
    "    lea 8(%rsp), %rcx\n"                                                                                          \
    "    mov %rcx, 160(%rdi)\n"                                                                                        \
    "    lea 424(%rdi), %rcx\n"                                                                                        \
    "    mov %rcx, 224(%rdi)\n"                                                                                        \
    "    fnstcw 424(%rdi)\n"                                                                                           \
    "    stmxcsr 448(%rdi)\n"                                                                                          \
    "    mov %gs:8, %rcx\n"                                                                                            \
    "    mov %rcx, 232(%rdi)\n"                                                                                        \
    "    mov %gs:16, %rcx\n"                                                                                           \
    "    mov %rcx, 240(%rdi)\n"

// rdx: the context to switch to (never returns).
#define RESTORE                                                                                                        \
    "    fldcw 424(%rdx)\n"                                                                                            \
    "    ldmxcsr 448(%rdx)\n"                                                                                          \
    "    mov 232(%rdx), %rcx\n"                                                                                        \
    "    test %rcx, %rcx\n"                                                                                            \
    "    jz 2f\n"                                                                                                      \
    "    mov %rcx, %gs:8\n"                                                                                            \
    "    mov 240(%rdx), %rcx\n"                                                                                        \
    "    mov %rcx, %gs:16\n"                                                                                           \
    "2:  mov 160(%rdx), %rsp\n"                                                                                        \
    "    mov 128(%rdx), %rbx\n"                                                                                        \
    "    mov 120(%rdx), %rbp\n"                                                                                        \
    "    mov 72(%rdx), %r12\n"                                                                                         \
    "    mov 80(%rdx), %r13\n"                                                                                         \
    "    mov 88(%rdx), %r14\n"                                                                                         \
    "    mov 96(%rdx), %r15\n"                                                                                         \
    "    mov 168(%rdx), %rcx\n"                                                                                        \
    "    push %rcx\n"                                                                                                  \
    "    mov 104(%rdx), %rdi\n"                                                                                        \
    "    mov 112(%rdx), %rsi\n"                                                                                        \
    "    mov 152(%rdx), %rcx\n"                                                                                        \
    "    mov 40(%rdx), %r8\n"                                                                                          \
    "    mov 48(%rdx), %r9\n"                                                                                          \
    "    mov 136(%rdx), %rdx\n"                                                                                        \
    "    xor %eax, %eax\n"                                                                                             \
    "    ret\n"

__asm__(".globl lxGetcontext\n"
        "lxGetcontext:\n" SAVE
        "    xor %eax, %eax\n"
        "    ret\n"
        ".globl lxSetcontext\n"
        "lxSetcontext:\n"
        "    mov %rdi, %rdx\n" RESTORE
        ".globl lxSwapcontext\n"
        "lxSwapcontext:\n"
        "    mov %rsi, %rdx\n" SAVE
        // The saved context resumes as a swapcontext returning 0.
        RESTORE
        // A function makecontext started has returned: rbx points at its
        // uc_link, the context to go on with (none: the thread ends).
        ".globl lxStartContext\n"
        "lxStartContext:\n"
        "    mov %rbx, %rsp\n"
        "    mov (%rsp), %rdx\n"
        "    test %rdx, %rdx\n"
        "    jz 3f\n" RESTORE
        "3:  and $-16, %rsp\n"
        "    sub $32, %rsp\n"
        "    xor %ecx, %ecx\n"
        "    call lxContextEnd\n");

void lxContextEnd(void)
{
    lxLog("a context without uc_link returned: the thread ends\n");
    ExitThread(0);
}

static SYSV void lx_makecontext(uint8_t *uc, void (*fn)(void), int argc, ...)
{
    __builtin_sysv_va_list ap;
    uint64_t *sp, *g = (uint64_t *)(uc + UC_GREGS);
    uint8_t *base = *(uint8_t **)(uc + UC_SS_SP);
    size_t size = *(size_t *)(uc + UC_SS_SIZE);
    int linkSlot = (argc > 6 ? argc - 6 : 0) + 1;
    static const int argRegs[6] = {RDI, RSI, RDX, RCX, R8, R9};

    sp = (uint64_t *)(base + size);
    sp -= linkSlot;
    sp = (uint64_t *)(((uintptr_t)sp & ~(uintptr_t)15) - 8);
    g[RIP] = (uint64_t)fn;
    g[RBX] = (uint64_t)&sp[linkSlot];
    g[RSP] = (uint64_t)sp;
    sp[0] = (uint64_t)lxStartContext;
    sp[linkSlot] = *(uint64_t *)(uc + UC_LINK);
    __builtin_sysv_va_start(ap, argc);
    for (int i = 0; i < argc; i++)
    {
        uint64_t a = __builtin_va_arg(ap, uint64_t);
        if (i < 6)
            g[argRegs[i]] = a;
        else
            sp[i - 5] = a;
    }
    __builtin_sysv_va_end(ap);
    // Windows' view of the stack while it runs.
    *(uint64_t *)(uc + UC_STACKBASE) = (uint64_t)(base + size);
    *(uint64_t *)(uc + UC_STACKLIMIT) = (uint64_t)base;
    *(uint64_t *)(uc + UC_FPREGS) = (uint64_t)(uc + UC_FPMEM);
}

static const LxSym syms[] = {
    {"getcontext", lxGetcontext}, {"setcontext", lxSetcontext}, {"swapcontext", lxSwapcontext},
    {"makecontext", lx_makecontext},
};

void lxUcontextInit(void)
{
    lxRegister(syms, LX_COUNT(syms));
}
