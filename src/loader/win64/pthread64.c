// libpthread on Windows threads (see ll64.h). The games' objects keep their
// glibc sizes; a zero-filled one (the static initializers) is ready, as the
// SRW locks and condition variables inside them are.
//   pthread_mutex_t (40): +4 count, +8 owner (recursive), +16 kind, +24 SRWLOCK
//   pthread_cond_t (48): +0 CONDITION_VARIABLE, +8 clock
//   pthread_rwlock_t (56): +0 SRWLOCK, +8 the writer's thread id
//   sem_t (32): +0 HANDLE, +8 count
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ll64.h"

#define LX_ETIMEDOUT 110
#define LX_EBUSY 16
#define LX_EINVAL 22
#define LX_EAGAIN 11
#define MAX_KEYS 1024

typedef SYSV void *(*StartFn)(void *);
typedef SYSV void (*DestructorFn)(void *);

typedef struct LxThread
{
    HANDLE handle;
    DWORD id;
    StartFn start;
    void *arg, *ret;
    volatile LONG detached, finished;
    void *values[MAX_KEYS];
} LxThread;

static __thread LxThread *self;
static DestructorFn destructors[MAX_KEYS];
static volatile LONG keyUsed[MAX_KEYS];

static LxThread *current(void)
{
    if (!self)
    {
        self = calloc(1, sizeof(LxThread));
        self->id = GetCurrentThreadId();
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self->handle, 0, FALSE,
                        DUPLICATE_SAME_ACCESS);
        self->detached = 1;
    }
    return self;
}

static void runDestructors(LxThread *t)
{
    for (int round = 0; round < 4; round++)
    {
        int any = 0;
        for (int k = 0; k < MAX_KEYS; k++)
        {
            void *v = t->values[k];
            if (v && destructors[k] && keyUsed[k])
            {
                t->values[k] = NULL;
                destructors[k](v);
                any = 1;
            }
        }
        if (!any)
            break;
    }
}

static void threadDone(LxThread *t)
{
    runDestructors(t);
    InterlockedExchange(&t->finished, 1);
    if (t->detached)
    {
        CloseHandle(t->handle);
        // The struct stays: a late pthread_self/join of a detached thread is
        // the game's error, not a use after free here.
    }
}

static DWORD WINAPI threadProc(void *p)
{
    LxThread *t = p;
    self = t;
    lxTlsThreadInit();
    t->ret = t->start(t->arg);
    threadDone(t);
    return 0;
}

typedef struct
{
    int schedParam, policy, flags, pad;
    size_t guardSize;
    void *stackAddr;
    size_t stackSize;
    void *cpuset;
    size_t cpusetSize;
} LxAttr;

static SYSV int lx_pthread_create(LxThread **out, const LxAttr *attr, StartFn start, void *arg)
{
    LxThread *t = calloc(1, sizeof(LxThread));
    size_t stack = attr && attr->stackSize ? attr->stackSize : 8 << 20;
    if (stack < (1 << 20))
        stack = 1 << 20;
    t->start = start;
    t->arg = arg;
    t->detached = attr && (attr->flags & 1);
    // The whole stack committed: the Linux code does not probe its pages.
    t->handle = CreateThread(NULL, stack, threadProc, t, CREATE_SUSPENDED, &t->id);
    if (!t->handle)
    {
        free(t);
        return LX_EAGAIN;
    }
    *out = t;
    lxTraceLog("pthread_create %p -> thread %lu\n", (void *)start, t->id);
    ResumeThread(t->handle);
    return 0;
}

static SYSV int lx_pthread_join(LxThread *t, void **ret)
{
    if (!t || t == self)
        return LX_EINVAL;
    WaitForSingleObject(t->handle, INFINITE);
    if (ret)
        *ret = t->ret;
    CloseHandle(t->handle);
    free(t);
    return 0;
}

typedef struct
{
    int64_t tv_sec, tv_nsec;
} LxTimespec;

static DWORD msUntil(const LxTimespec *abs, int clock)
{
    LxTimespec now;
    SYSV int (*getTime)(int, LxTimespec *) = lxBridge("clock_gettime");
    int64_t ms;
    getTime(clock, &now);
    ms = (abs->tv_sec - now.tv_sec) * 1000 + (abs->tv_nsec - now.tv_nsec) / 1000000;
    return ms <= 0 ? 0 : ms > 0x7ffffffe ? 0x7ffffffe : (DWORD)ms;
}

static SYSV int lx_pthread_timedjoin_np(LxThread *t, void **ret, const LxTimespec *abs)
{
    if (WaitForSingleObject(t->handle, msUntil(abs, 0)) == WAIT_TIMEOUT)
        return LX_ETIMEDOUT;
    return lx_pthread_join(t, ret);
}

static SYSV int lx_pthread_tryjoin_np(LxThread *t, void **ret)
{
    if (WaitForSingleObject(t->handle, 0) == WAIT_TIMEOUT)
        return LX_EBUSY;
    return lx_pthread_join(t, ret);
}

static SYSV int lx_pthread_detach(LxThread *t)
{
    InterlockedExchange(&t->detached, 1);
    if (t->finished)
        CloseHandle(t->handle);
    return 0;
}

static SYSV LxThread *lx_pthread_self(void) { return current(); }
static SYSV int lx_pthread_equal(LxThread *a, LxThread *b) { return a == b; }
static SYSV void lx_pthread_exit(void *ret)
{
    LxThread *t = current();
    t->ret = ret;
    threadDone(t);
    ExitThread(0);
}

static SYSV int lx_pthread_key_create(unsigned *key, DestructorFn d)
{
    for (int k = 1; k < MAX_KEYS; k++)
        if (!InterlockedCompareExchange(&keyUsed[k], 1, 0))
        {
            destructors[k] = d;
            *key = (unsigned)k;
            return 0;
        }
    return LX_EAGAIN;
}
static SYSV int lx_pthread_key_delete(unsigned key)
{
    if (key < MAX_KEYS)
        InterlockedExchange(&keyUsed[key], 0);
    return 0;
}
static SYSV void *lx_pthread_getspecific(unsigned key) { return key < MAX_KEYS ? current()->values[key] : NULL; }
static SYSV int lx_pthread_setspecific(unsigned key, const void *v)
{
    if (key >= MAX_KEYS)
        return LX_EINVAL;
    current()->values[key] = (void *)v;
    return 0;
}

static SYSV int lx_pthread_once(volatile LONG *once, SYSV void (*fn)(void))
{
    if (*once == 2)
        return 0;
    if (!InterlockedCompareExchange(once, 1, 0))
    {
        fn();
        InterlockedExchange(once, 2);
        return 0;
    }
    while (*once != 2)
        SwitchToThread();
    return 0;
}

// Attributes.
static SYSV int lx_attr_init(LxAttr *a) { memset(a, 0, sizeof(*a)); a->stackSize = 8 << 20; return 0; }
static SYSV int lx_attr_setdetachstate(LxAttr *a, int s) { a->flags = (a->flags & ~1) | (s & 1); return 0; }
static SYSV int lx_attr_getdetachstate(const LxAttr *a, int *s) { *s = a->flags & 1; return 0; }
static SYSV int lx_attr_setstacksize(LxAttr *a, size_t n) { a->stackSize = n; return 0; }
static SYSV int lx_attr_getstacksize(const LxAttr *a, size_t *n) { *n = a->stackSize; return 0; }
static SYSV int lx_attr_setstack(LxAttr *a, void *p, size_t n) { a->stackAddr = p; a->stackSize = n; return 0; }
static SYSV int lx_attr_getstack(const LxAttr *a, void **p, size_t *n) { *p = a->stackAddr; *n = a->stackSize; return 0; }
static SYSV int lx_attr_setguardsize(LxAttr *a, size_t n) { a->guardSize = n; return 0; }
static SYSV int lx_attr_getguardsize(const LxAttr *a, size_t *n) { *n = a->guardSize; return 0; }
static SYSV int lx_attr_setschedparam(LxAttr *a, const int *p) { a->schedParam = *p; return 0; }
static SYSV int lx_attr_getschedparam(const LxAttr *a, int *p) { *p = a->schedParam; return 0; }
static SYSV int lx_attr_setschedpolicy(LxAttr *a, int p) { a->policy = p; return 0; }
static SYSV int lx_attr_getschedpolicy(const LxAttr *a, int *p) { *p = a->policy; return 0; }
static SYSV int lx_zero(void) { return 0; }

static SYSV int lx_pthread_getattr_np(LxThread *t, LxAttr *a)
{
    ULONG_PTR lo, hi;
    (void)t;
    lx_attr_init(a);
    GetCurrentThreadStackLimits(&lo, &hi);
    a->stackAddr = (void *)lo;
    a->stackSize = hi - lo;
    return 0;
}

// Mutexes.
#define M_COUNT(m) (*(volatile LONG *)((uint8_t *)(m) + 4))
#define M_OWNER(m) (*(volatile LONG *)((uint8_t *)(m) + 8))
#define M_KIND(m) (*(int *)((uint8_t *)(m) + 16))
#define M_LOCK(m) ((PSRWLOCK)((uint8_t *)(m) + 24))

static SYSV int lx_mutex_init(void *m, const int *attr)
{
    memset(m, 0, 40);
    M_KIND(m) = attr ? *attr : 0;
    return 0;
}
static SYSV int lx_mutex_lock(void *m)
{
    if (M_KIND(m) == 1)
    {
        LONG me = (LONG)GetCurrentThreadId();
        if (M_OWNER(m) == me)
        {
            M_COUNT(m)++;
            return 0;
        }
        AcquireSRWLockExclusive(M_LOCK(m));
        M_OWNER(m) = me;
        M_COUNT(m) = 1;
        return 0;
    }
    AcquireSRWLockExclusive(M_LOCK(m));
    M_OWNER(m) = (LONG)GetCurrentThreadId();
    return 0;
}
static SYSV int lx_mutex_trylock(void *m)
{
    LONG me = (LONG)GetCurrentThreadId();
    if (M_KIND(m) == 1 && M_OWNER(m) == me)
    {
        M_COUNT(m)++;
        return 0;
    }
    if (!TryAcquireSRWLockExclusive(M_LOCK(m)))
        return LX_EBUSY;
    M_OWNER(m) = me;
    M_COUNT(m) = 1;
    return 0;
}
static SYSV int lx_mutex_unlock(void *m)
{
    if (M_KIND(m) == 1 && --M_COUNT(m) > 0)
        return 0;
    M_OWNER(m) = 0;
    ReleaseSRWLockExclusive(M_LOCK(m));
    return 0;
}
static SYSV int lx_mutex_timedlock(void *m, const LxTimespec *abs)
{
    DWORD deadline = GetTickCount() + msUntil(abs, 0);
    while (lx_mutex_trylock(m))
    {
        if ((LONG)(GetTickCount() - deadline) >= 0)
            return LX_ETIMEDOUT;
        Sleep(1);
    }
    return 0;
}
static SYSV int lx_mutexattr_init(int *a) { *a = 0; return 0; }
static SYSV int lx_mutexattr_settype(int *a, int t) { *a = t; return 0; }
static SYSV int lx_mutexattr_gettype(const int *a, int *t) { *t = *a; return 0; }

// Condition variables.
#define C_VAR(c) ((PCONDITION_VARIABLE)(c))
#define C_CLOCK(c) (*(int *)((uint8_t *)(c) + 8))

static SYSV int lx_cond_init(void *c, const int *attr)
{
    memset(c, 0, 48);
    C_CLOCK(c) = attr ? *attr : 0;
    return 0;
}

static int condWait(void *c, void *m, DWORD ms)
{
    LONG count = M_COUNT(m);
    BOOL ok;
    M_OWNER(m) = 0;
    ok = SleepConditionVariableSRW(C_VAR(c), M_LOCK(m), ms, 0);
    M_OWNER(m) = (LONG)GetCurrentThreadId();
    M_COUNT(m) = count;
    return ok ? 0 : LX_ETIMEDOUT;
}
static SYSV int lx_cond_wait(void *c, void *m) { return condWait(c, m, INFINITE); }
static SYSV int lx_cond_timedwait(void *c, void *m, const LxTimespec *abs) { return condWait(c, m, msUntil(abs, C_CLOCK(c))); }
static SYSV int lx_cond_clockwait(void *c, void *m, int clock, const LxTimespec *abs) { return condWait(c, m, msUntil(abs, clock)); }
static SYSV int lx_cond_signal(void *c) { WakeConditionVariable(C_VAR(c)); return 0; }
static SYSV int lx_cond_broadcast(void *c) { WakeAllConditionVariable(C_VAR(c)); return 0; }
static SYSV int lx_condattr_init(int *a) { *a = 0; return 0; }
static SYSV int lx_condattr_setclock(int *a, int clock) { *a = clock; return 0; }
static SYSV int lx_condattr_getclock(const int *a, int *clock) { *clock = *a; return 0; }

// Read-write locks.
#define RW_LOCK(l) ((PSRWLOCK)(l))
#define RW_WRITER(l) (*(volatile LONG *)((uint8_t *)(l) + 8))
static SYSV int lx_rwlock_init(void *l, const void *a) { (void)a; memset(l, 0, 56); return 0; }
static SYSV int lx_rwlock_rdlock(void *l) { AcquireSRWLockShared(RW_LOCK(l)); return 0; }
static SYSV int lx_rwlock_tryrdlock(void *l) { return TryAcquireSRWLockShared(RW_LOCK(l)) ? 0 : LX_EBUSY; }
static SYSV int lx_rwlock_wrlock(void *l)
{
    AcquireSRWLockExclusive(RW_LOCK(l));
    RW_WRITER(l) = (LONG)GetCurrentThreadId();
    return 0;
}
static SYSV int lx_rwlock_trywrlock(void *l)
{
    if (!TryAcquireSRWLockExclusive(RW_LOCK(l)))
        return LX_EBUSY;
    RW_WRITER(l) = (LONG)GetCurrentThreadId();
    return 0;
}
static SYSV int lx_rwlock_unlock(void *l)
{
    if (RW_WRITER(l) == (LONG)GetCurrentThreadId())
    {
        RW_WRITER(l) = 0;
        ReleaseSRWLockExclusive(RW_LOCK(l));
    }
    else
        ReleaseSRWLockShared(RW_LOCK(l));
    return 0;
}

// Spin locks.
static SYSV int lx_spin_init(volatile LONG *l, int s) { (void)s; *l = 0; return 0; }
static SYSV int lx_spin_lock(volatile LONG *l)
{
    while (InterlockedExchange(l, 1))
        YieldProcessor();
    return 0;
}
static SYSV int lx_spin_trylock(volatile LONG *l) { return InterlockedExchange(l, 1) ? LX_EBUSY : 0; }
static SYSV int lx_spin_unlock(volatile LONG *l) { InterlockedExchange(l, 0); return 0; }

// Semaphores.
#define S_HANDLE(s) (*(HANDLE *)(s))
#define S_COUNT(s) (*(volatile LONG *)((uint8_t *)(s) + 8))
static HANDLE semHandle(void *s)
{
    if (!S_HANDLE(s))
    {
        HANDLE h = CreateSemaphoreW(NULL, 0, 0x7fffffff, NULL);
        if (InterlockedCompareExchangePointer((void **)s, h, NULL))
            CloseHandle(h);
    }
    return S_HANDLE(s);
}
static SYSV int lx_sem_init(void *s, int shared, unsigned value)
{
    (void)shared;
    memset(s, 0, 32);
    S_HANDLE(s) = CreateSemaphoreW(NULL, (LONG)value, 0x7fffffff, NULL);
    S_COUNT(s) = (LONG)value;
    return 0;
}
static SYSV int lx_sem_destroy(void *s)
{
    if (S_HANDLE(s))
        CloseHandle(S_HANDLE(s));
    S_HANDLE(s) = NULL;
    return 0;
}
static SYSV int lx_sem_post(void *s)
{
    InterlockedIncrement(&S_COUNT(s));
    ReleaseSemaphore(semHandle(s), 1, NULL);
    return 0;
}
static SYSV int lx_sem_wait(void *s)
{
    WaitForSingleObject(semHandle(s), INFINITE);
    InterlockedDecrement(&S_COUNT(s));
    return 0;
}
static SYSV int lx_sem_trywait(void *s)
{
    if (WaitForSingleObject(semHandle(s), 0) != WAIT_OBJECT_0)
    {
        lxSetErrno(LX_EAGAIN);
        return -1;
    }
    InterlockedDecrement(&S_COUNT(s));
    return 0;
}
static SYSV int lx_sem_timedwait(void *s, const LxTimespec *abs)
{
    if (WaitForSingleObject(semHandle(s), msUntil(abs, 0)) != WAIT_OBJECT_0)
    {
        lxSetErrno(LX_ETIMEDOUT);
        return -1;
    }
    InterlockedDecrement(&S_COUNT(s));
    return 0;
}
static SYSV int lx_sem_getvalue(void *s, int *v) { *v = (int)S_COUNT(s); return 0; }

// Signals: none here.
static SYSV void *lx_signal(int sig, void *h) { (void)sig; (void)h; return NULL; }

static const LxSym syms[] = {
    {"pthread_create", lx_pthread_create}, {"pthread_join", lx_pthread_join},
    {"pthread_timedjoin_np", lx_pthread_timedjoin_np}, {"pthread_tryjoin_np", lx_pthread_tryjoin_np},
    {"pthread_detach", lx_pthread_detach}, {"pthread_self", lx_pthread_self}, {"pthread_equal", lx_pthread_equal},
    {"pthread_exit", lx_pthread_exit}, {"pthread_key_create", lx_pthread_key_create},
    {"__pthread_key_create", lx_pthread_key_create}, {"pthread_key_delete", lx_pthread_key_delete},
    {"pthread_getspecific", lx_pthread_getspecific}, {"pthread_setspecific", lx_pthread_setspecific},
    {"pthread_once", lx_pthread_once}, {"__pthread_once", lx_pthread_once},
    {"pthread_attr_init", lx_attr_init}, {"pthread_attr_destroy", lx_zero},
    {"pthread_attr_setdetachstate", lx_attr_setdetachstate}, {"pthread_attr_getdetachstate", lx_attr_getdetachstate},
    {"pthread_attr_setstacksize", lx_attr_setstacksize}, {"pthread_attr_getstacksize", lx_attr_getstacksize},
    {"pthread_attr_setstack", lx_attr_setstack}, {"pthread_attr_getstack", lx_attr_getstack},
    {"pthread_attr_setguardsize", lx_attr_setguardsize}, {"pthread_attr_getguardsize", lx_attr_getguardsize},
    {"pthread_attr_setschedparam", lx_attr_setschedparam}, {"pthread_attr_getschedparam", lx_attr_getschedparam},
    {"pthread_attr_setschedpolicy", lx_attr_setschedpolicy}, {"pthread_attr_getschedpolicy", lx_attr_getschedpolicy},
    {"pthread_attr_setinheritsched", lx_zero}, {"pthread_attr_setscope", lx_zero},
    {"pthread_getattr_np", lx_pthread_getattr_np}, {"pthread_setname_np", lx_zero}, {"pthread_getname_np", lx_zero},
    {"pthread_setschedparam", lx_zero}, {"pthread_getschedparam", lx_zero}, {"pthread_setaffinity_np", lx_zero},
    {"pthread_getaffinity_np", lx_zero}, {"pthread_cancel", lx_zero}, {"pthread_setcancelstate", lx_zero},
    {"pthread_setcanceltype", lx_zero}, {"pthread_testcancel", lx_zero}, {"pthread_kill", lx_zero},
    {"pthread_sigmask", lx_zero}, {"pthread_atfork", lx_zero}, {"pthread_yield", lx_zero},
    {"pthread_mutex_init", lx_mutex_init}, {"pthread_mutex_destroy", lx_zero}, {"pthread_mutex_lock", lx_mutex_lock},
    {"pthread_mutex_trylock", lx_mutex_trylock}, {"pthread_mutex_unlock", lx_mutex_unlock},
    {"pthread_mutex_timedlock", lx_mutex_timedlock}, {"__pthread_mutex_lock", lx_mutex_lock},
    {"__pthread_mutex_unlock", lx_mutex_unlock},
    {"pthread_mutexattr_init", lx_mutexattr_init}, {"pthread_mutexattr_destroy", lx_zero},
    {"pthread_mutexattr_settype", lx_mutexattr_settype}, {"pthread_mutexattr_gettype", lx_mutexattr_gettype},
    {"pthread_mutexattr_setpshared", lx_zero}, {"pthread_mutexattr_setprotocol", lx_zero},
    {"pthread_cond_init", lx_cond_init}, {"pthread_cond_destroy", lx_zero}, {"pthread_cond_wait", lx_cond_wait},
    {"pthread_cond_timedwait", lx_cond_timedwait}, {"pthread_cond_clockwait", lx_cond_clockwait},
    {"pthread_cond_signal", lx_cond_signal}, {"pthread_cond_broadcast", lx_cond_broadcast},
    {"pthread_condattr_init", lx_condattr_init}, {"pthread_condattr_destroy", lx_zero},
    {"pthread_condattr_setclock", lx_condattr_setclock}, {"pthread_condattr_getclock", lx_condattr_getclock},
    {"pthread_condattr_setpshared", lx_zero},
    {"pthread_rwlock_init", lx_rwlock_init}, {"pthread_rwlock_destroy", lx_zero},
    {"pthread_rwlock_rdlock", lx_rwlock_rdlock}, {"pthread_rwlock_tryrdlock", lx_rwlock_tryrdlock},
    {"pthread_rwlock_wrlock", lx_rwlock_wrlock}, {"pthread_rwlock_trywrlock", lx_rwlock_trywrlock},
    {"pthread_rwlock_unlock", lx_rwlock_unlock},
    {"pthread_spin_init", lx_spin_init}, {"pthread_spin_destroy", lx_zero}, {"pthread_spin_lock", lx_spin_lock},
    {"pthread_spin_trylock", lx_spin_trylock}, {"pthread_spin_unlock", lx_spin_unlock},
    {"sem_init", lx_sem_init}, {"sem_destroy", lx_sem_destroy}, {"sem_post", lx_sem_post}, {"sem_wait", lx_sem_wait},
    {"sem_trywait", lx_sem_trywait}, {"sem_timedwait", lx_sem_timedwait}, {"sem_getvalue", lx_sem_getvalue},
    {"signal", lx_signal}, {"sigaction", lx_zero}, {"sigemptyset", lx_zero}, {"sigfillset", lx_zero},
    {"sigaddset", lx_zero}, {"sigdelset", lx_zero}, {"sigprocmask", lx_zero}, {"sigaltstack", lx_zero},
};

void lxPthreadInit(void)
{
    lxRegister(syms, LX_COUNT(syms));
}
