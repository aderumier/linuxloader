// libc: memory, strings, the environment, errno, time, processes, files and
// directories (see ll64.h). Linux x86-64's structures (stat, dirent, tm,
// timespec...) are written as glibc lays them out.
#include <windows.h>
#include <bcrypt.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include "ll64.h"

// ---------------------------------------------------------------------------
// errno

static __thread int lxErrno;

int *lxErrnoLocation(void) { return &lxErrno; }
void lxSetErrno(int e) { lxErrno = e; }

int lxErrnoFromCrt(int e)
{
    switch (e)
    {
    case 36: return 35;    // EDEADLK
    case 38: return 36;    // ENAMETOOLONG
    case 39: return 37;    // ENOLCK
    case 40: return 38;    // ENOSYS
    case 41: return 39;    // ENOTEMPTY
    case 42: return 84;    // EILSEQ
    case 138: return 110;  // ETIMEDOUT
    case 140: return 11;   // EWOULDBLOCK
    default: return e <= 34 ? e : 22;
    }
}

void lxSetErrnoFromCrt(void) { lxErrno = lxErrnoFromCrt(errno); }

static SYSV int *lx_errno_location(void) { return &lxErrno; }

// ---------------------------------------------------------------------------
// Memory: each block has its start and size before it, so that the aligned
// ones free as the others do.

typedef struct
{
    void *raw;
    size_t size;
} Header;

static void *wrapBlock(void *raw, size_t align, size_t size)
{
    uint8_t *p;
    if (!raw)
        return NULL;
    p = (uint8_t *)(((uintptr_t)raw + sizeof(Header) + align - 1) & ~(uintptr_t)(align - 1));
    ((Header *)p)[-1].raw = raw;
    ((Header *)p)[-1].size = size;
    return p;
}

SYSV void *lxMalloc(size_t n)
{
    return wrapBlock(malloc(n + sizeof(Header)), 16, n);
}

SYSV void lxFree(void *p)
{
    if (p)
        free(((Header *)p)[-1].raw);
}

SYSV void *lxCalloc(size_t a, size_t b)
{
    size_t n = a * b;
    void *p;
    if (b && n / b != a)
        return NULL;
    p = lxMalloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}

SYSV void *lxRealloc(void *p, size_t n)
{
    Header *h;
    void *q;
    if (!p)
        return lxMalloc(n);
    if (!n)
    {
        lxFree(p);
        return NULL;
    }
    h = &((Header *)p)[-1];
    if ((uint8_t *)h->raw == (uint8_t *)p - sizeof(Header))
    {
        void *raw = realloc(h->raw, n + sizeof(Header));
        return wrapBlock(raw, 16, n);
    }
    q = lxMalloc(n);
    if (q)
    {
        memcpy(q, p, h->size < n ? h->size : n);
        lxFree(p);
    }
    return q;
}

SYSV void *lxMemalign(size_t align, size_t n)
{
    if (align < 16)
        align = 16;
    return wrapBlock(malloc(n + sizeof(Header) + align), align, n);
}

static SYSV int lx_posix_memalign(void **out, size_t align, size_t n)
{
    void *p = lxMemalign(align, n);
    if (!p)
        return 12;
    *out = p;
    return 0;
}

static SYSV void *lx_aligned_alloc(size_t align, size_t n) { return lxMemalign(align, n); }
static SYSV void *lx_valloc(size_t n) { return lxMemalign(4096, n); }
static SYSV size_t lx_malloc_usable_size(void *p) { return p ? ((Header *)p)[-1].size : 0; }

SYSV char *lxStrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = lxMalloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}

static SYSV char *lx_strndup(const char *s, size_t m)
{
    size_t n = strnlen(s, m);
    char *p = lxMalloc(n + 1);
    if (p)
    {
        memcpy(p, s, n);
        p[n] = 0;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Strings.

static SYSV void *lx_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
static SYSV void *lx_memcpy_chk(void *d, const void *s, size_t n, size_t dn) { (void)dn; return memcpy(d, s, n); }
static SYSV void *lx_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }
static SYSV void *lx_memmove_chk(void *d, const void *s, size_t n, size_t dn) { (void)dn; return memmove(d, s, n); }
static SYSV void *lx_memset(void *d, int c, size_t n) { return memset(d, c, n); }
static SYSV void *lx_memset_chk(void *d, int c, size_t n, size_t dn) { (void)dn; return memset(d, c, n); }
static SYSV int lx_memcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
static SYSV void *lx_memchr(const void *s, int c, size_t n) { return (void *)memchr(s, c, n); }
static SYSV void *lx_memrchr(const void *s, int c, size_t n)
{
    const uint8_t *p = s;
    while (n--)
        if (p[n] == (uint8_t)c)
            return (void *)(p + n);
    return NULL;
}
static SYSV void *lx_mempcpy(void *d, const void *s, size_t n) { return (uint8_t *)memcpy(d, s, n) + n; }
static SYSV size_t lx_strlen(const char *s) { return strlen(s); }
static SYSV size_t lx_strnlen(const char *s, size_t n) { return strnlen(s, n); }
static SYSV char *lx_strcpy(char *d, const char *s) { return strcpy(d, s); }
static SYSV char *lx_strcpy_chk(char *d, const char *s, size_t n) { (void)n; return strcpy(d, s); }
static SYSV char *lx_stpcpy(char *d, const char *s) { size_t n = strlen(s); memcpy(d, s, n + 1); return d + n; }
static SYSV char *lx_stpcpy_chk(char *d, const char *s, size_t dn) { (void)dn; return lx_stpcpy(d, s); }
static SYSV char *lx_strncpy(char *d, const char *s, size_t n) { return strncpy(d, s, n); }
static SYSV char *lx_strncpy_chk(char *d, const char *s, size_t n, size_t dn) { (void)dn; return strncpy(d, s, n); }
static SYSV char *lx_strcat(char *d, const char *s) { return strcat(d, s); }
static SYSV char *lx_strcat_chk(char *d, const char *s, size_t dn) { (void)dn; return strcat(d, s); }
static SYSV char *lx_strncat(char *d, const char *s, size_t n) { return strncat(d, s, n); }
static SYSV char *lx_strncat_chk(char *d, const char *s, size_t n, size_t dn) { (void)dn; return strncat(d, s, n); }
static SYSV int lx_strcmp(const char *a, const char *b) { return strcmp(a, b); }
static SYSV int lx_strncmp(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
static SYSV int lx_strcasecmp(const char *a, const char *b) { return _stricmp(a, b); }
static SYSV int lx_strncasecmp(const char *a, const char *b, size_t n) { return _strnicmp(a, b, n); }
static SYSV int lx_strcoll(const char *a, const char *b) { return strcmp(a, b); }
static SYSV int lx_strcoll_l(const char *a, const char *b, void *l) { (void)l; return strcmp(a, b); }
static SYSV size_t lx_strxfrm(char *d, const char *s, size_t n)
{
    size_t l = strlen(s);
    if (n)
    {
        strncpy(d, s, n);
        if (l >= n)
            d[n - 1] = 0;
    }
    return l;
}
static SYSV size_t lx_strxfrm_l(char *d, const char *s, size_t n, void *l) { (void)l; return lx_strxfrm(d, s, n); }
static SYSV char *lx_strchr(const char *s, int c) { return (char *)strchr(s, c); }
static SYSV char *lx_strrchr(const char *s, int c) { return (char *)strrchr(s, c); }
static SYSV char *lx_strchrnul(const char *s, int c) { const char *p = strchr(s, c); return (char *)(p ? p : s + strlen(s)); }
static SYSV char *lx_strstr(const char *a, const char *b) { return (char *)strstr(a, b); }
static SYSV char *lx_strcasestr(const char *a, const char *b)
{
    size_t n = strlen(b);
    for (; *a; a++)
        if (!_strnicmp(a, b, n))
            return (char *)a;
    return n ? NULL : (char *)a;
}
static SYSV char *lx_strpbrk(const char *a, const char *b) { return (char *)strpbrk(a, b); }
static SYSV size_t lx_strspn(const char *a, const char *b) { return strspn(a, b); }
static SYSV size_t lx_strcspn(const char *a, const char *b) { return strcspn(a, b); }
static SYSV char *lx_strtok_r(char *s, const char *d, char **save) { return strtok_s(s, d, save); }
static SYSV char *lx_strtok(char *s, const char *d)
{
    static __thread char *save;
    return strtok_s(s, d, &save);
}
static SYSV char *lx_strsep(char **sp, const char *d)
{
    char *s = *sp, *e;
    if (!s)
        return NULL;
    e = s + strcspn(s, d);
    if (*e)
        *e++ = 0, *sp = e;
    else
        *sp = NULL;
    return s;
}

// The C locale's ctype tables, as glibc hands them (index -128..255).
static unsigned short ctypeB[384];
static int32_t ctypeLower[384], ctypeUpper[384];
static const unsigned short *ctypeBPtr = ctypeB + 128;
static const int32_t *ctypeLowerPtr = ctypeLower + 128, *ctypeUpperPtr = ctypeUpper + 128;

static void ctypeInit(void)
{
    for (int c = -128; c < 256; c++)
    {
        unsigned short f = 0;
        int u = c & 0xff;
        if (c >= 0 && c < 128)
        {
            if (isupper(u)) f |= 0x100;
            if (islower(u)) f |= 0x200;
            if (isalpha(u)) f |= 0x400;
            if (isdigit(u)) f |= 0x800;
            if (isxdigit(u)) f |= 0x1000;
            if (isspace(u)) f |= 0x2000;
            if (isprint(u)) f |= 0x4000;
            if (isgraph(u)) f |= 0x8000;
            if (u == ' ' || u == '\t') f |= 0x1;
            if (iscntrl(u)) f |= 0x2;
            if (ispunct(u)) f |= 0x4;
            if (isalnum(u)) f |= 0x8;
        }
        ctypeB[c + 128] = f;
        ctypeLower[c + 128] = c >= 'A' && c <= 'Z' ? c + 32 : c;
        ctypeUpper[c + 128] = c >= 'a' && c <= 'z' ? c - 32 : c;
    }
}

static SYSV const unsigned short **lx_ctype_b_loc(void) { return &ctypeBPtr; }
static SYSV const int32_t **lx_ctype_tolower_loc(void) { return &ctypeLowerPtr; }
static SYSV const int32_t **lx_ctype_toupper_loc(void) { return &ctypeUpperPtr; }
static SYSV size_t lx_ctype_get_mb_cur_max(void) { return 1; }
static SYSV int lx_tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static SYSV int lx_toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static SYSV int lx_isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static SYSV int lx_isdigit(int c) { return c >= '0' && c <= '9'; }
static SYSV int lx_isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
static SYSV int lx_isalnum(int c) { return lx_isalpha(c) || lx_isdigit(c); }
static SYSV int lx_isupper(int c) { return c >= 'A' && c <= 'Z'; }
static SYSV int lx_islower(int c) { return c >= 'a' && c <= 'z'; }
static SYSV int lx_isxdigit(int c) { return isxdigit(c & 0xff) && c < 128; }
static SYSV int lx_isprint(int c) { return c >= 32 && c < 127; }

// Numbers: Linux's long is 64 bits.
static SYSV lx_long lx_strtol(const char *s, char **e, int b) { return strtoll(s, e, b); }
static SYSV lx_ulong lx_strtoul(const char *s, char **e, int b) { return strtoull(s, e, b); }
static SYSV long long lx_strtoll(const char *s, char **e, int b) { return strtoll(s, e, b); }
static SYSV unsigned long long lx_strtoull(const char *s, char **e, int b) { return strtoull(s, e, b); }
static SYSV double lx_strtod(const char *s, char **e) { return strtod(s, e); }
static SYSV float lx_strtof(const char *s, char **e) { return strtof(s, e); }
static SYSV long double lx_strtold(const char *s, char **e) { return strtold(s, e); }
static SYSV double lx_strtod_l(const char *s, char **e, void *l) { (void)l; return strtod(s, e); }
static SYSV float lx_strtof_l(const char *s, char **e, void *l) { (void)l; return strtof(s, e); }
static SYSV long double lx_strtold_l(const char *s, char **e, void *l) { (void)l; return strtold(s, e); }
static SYSV int lx_atoi(const char *s) { return atoi(s); }
static SYSV lx_long lx_atol(const char *s) { return atoll(s); }
static SYSV long long lx_atoll(const char *s) { return atoll(s); }
static SYSV double lx_atof(const char *s) { return atof(s); }
static SYSV int lx_abs(int a) { return a < 0 ? -a : a; }
static SYSV lx_long lx_labs(lx_long a) { return a < 0 ? -a : a; }

// glibc's TYPE_3 random(), as the games' arithmetic expects RAND_MAX 2^31-1:
// r[n] = r[n-31] + r[n-3], the last 34 in a ring indexed by n.
static int32_t randRing[34];
static uint64_t randN;
static CRITICAL_SECTION randLock;
static SYSV void lx_srand(unsigned seed)
{
    int32_t r[344];
    r[0] = (int32_t)(seed ? seed : 1);
    for (int i = 1; i < 31; i++)
    {
        int64_t hi = r[i - 1] / 127773, lo = r[i - 1] % 127773, w = 16807 * lo - 2836 * hi;
        r[i] = (int32_t)(w < 0 ? w + 2147483647 : w);
    }
    for (int i = 31; i < 34; i++)
        r[i] = r[i - 31];
    for (int i = 34; i < 344; i++)
        r[i] = (int32_t)((uint32_t)r[i - 31] + (uint32_t)r[i - 3]);
    EnterCriticalSection(&randLock);
    for (int i = 310; i < 344; i++)
        randRing[i % 34] = r[i];
    randN = 344;
    LeaveCriticalSection(&randLock);
}
static SYSV int lx_rand(void)
{
    int32_t v;
    if (!randN)
        lx_srand(1);
    EnterCriticalSection(&randLock);
    v = (int32_t)((uint32_t)randRing[(randN - 31) % 34] + (uint32_t)randRing[(randN - 3) % 34]);
    randRing[randN % 34] = v;
    randN++;
    LeaveCriticalSection(&randLock);
    return (int)((uint32_t)v >> 1);
}
static SYSV int lx_rand_r(unsigned *seed)
{
    unsigned next = *seed;
    int result;
    next = next * 1103515245 + 12345;
    result = (unsigned)(next / 65536) % 2048;
    next = next * 1103515245 + 12345;
    result = (result << 10) ^ ((unsigned)(next / 65536) % 1024);
    next = next * 1103515245 + 12345;
    result = (result << 10) ^ ((unsigned)(next / 65536) % 1024);
    *seed = next;
    return result;
}
static SYSV int lx_getentropy(void *buf, size_t n)
{
    BCryptGenRandom(NULL, buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return 0;
}
static SYSV uint32_t lx_arc4random(void)
{
    uint32_t v;
    lx_getentropy(&v, sizeof(v));
    return v;
}

// qsort and bsearch with the game's comparator.
typedef SYSV int (*LxCompare)(const void *, const void *);
static void swapBytes(uint8_t *a, uint8_t *b, size_t n)
{
    while (n--)
    {
        uint8_t t = *a;
        *a++ = *b;
        *b++ = t;
    }
}
static void qsortRec(uint8_t *base, size_t n, size_t size, LxCompare cmp)
{
    while (n > 1)
    {
        if (n < 8)
        {
            for (size_t i = 1; i < n; i++)
                for (size_t j = i; j > 0 && cmp(base + (j - 1) * size, base + j * size) > 0; j--)
                    swapBytes(base + (j - 1) * size, base + j * size, size);
            return;
        }
        size_t mid = n / 2, i = 0, store;
        swapBytes(base + mid * size, base + (n - 1) * size, size);
        uint8_t *pivot = base + (n - 1) * size;
        for (store = 0; i < n - 1; i++)
            if (cmp(base + i * size, pivot) < 0)
                swapBytes(base + i * size, base + store++ * size, size);
        swapBytes(base + store * size, pivot, size);
        if (store < n - store - 1)
        {
            qsortRec(base, store, size, cmp);
            base += (store + 1) * size;
            n -= store + 1;
        }
        else
        {
            qsortRec(base + (store + 1) * size, n - store - 1, size, cmp);
            n = store;
        }
    }
}
static SYSV void lx_qsort(void *base, size_t n, size_t size, LxCompare cmp) { qsortRec(base, n, size, cmp); }
static SYSV void *lx_bsearch(const void *key, const void *base, size_t n, size_t size, LxCompare cmp)
{
    size_t lo = 0, hi = n;
    while (lo < hi)
    {
        size_t mid = (lo + hi) / 2;
        const uint8_t *p = (const uint8_t *)base + mid * size;
        int r = cmp(key, p);
        if (!r)
            return (void *)p;
        if (r < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// The environment, the games' own copy (UTF-8).

static char **lxEnviron;
static size_t envCount, envCap;

static void envInit(void)
{
    char *block = GetEnvironmentStringsA();
    envCap = 64;
    lxEnviron = calloc(envCap, sizeof(char *));
    for (char *p = block; p && *p; p += strlen(p) + 1)
    {
        if (*p == '=')
            continue;
        if (envCount + 1 >= envCap)
            lxEnviron = realloc(lxEnviron, (envCap *= 2) * sizeof(char *));
        lxEnviron[envCount++] = _strdup(p);
    }
    lxEnviron[envCount] = NULL;
    FreeEnvironmentStringsA(block);
}

char **lxEnvironment(void) { return lxEnviron; }

static int envFind(const char *name, size_t n)
{
    for (size_t i = 0; i < envCount; i++)
        if (!strncmp(lxEnviron[i], name, n) && lxEnviron[i][n] == '=')
            return (int)i;
    return -1;
}

SYSV char *lxGetenv(const char *name)
{
    int i = envFind(name, strlen(name));
    return i < 0 ? NULL : lxEnviron[i] + strlen(name) + 1;
}

static SYSV int lx_setenv(const char *name, const char *value, int overwrite)
{
    size_t n = strlen(name);
    int i = envFind(name, n);
    char *entry;
    if (i >= 0 && !overwrite)
        return 0;
    entry = malloc(n + strlen(value) + 2);
    sprintf(entry, "%s=%s", name, value);
    if (i >= 0)
        lxEnviron[i] = entry;
    else
    {
        if (envCount + 1 >= envCap)
            lxEnviron = realloc(lxEnviron, (envCap *= 2) * sizeof(char *));
        lxEnviron[envCount++] = entry;
        lxEnviron[envCount] = NULL;
    }
    SetEnvironmentVariableA(name, value);
    return 0;
}

static SYSV int lx_unsetenv(const char *name)
{
    int i = envFind(name, strlen(name));
    if (i >= 0)
    {
        memmove(&lxEnviron[i], &lxEnviron[i + 1], (envCount - i) * sizeof(char *));
        envCount--;
    }
    SetEnvironmentVariableA(name, NULL);
    return 0;
}

static SYSV int lx_putenv(char *s)
{
    char name[256];
    const char *eq = strchr(s, '=');
    if (!eq || eq - s >= (int)sizeof(name))
        return lx_unsetenv(s);
    memcpy(name, s, eq - s);
    name[eq - s] = 0;
    return lx_setenv(name, eq + 1, 1);
}

// ---------------------------------------------------------------------------
// Exit.

#define MAX_ATEXIT 256
static struct
{
    SYSV void (*fn)(void *);
    void *arg;
} atexits[MAX_ATEXIT];
static int nAtexit;
static CRITICAL_SECTION exitLock;

static SYSV int lx_cxa_atexit(SYSV void (*fn)(void *), void *arg, void *dso)
{
    (void)dso;
    EnterCriticalSection(&exitLock);
    if (nAtexit < MAX_ATEXIT)
    {
        atexits[nAtexit].fn = fn;
        atexits[nAtexit].arg = arg;
        nAtexit++;
    }
    LeaveCriticalSection(&exitLock);
    return 0;
}
static SYSV int lx_atexit(SYSV void (*fn)(void)) { return lx_cxa_atexit((SYSV void (*)(void *))fn, NULL, NULL); }
static SYSV int lx_cxa_thread_atexit_impl(void *fn, void *arg, void *dso) { (void)fn; (void)arg; (void)dso; return 0; }
static SYSV void lx_cxa_finalize(void *dso) { (void)dso; }

void lxRunFini(void);

void lxExit(int code)
{
    static LONG exiting;
    if (InterlockedExchange(&exiting, 1))
        ExitProcess(code);
    lxLog("exit(%d)\n", code);
    // LL64_EXIT_CLEAN=1: the game's destructors run (off by default: some
    // stop on threads already gone).
    if (getenv("LL64_EXIT_CLEAN"))
    {
        while (nAtexit-- > 0)
            atexits[nAtexit].fn(atexits[nAtexit].arg);
        lxRunFini();
    }
    fflush(NULL);
    ExitProcess(code);
}

static SYSV void lx_exit(int code) { lxExit(code); }
static SYSV void lx__exit(int code) { fflush(NULL); ExitProcess(code); }
static SYSV void lx_abort(void)
{
    lxLog("abort()\n");
    fflush(NULL);
    ExitProcess(134);
}
static SYSV void lx_assert_fail(const char *a, const char *file, unsigned line, const char *fn)
{
    lxLog("assertion failed: %s (%s:%u, %s)\n", a, file, line, fn ? fn : "?");
    lx_abort();
}
static SYSV void lx_stack_chk_fail(void)
{
    lxLog("stack smashing detected (caller %p)\n", __builtin_return_address(0));
    lx_abort();
}
static SYSV void lx_chk_fail(void)
{
    lxLog("buffer overflow detected\n");
    lx_abort();
}

void lxRunExecutableInit(LxModule *exe);
extern LxModule *lxExe;

typedef SYSV int (*MainFn)(int, char **, char **);
typedef SYSV void (*CsuInit)(int, char **, char **);

static SYSV int lx_libc_start_main(MainFn mainFn, int argc, char **argv, CsuInit init, void *fini, void *rtldFini,
                                   void *stackEnd)
{
    (void)fini;
    (void)rtldFini;
    (void)stackEnd;
    lxLog("__libc_start_main: main %p\n", (void *)mainFn);
    if (init)
        init(argc, argv, lxEnviron);
    else
        lxRunExecutableInit(lxExe);
    lxExit(mainFn(argc, argv, lxEnviron));
}

// ---------------------------------------------------------------------------
// Time.

typedef struct
{
    int64_t tv_sec, tv_nsec;
} LxTimespec;

typedef struct
{
    int64_t tv_sec, tv_usec;
} LxTimeval;

typedef struct
{
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    int64_t tm_gmtoff;
    const char *tm_zone;
} LxTm;

static LARGE_INTEGER qpcFreq, qpcStart;

static void realtime(LxTimespec *ts)
{
    FILETIME ft;
    uint64_t t;
    GetSystemTimePreciseAsFileTime(&ft);
    t = ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) - 116444736000000000ull;
    ts->tv_sec = (int64_t)(t / 10000000);
    ts->tv_nsec = (int64_t)(t % 10000000) * 100;
}

static void monotonic(LxTimespec *ts)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // The boot time Linux's monotonic clock starts at is far away: a day in.
    uint64_t t = (uint64_t)(now.QuadPart - qpcStart.QuadPart), f = (uint64_t)qpcFreq.QuadPart;
    static int64_t offset = -1;
    if (offset < 0)
        offset = getenv("LL64_MONO0") ? 0 : 86400;
    ts->tv_sec = (int64_t)(t / f) + offset;
    ts->tv_nsec = (int64_t)((t % f) * 1000000000ull / f);
}

static SYSV int lx_clock_gettime(int clk, LxTimespec *ts)
{
    if (clk == 0 || clk == 5)
        realtime(ts);
    else if (clk == 2 || clk == 3)
    {
        FILETIME c, e, k, u;
        uint64_t t;
        if (clk == 2)
            GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
        else
            GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u);
        t = ((uint64_t)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((uint64_t)u.dwHighDateTime << 32 | u.dwLowDateTime);
        ts->tv_sec = (int64_t)(t / 10000000);
        ts->tv_nsec = (int64_t)(t % 10000000) * 100;
    }
    else
        monotonic(ts);
    return 0;
}

static SYSV int lx_clock_getres(int clk, LxTimespec *ts)
{
    (void)clk;
    if (ts)
        ts->tv_sec = 0, ts->tv_nsec = 100;
    return 0;
}

static SYSV int lx_gettimeofday(LxTimeval *tv, void *tz)
{
    LxTimespec ts;
    (void)tz;
    realtime(&ts);
    if (tv)
        tv->tv_sec = ts.tv_sec, tv->tv_usec = ts.tv_nsec / 1000;
    return 0;
}

static SYSV int64_t lx_time(int64_t *t)
{
    LxTimespec ts;
    realtime(&ts);
    if (t)
        *t = ts.tv_sec;
    return ts.tv_sec;
}

static SYSV lx_long lx_clock(void)
{
    LxTimespec ts;
    lx_clock_gettime(2, &ts);
    return ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// Sleeps of a millisecond or more from the waitable timer (high resolution
// where Windows has it).
void lxSleepNs(int64_t ns)
{
    static __thread HANDLE timer;
    LARGE_INTEGER due;
    if (ns <= 0)
    {
        SwitchToThread();
        return;
    }
    if (!timer)
    {
        timer = CreateWaitableTimerExW(NULL, NULL, 2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
        if (!timer)
            timer = CreateWaitableTimerW(NULL, TRUE, NULL);
    }
    due.QuadPart = -(ns / 100 ? ns / 100 : 1);
    if (timer && SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
        WaitForSingleObject(timer, INFINITE);
    else
        Sleep((DWORD)(ns / 1000000));
}

static SYSV int lx_nanosleep(const LxTimespec *req, LxTimespec *rem)
{
    lxSleepNs(req->tv_sec * 1000000000 + req->tv_nsec);
    if (rem)
        rem->tv_sec = rem->tv_nsec = 0;
    return 0;
}

static SYSV int lx_clock_nanosleep(int clk, int flags, const LxTimespec *req, LxTimespec *rem)
{
    if (flags & 1)   // TIMER_ABSTIME
    {
        LxTimespec now;
        lx_clock_gettime(clk, &now);
        lxSleepNs((req->tv_sec - now.tv_sec) * 1000000000 + (req->tv_nsec - now.tv_nsec));
        return 0;
    }
    return lx_nanosleep(req, rem);
}

static SYSV int lx_usleep(unsigned us) { lxSleepNs((int64_t)us * 1000); return 0; }
static SYSV unsigned lx_sleep(unsigned s) { Sleep(s * 1000); return 0; }
static SYSV int lx_sched_yield(void) { SwitchToThread(); return 0; }

static void tmToLinux(const struct tm *w, LxTm *l, int64_t gmtoff, const char *zone)
{
    l->tm_sec = w->tm_sec;
    l->tm_min = w->tm_min;
    l->tm_hour = w->tm_hour;
    l->tm_mday = w->tm_mday;
    l->tm_mon = w->tm_mon;
    l->tm_year = w->tm_year;
    l->tm_wday = w->tm_wday;
    l->tm_yday = w->tm_yday;
    l->tm_isdst = w->tm_isdst;
    l->tm_gmtoff = gmtoff;
    l->tm_zone = zone;
}

static void tmFromLinux(const LxTm *l, struct tm *w)
{
    memset(w, 0, sizeof(*w));
    w->tm_sec = l->tm_sec;
    w->tm_min = l->tm_min;
    w->tm_hour = l->tm_hour;
    w->tm_mday = l->tm_mday;
    w->tm_mon = l->tm_mon;
    w->tm_year = l->tm_year;
    w->tm_wday = l->tm_wday;
    w->tm_yday = l->tm_yday;
    w->tm_isdst = l->tm_isdst;
}

static SYSV LxTm *lx_localtime_r(const int64_t *t, LxTm *out)
{
    struct tm w;
    __time64_t tt = *t;
    if (_localtime64_s(&w, &tt))
        return NULL;
    tmToLinux(&w, out, (int64_t)(_mkgmtime64(&w) - tt), "LOC");
    return out;
}
static SYSV LxTm *lx_gmtime_r(const int64_t *t, LxTm *out)
{
    struct tm w;
    __time64_t tt = *t;
    if (_gmtime64_s(&w, &tt))
        return NULL;
    tmToLinux(&w, out, 0, "GMT");
    return out;
}
static SYSV LxTm *lx_localtime(const int64_t *t) { static __thread LxTm tm; return lx_localtime_r(t, &tm); }
static SYSV LxTm *lx_gmtime(const int64_t *t) { static __thread LxTm tm; return lx_gmtime_r(t, &tm); }
static SYSV int64_t lx_mktime(LxTm *l)
{
    struct tm w;
    int64_t t;
    tmFromLinux(l, &w);
    t = _mktime64(&w);
    if (t != -1)
        lx_localtime_r(&t, l);
    return t;
}
static SYSV int64_t lx_timegm(LxTm *l)
{
    struct tm w;
    tmFromLinux(l, &w);
    return _mkgmtime64(&w);
}
static SYSV size_t lx_strftime(char *s, size_t max, const char *fmt, const LxTm *l)
{
    struct tm w;
    char f[512];
    size_t o = 0;
    tmFromLinux(l, &w);
    // The C99 conversions MSVCRT lacks, spelled out.
    for (const char *p = fmt; *p && o + 16 < sizeof(f); p++)
    {
        if (p[0] == '%' && p[1])
        {
            const char *rep = NULL;
            switch (p[1])
            {
            case 'T': rep = "%H:%M:%S"; break;
            case 'D': rep = "%m/%d/%y"; break;
            case 'F': rep = "%Y-%m-%d"; break;
            case 'R': rep = "%H:%M"; break;
            case 'e': rep = "%d"; break;
            case 'k': rep = "%H"; break;
            case 'l': rep = "%I"; break;
            case 'n': rep = "\n"; break;
            case 't': rep = "\t"; break;
            case 's': rep = ""; break;
            case 'z': rep = "+0000"; break;
            case 'h': rep = "%b"; break;
            case 'P': rep = "%p"; break;
            case 'E': case 'O': p++; continue;
            }
            if (rep)
            {
                o += (size_t)snprintf(f + o, sizeof(f) - o, "%s", rep);
                p++;
                continue;
            }
            f[o++] = *p++;
        }
        f[o++] = *p;
    }
    f[o] = 0;
    return strftime(s, max, f, &w);
}
static SYSV size_t lx_strftime_l(char *s, size_t max, const char *fmt, const LxTm *l, void *loc)
{
    (void)loc;
    return lx_strftime(s, max, fmt, l);
}
static SYSV char *lx_asctime_r(const LxTm *l, char *buf)
{
    struct tm w;
    tmFromLinux(l, &w);
    strftime(buf, 26, "%a %b %d %H:%M:%S %Y\n", &w);
    return buf;
}
static SYSV void lx_tzset(void) { _tzset(); }

// ---------------------------------------------------------------------------
// Processes and the system.

static SYSV int lx_getpid(void) { return (int)GetCurrentProcessId(); }
static SYSV int lx_getppid(void) { return 1; }
static SYSV unsigned lx_getuid(void) { return 1000; }
static SYSV int lx_gettid(void) { return (int)GetCurrentThreadId(); }

typedef struct
{
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
} LxUtsname;

static SYSV int lx_uname(LxUtsname *u)
{
    DWORD n = sizeof(u->nodename);
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname, "Linux");
    if (!GetComputerNameA(u->nodename, &n))
        strcpy(u->nodename, "cabinet");
    strcpy(u->release, "4.15.0");
    strcpy(u->version, "#1 SMP");
    strcpy(u->machine, "x86_64");
    return 0;
}

static SYSV int lx_gethostname(char *name, size_t n)
{
    DWORD l = (DWORD)n;
    if (!GetComputerNameA(name, &l))
        snprintf(name, n, "cabinet");
    return 0;
}

static int cpuCount(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
}

static SYSV lx_long lx_sysconf(int name)
{
    MEMORYSTATUSEX ms;
    switch (name)
    {
    case 2: return 100;            // _SC_CLK_TCK
    case 30: return 4096;          // _SC_PAGESIZE
    case 83: case 84: return cpuCount();   // _SC_NPROCESSORS_CONF/ONLN
    case 85: case 86:              // _SC_PHYS_PAGES, _SC_AVPHYS_PAGES
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        return (lx_long)((name == 85 ? ms.ullTotalPhys : ms.ullAvailPhys) / 4096);
    case 4: return 1024;           // _SC_OPEN_MAX
    case 0: return 2097152;        // _SC_ARG_MAX
    case 3: return 32;             // _SC_NGROUPS_MAX
    default: return -1;
    }
}
static SYSV int lx_get_nprocs(void) { return cpuCount(); }
static SYSV int lx_getpagesize(void) { return 4096; }

static SYSV int lx_sched_getaffinity(int pid, size_t size, uint8_t *mask)
{
    (void)pid;
    memset(mask, 0, size);
    for (int i = 0; i < cpuCount() && i / 8 < (int)size; i++)
        mask[i / 8] |= 1 << (i % 8);
    return 0;
}

static SYSV int lx_system(const char *cmd)
{
    lxLog("system(\"%s\") ignored\n", cmd ? cmd : "(null)");
    return cmd ? 0 : 1;
}

// The game's shell commands do not run here: popen reads nothing.
static SYSV FILE *lx_popen(const char *cmd, const char *mode)
{
    lxLog("popen(\"%s\") reads nothing\n", cmd);
    (void)mode;
    return tmpfile();
}
static SYSV int lx_pclose(FILE *f) { fclose(f); return 0; }

static SYSV int lx_kill(int pid, int sig) { (void)pid; (void)sig; return 0; }
static SYSV int lx_raise(int sig) { lxLog("raise(%d)\n", sig); if (sig == 6) lx_abort(); return 0; }
static SYSV int lx_zero(void) { return 0; }
static SYSV int lx_minus1(void) { lxSetErrno(38); return -1; }
static SYSV void *lx_null(void) { return NULL; }

// ---------------------------------------------------------------------------
// Files.

static int openFlags(int f)
{
    int w = _O_BINARY | (f & 3);
    if (f & 0x40) w |= _O_CREAT;
    if (f & 0x80) w |= _O_EXCL;
    if (f & 0x200) w |= _O_TRUNC;
    if (f & 0x400) w |= _O_APPEND;
    return w;
}

static int isDir(const char *p)
{
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Directory descriptors: Windows has none, open() of a directory hands one of
// these (fstat, fchdir, openat).
#define DIRFD_BASE 100000
#define MAX_DIRFDS 64
static char *dirFds[MAX_DIRFDS];

// /dev/urandom: reads random bytes.
#define RANDOM_FD 300001

int lxDspIsPath(const char *path);
int lxDspIsFd(int fd);
int lxDspOpen(void);
int lxDspClose(void);
int64_t lxDspWrite(const void *buf, size_t n);
int lxDspIoctl(unsigned req, void *arg);

SYSV int lxOpen(const char *path, int flags, int mode)
{
    char buf[MAX_PATH];
    const char *p;
    int fd;
    if (lxDspIsPath(path))
        return lxDspOpen();
    if (!strcmp(path, "/dev/urandom") || !strcmp(path, "/dev/random"))
        return RANDOM_FD;
    p = lxMapPath(path, buf, sizeof(buf));
    if (!p)
    {
        lxSetErrno(2);
        return -1;
    }
    if (isDir(p))
    {
        for (int i = 0; i < MAX_DIRFDS; i++)
            if (!dirFds[i])
            {
                dirFds[i] = _strdup(p);
                return DIRFD_BASE + i;
            }
        lxSetErrno(24);
        return -1;
    }
    fd = _open(p, openFlags(flags), mode ? _S_IREAD | _S_IWRITE : 0);
    if (fd < 0)
        lxSetErrnoFromCrt();
    lxTraceLog("open %s -> %d\n", path, fd);
    return fd;
}

static SYSV int lx_open(const char *path, int flags, ...)
{
    int mode = 0;
    if (flags & 0x40)
    {
        __builtin_sysv_va_list ap;
        __builtin_sysv_va_start(ap, flags);
        mode = __builtin_va_arg(ap, int);
        __builtin_sysv_va_end(ap);
    }
    return lxOpen(path, flags, mode);
}
static SYSV int lx_open_2(const char *path, int flags) { return lxOpen(path, flags, 0); }
static SYSV int lx_openat(int dirfd, const char *path, int flags, ...)
{
    char full[MAX_PATH];
    int mode = 0;
    if (flags & 0x40)
    {
        __builtin_sysv_va_list ap;
        __builtin_sysv_va_start(ap, flags);
        mode = __builtin_va_arg(ap, int);
        __builtin_sysv_va_end(ap);
    }
    if (path[0] != '/' && dirfd >= DIRFD_BASE && dirfd < DIRFD_BASE + MAX_DIRFDS && dirFds[dirfd - DIRFD_BASE])
    {
        snprintf(full, sizeof(full), "%s/%s", dirFds[dirfd - DIRFD_BASE], path);
        return lxOpen(full, flags, mode);
    }
    return lxOpen(path, flags, mode);
}
static SYSV int lx_openat_2(int dirfd, const char *path, int flags) { return lx_openat(dirfd, path, flags, 0); }

static SYSV int lx_close(int fd)
{
    if (lxDspIsFd(fd))
        return lxDspClose();
    if (fd == RANDOM_FD)
        return 0;
    if (fd >= DIRFD_BASE && fd < DIRFD_BASE + MAX_DIRFDS)
    {
        free(dirFds[fd - DIRFD_BASE]);
        dirFds[fd - DIRFD_BASE] = NULL;
        return 0;
    }
    if (fd < 0)
        return -1;
    return _close(fd);
}

static SYSV int lx_getentropy(void *buf, size_t n);
static SYSV int64_t lx_read(int fd, void *buf, size_t n)
{
    if (fd == RANDOM_FD)
    {
        lx_getentropy(buf, n);
        return (int64_t)n;
    }
    int r = _read(fd, buf, (unsigned)(n > 0x7fffffff ? 0x7fffffff : n));
    if (r < 0)
        lxSetErrnoFromCrt();
    return r;
}
static SYSV int64_t lx_read_chk(int fd, void *buf, size_t n, size_t bn) { (void)bn; return lx_read(fd, buf, n); }
static SYSV int64_t lx_write(int fd, const void *buf, size_t n)
{
    int r;
    if (lxDspIsFd(fd))
        return lxDspWrite(buf, n);
    if (fd == 1 || fd == 2)
    {
        fwrite(buf, 1, n, fd == 1 ? stdout : stderr);
        fflush(fd == 1 ? stdout : stderr);
        return (int64_t)n;
    }
    r = _write(fd, buf, (unsigned)(n > 0x7fffffff ? 0x7fffffff : n));
    if (r < 0)
        lxSetErrnoFromCrt();
    return r;
}
typedef struct
{
    void *base;
    size_t len;
} LxIovec;
static SYSV int64_t lx_writev(int fd, const LxIovec *iov, int n)
{
    int64_t t = 0;
    for (int i = 0; i < n; i++)
    {
        int64_t r = lx_write(fd, iov[i].base, iov[i].len);
        if (r < 0)
            return t ? t : r;
        t += r;
    }
    return t;
}
static SYSV int64_t lx_lseek(int fd, int64_t off, int whence)
{
    int64_t r = _lseeki64(fd, off, whence);
    if (r < 0)
        lxSetErrnoFromCrt();
    return r;
}
static SYSV int lx_fsync(int fd) { (void)fd; return 0; }
static SYSV int lx_ftruncate(int fd, int64_t n) { return _chsize_s(fd, n) ? -1 : 0; }
static SYSV int lx_dup(int fd) { return _dup(fd); }
static SYSV int lx_dup2(int a, int b) { return _dup2(a, b) ? -1 : b; }
static SYSV int lx_pipe(int fds[2]) { return _pipe(fds, 65536, _O_BINARY); }
static SYSV int lx_isatty(int fd) { return _isatty(fd) ? 1 : 0; }
static SYSV int lx_ioctl(int fd, unsigned long req, ...)
{
    if (lxDspIsFd(fd))
    {
        __builtin_sysv_va_list ap;
        void *arg;
        __builtin_sysv_va_start(ap, req);
        arg = __builtin_va_arg(ap, void *);
        __builtin_sysv_va_end(ap);
        return lxDspIoctl((unsigned)req, arg);
    }
    lxTraceLog("ioctl(%d, %#lx) refused\n", fd, req);
    lxSetErrno(25);
    return -1;
}
static SYSV int lx_fcntl(int fd, int cmd, ...)
{
    (void)fd;
    if (cmd == 0 || cmd == 1030)   // F_DUPFD, F_DUPFD_CLOEXEC
        return _dup(fd);
    return 0;
}
static SYSV int lx_flock(int fd, int op) { (void)fd; (void)op; return 0; }

// struct stat, glibc x86-64.
typedef struct
{
    uint64_t st_dev, st_ino, st_nlink;
    uint32_t st_mode, st_uid, st_gid, pad0;
    uint64_t st_rdev;
    int64_t st_size, st_blksize, st_blocks;
    LxTimespec st_atim, st_mtim, st_ctim;
    int64_t reserved[3];
} LxStat;

static void statToLinux(const struct _stat64 *w, LxStat *l)
{
    memset(l, 0, sizeof(*l));
    l->st_dev = w->st_dev;
    l->st_ino = 1;
    l->st_nlink = 1;
    l->st_mode = w->st_mode;
    if ((w->st_mode & 0xf000) == 0x4000)
        l->st_mode |= 0755;
    else if ((w->st_mode & 0xf000) == 0x8000)
        l->st_mode |= 0644;
    l->st_uid = l->st_gid = 1000;
    l->st_size = w->st_size;
    l->st_blksize = 4096;
    l->st_blocks = (w->st_size + 511) / 512;
    l->st_atim.tv_sec = w->st_atime;
    l->st_mtim.tv_sec = w->st_mtime;
    l->st_ctim.tv_sec = w->st_ctime;
}

SYSV int lxStat(const char *path, LxStat *st)
{
    char buf[MAX_PATH];
    struct _stat64 w;
    const char *p = lxMapPath(path, buf, sizeof(buf));
    char trimmed[MAX_PATH];
    size_t n;
    if (lxDspIsPath(path))
    {
        memset(st, 0, sizeof(*st));
        st->st_mode = 0020666;   // a character device
        return 0;
    }
    if (!p)
    {
        lxSetErrno(2);
        return -1;
    }
    // _stat64 refuses a directory with a trailing slash.
    snprintf(trimmed, sizeof(trimmed), "%s", p);
    n = strlen(trimmed);
    while (n > 3 && (trimmed[n - 1] == '/' || trimmed[n - 1] == '\\'))
        trimmed[--n] = 0;
    if (_stat64(trimmed, &w))
    {
        lxSetErrnoFromCrt();
        return -1;
    }
    statToLinux(&w, st);
    return 0;
}

static SYSV int lx_fstat(int fd, LxStat *st)
{
    struct _stat64 w;
    if (fd >= DIRFD_BASE && fd < DIRFD_BASE + MAX_DIRFDS && dirFds[fd - DIRFD_BASE])
        return lxStat(dirFds[fd - DIRFD_BASE], st);
    if (_fstat64(fd, &w))
    {
        lxSetErrnoFromCrt();
        return -1;
    }
    statToLinux(&w, st);
    return 0;
}
static SYSV int lx_xstat(int v, const char *p, LxStat *st) { (void)v; return lxStat(p, st); }
static SYSV int lx_fxstat(int v, int fd, LxStat *st) { (void)v; return lx_fstat(fd, st); }
static SYSV int lx_fstatat(int dirfd, const char *p, LxStat *st, int flags)
{
    (void)dirfd;
    (void)flags;
    return lxStat(p, st);
}

typedef struct
{
    int64_t f_type, f_bsize;
    uint64_t f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int32_t f_fsid[2];
    int64_t f_namelen, f_frsize, f_flags, f_spare[4];
} LxStatfs;
static SYSV int lx_statfs(const char *p, LxStatfs *s)
{
    (void)p;
    memset(s, 0, sizeof(*s));
    s->f_type = 0xef53;
    s->f_bsize = s->f_frsize = 4096;
    s->f_blocks = 1 << 24;
    s->f_bfree = s->f_bavail = 1 << 23;
    s->f_namelen = 255;
    return 0;
}
typedef struct
{
    uint64_t f_bsize, f_frsize, f_blocks, f_bfree, f_bavail, f_files, f_ffree, f_favail, f_fsid, f_flag, f_namemax;
    int32_t spare[6];
} LxStatvfs;
static SYSV int lx_statvfs(const char *p, LxStatvfs *s)
{
    (void)p;
    memset(s, 0, sizeof(*s));
    s->f_bsize = s->f_frsize = 4096;
    s->f_blocks = 1 << 24;
    s->f_bfree = s->f_bavail = 1 << 23;
    s->f_namemax = 255;
    return 0;
}

#define MAPPED(path, call)                                                                                             \
    char buf[MAX_PATH];                                                                                                \
    const char *p = lxMapPath(path, buf, sizeof(buf));                                                                 \
    int r;                                                                                                             \
    if (!p)                                                                                                            \
    {                                                                                                                  \
        lxSetErrno(2);                                                                                                 \
        return -1;                                                                                                     \
    }                                                                                                                  \
    r = call;                                                                                                          \
    if (r < 0)                                                                                                         \
        lxSetErrnoFromCrt();                                                                                           \
    return r;

static SYSV int lx_access(const char *path, int mode) { MAPPED(path, _access(p, mode & 6)) }
static SYSV int lx_unlink(const char *path) { MAPPED(path, _unlink(p)) }
static SYSV int lx_rmdir(const char *path) { MAPPED(path, _rmdir(p)) }
static SYSV int lx_mkdir(const char *path, unsigned mode) { (void)mode; MAPPED(path, _mkdir(p)) }
static SYSV int lx_chdir(const char *path) { MAPPED(path, _chdir(p)) }
static SYSV int lx_chmod(const char *path, unsigned mode) { (void)mode; MAPPED(path, _access(p, 0)) }
static SYSV int lx_remove(const char *path) { MAPPED(path, isDir(p) ? _rmdir(p) : _unlink(p)) }
static SYSV int lx_truncate(const char *path, int64_t n)
{
    char buf[MAX_PATH];
    const char *p = lxMapPath(path, buf, sizeof(buf));
    int fd = p ? _open(p, _O_RDWR | _O_BINARY) : -1, r;
    if (fd < 0)
        return -1;
    r = _chsize_s(fd, n) ? -1 : 0;
    _close(fd);
    return r;
}
static SYSV int lx_rename(const char *a, const char *b)
{
    char ba[MAX_PATH], bb[MAX_PATH];
    const char *pa = lxMapPath(a, ba, sizeof(ba)), *pb = lxMapPath(b, bb, sizeof(bb));
    if (!pa || !pb)
        return -1;
    if (MoveFileExA(pa, pb, MOVEFILE_REPLACE_EXISTING))
        return 0;
    lxSetErrno(2);
    return -1;
}
static SYSV char *lx_getcwd(char *buf, size_t n)
{
    char w[MAX_PATH];
    if (!_getcwd(w, sizeof(w)))
        return NULL;
    for (char *c = w; *c; c++)
        if (*c == '\\')
            *c = '/';
    if (!buf)
        return lxStrdup(w);
    if (strlen(w) + 1 > n)
    {
        lxSetErrno(34);
        return NULL;
    }
    strcpy(buf, w);
    return buf;
}
static SYSV int64_t lx_readlink(const char *path, char *buf, size_t n)
{
    if (!strcmp(path, "/proc/self/exe"))
    {
        const char *exe = "/pm/g7/game";
        size_t l = strlen(exe);
        if (l > n)
            l = n;
        memcpy(buf, exe, l);
        return (int64_t)l;
    }
    lxSetErrno(22);
    return -1;
}
static SYSV char *lx_realpath(const char *path, char *out)
{
    char buf[MAX_PATH], full[MAX_PATH];
    const char *p = lxMapPath(path, buf, sizeof(buf));
    if (!p || !_fullpath(full, p, sizeof(full)) || GetFileAttributesA(full) == INVALID_FILE_ATTRIBUTES)
    {
        lxSetErrno(2);
        return NULL;
    }
    // The game's own spelling, for the paths it built.
    snprintf(full, sizeof(full), "%s", path);
    if (!out)
        return lxStrdup(full);
    strcpy(out, full);
    return out;
}
static SYSV char *lx_realpath_chk(const char *path, char *out, size_t n) { (void)n; return lx_realpath(path, out); }
static SYSV unsigned lx_umask(unsigned m) { (void)m; return 022; }

static SYSV int lx_mkostemp(char *tmpl, int flags)
{
    (void)flags;
    if (_mktemp_s(tmpl, strlen(tmpl) + 1))
        return -1;
    return lxOpen(tmpl, 0x42 | 0x80, 0600);
}
static SYSV int lx_mkstemp(char *tmpl) { return lx_mkostemp(tmpl, 0); }

// Directories: glibc's DIR is opaque, its dirent64 not.
typedef struct
{
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
} LxDirent;

typedef struct
{
    HANDLE find;
    WIN32_FIND_DATAA data;
    int first, fd;
    LxDirent ent;
    char path[MAX_PATH];
} LxDir;

static SYSV LxDir *lx_opendir(const char *path)
{
    char buf[MAX_PATH], pattern[MAX_PATH];
    const char *p = lxMapPath(path, buf, sizeof(buf));
    LxDir *d;
    if (!p || !isDir(p))
    {
        lxSetErrno(2);
        return NULL;
    }
    d = calloc(1, sizeof(*d));
    snprintf(d->path, sizeof(d->path), "%s", p);
    snprintf(pattern, sizeof(pattern), "%s\\*", p);
    d->find = FindFirstFileA(pattern, &d->data);
    d->first = 1;
    d->fd = -1;
    return d;
}
static SYSV LxDir *lx_fdopendir(int fd)
{
    if (fd >= DIRFD_BASE && fd < DIRFD_BASE + MAX_DIRFDS && dirFds[fd - DIRFD_BASE])
    {
        LxDir *d = lx_opendir(dirFds[fd - DIRFD_BASE]);
        if (d)
            d->fd = fd;
        return d;
    }
    lxSetErrno(9);
    return NULL;
}
static SYSV LxDirent *lx_readdir(LxDir *d)
{
    if (!d || d->find == INVALID_HANDLE_VALUE)
        return NULL;
    if (!d->first && !FindNextFileA(d->find, &d->data))
        return NULL;
    d->first = 0;
    memset(&d->ent, 0, sizeof(d->ent));
    d->ent.d_ino = 1;
    d->ent.d_reclen = sizeof(LxDirent);
    d->ent.d_type = (d->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 4 : 8;
    snprintf(d->ent.d_name, sizeof(d->ent.d_name), "%s", d->data.cFileName);
    return &d->ent;
}
static SYSV int lx_closedir(LxDir *d)
{
    if (!d)
        return -1;
    if (d->find != INVALID_HANDLE_VALUE)
        FindClose(d->find);
    if (d->fd >= 0)
        lx_close(d->fd);
    free(d);
    return 0;
}
static SYSV int lx_dirfd(LxDir *d)
{
    if (d->fd < 0)
        d->fd = lxOpen(d->path, 0, 0);
    return d->fd;
}
static SYSV void lx_rewinddir(LxDir *d)
{
    char pattern[MAX_PATH];
    if (d->find != INVALID_HANDLE_VALUE)
        FindClose(d->find);
    snprintf(pattern, sizeof(pattern), "%s\\*", d->path);
    d->find = FindFirstFileA(pattern, &d->data);
    d->first = 1;
}

// mmap: anonymous memory from VirtualAlloc, files read into it.
static SYSV void *lx_mmap(void *addr, size_t len, int prot, int flags, int fd, int64_t off)
{
    void *p = NULL;
    (void)prot;
    if (flags & 0x10)   // MAP_FIXED
        p = VirtualAlloc(addr, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!p)
        p = VirtualAlloc(flags & 0x10 ? NULL : addr, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!p)
        p = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!p)
    {
        lxSetErrno(12);
        return (void *)-1;
    }
    if (!(flags & 0x20) && fd >= 0)
    {
        int64_t at = _telli64(fd);
        _lseeki64(fd, off, SEEK_SET);
        _read(fd, p, (unsigned)len);
        _lseeki64(fd, at, SEEK_SET);
    }
    return p;
}
static SYSV int lx_munmap(void *p, size_t len)
{
    (void)len;
    VirtualFree(p, 0, MEM_RELEASE);
    return 0;
}
static SYSV int lx_mprotect(void *p, size_t len, int prot) { (void)p; (void)len; (void)prot; return 0; }

// getauxval
static SYSV lx_ulong lx_getauxval(lx_ulong type) { return type == 6 ? 4096 : 0; }

static int libcInitDone;
static void libcInitOnce(void)
{
    if (libcInitDone++)
        return;
    ctypeInit();
    envInit();
    InitializeCriticalSection(&exitLock);
    InitializeCriticalSection(&randLock);
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcStart);
}

static char libcSingleThreaded = 0;

static const LxSym syms[] = {
    {"__errno_location", lx_errno_location},
    {"malloc", lxMalloc}, {"free", lxFree}, {"calloc", lxCalloc}, {"realloc", lxRealloc},
    {"memalign", lxMemalign}, {"posix_memalign", lx_posix_memalign}, {"aligned_alloc", lx_aligned_alloc},
    {"valloc", lx_valloc}, {"malloc_usable_size", lx_malloc_usable_size}, {"cfree", lxFree},
    {"strdup", lxStrdup}, {"__strdup", lxStrdup}, {"strndup", lx_strndup},
    {"memcpy", lx_memcpy}, {"__memcpy_chk", lx_memcpy_chk}, {"memmove", lx_memmove}, {"__memmove_chk", lx_memmove_chk},
    {"memset", lx_memset}, {"__memset_chk", lx_memset_chk}, {"memcmp", lx_memcmp}, {"bcmp", lx_memcmp},
    {"memchr", lx_memchr}, {"memrchr", lx_memrchr}, {"mempcpy", lx_mempcpy}, {"__mempcpy", lx_mempcpy},
    {"strlen", lx_strlen}, {"strnlen", lx_strnlen}, {"strcpy", lx_strcpy}, {"__strcpy_chk", lx_strcpy_chk},
    {"stpcpy", lx_stpcpy}, {"__stpcpy_chk", lx_stpcpy_chk}, {"strncpy", lx_strncpy},
    {"__strncpy_chk", lx_strncpy_chk}, {"strcat", lx_strcat}, {"__strcat_chk", lx_strcat_chk},
    {"strncat", lx_strncat}, {"__strncat_chk", lx_strncat_chk}, {"strcmp", lx_strcmp}, {"strncmp", lx_strncmp},
    {"strcasecmp", lx_strcasecmp}, {"strncasecmp", lx_strncasecmp}, {"strcoll", lx_strcoll},
    {"__strcoll_l", lx_strcoll_l}, {"strcoll_l", lx_strcoll_l}, {"strxfrm", lx_strxfrm},
    {"__strxfrm_l", lx_strxfrm_l}, {"strxfrm_l", lx_strxfrm_l}, {"strchr", lx_strchr}, {"index", lx_strchr},
    {"strrchr", lx_strrchr}, {"rindex", lx_strrchr}, {"strchrnul", lx_strchrnul}, {"strstr", lx_strstr},
    {"strcasestr", lx_strcasestr}, {"strpbrk", lx_strpbrk}, {"strspn", lx_strspn}, {"strcspn", lx_strcspn},
    {"strtok", lx_strtok}, {"strtok_r", lx_strtok_r}, {"__strtok_r", lx_strtok_r}, {"strsep", lx_strsep},
    {"__ctype_b_loc", lx_ctype_b_loc}, {"__ctype_tolower_loc", lx_ctype_tolower_loc},
    {"__ctype_toupper_loc", lx_ctype_toupper_loc}, {"__ctype_get_mb_cur_max", lx_ctype_get_mb_cur_max},
    {"tolower", lx_tolower}, {"toupper", lx_toupper}, {"isspace", lx_isspace}, {"isdigit", lx_isdigit},
    {"isalpha", lx_isalpha}, {"isalnum", lx_isalnum}, {"isupper", lx_isupper}, {"islower", lx_islower},
    {"isxdigit", lx_isxdigit}, {"isprint", lx_isprint},
    {"strtol", lx_strtol}, {"__isoc23_strtol", lx_strtol}, {"strtoul", lx_strtoul},
    {"__isoc23_strtoul", lx_strtoul}, {"strtoll", lx_strtoll}, {"__isoc23_strtoll", lx_strtoll},
    {"strtoull", lx_strtoull}, {"__isoc23_strtoull", lx_strtoull}, {"strtoq", lx_strtoll},
    {"strtouq", lx_strtoull}, {"strtod", lx_strtod}, {"strtof", lx_strtof}, {"strtold", lx_strtold},
    {"__strtod_l", lx_strtod_l}, {"strtod_l", lx_strtod_l}, {"__strtof_l", lx_strtof_l},
    {"strtof_l", lx_strtof_l}, {"__strtold_l", lx_strtold_l}, {"strtold_l", lx_strtold_l},
    {"atoi", lx_atoi}, {"atol", lx_atol}, {"atoll", lx_atoll}, {"atof", lx_atof}, {"abs", lx_abs},
    {"labs", lx_labs}, {"llabs", lx_labs},
    {"rand", lx_rand}, {"srand", lx_srand}, {"random", lx_rand}, {"srandom", lx_srand},
    {"rand_r", lx_rand_r}, {"arc4random", lx_arc4random}, {"getentropy", lx_getentropy},
    {"qsort", lx_qsort}, {"bsearch", lx_bsearch},
    {"getenv", lxGetenv}, {"secure_getenv", lxGetenv}, {"__secure_getenv", lxGetenv}, {"setenv", lx_setenv},
    {"unsetenv", lx_unsetenv}, {"putenv", lx_putenv},
    {"__cxa_atexit", lx_cxa_atexit}, {"atexit", lx_atexit}, {"__cxa_thread_atexit_impl", lx_cxa_thread_atexit_impl},
    {"__cxa_finalize", lx_cxa_finalize}, {"exit", lx_exit}, {"_exit", lx__exit}, {"_Exit", lx__exit},
    {"quick_exit", lx__exit}, {"abort", lx_abort}, {"__assert_fail", lx_assert_fail},
    {"__stack_chk_fail", lx_stack_chk_fail}, {"__chk_fail", lx_chk_fail}, {"__fortify_fail", lx_chk_fail},
    {"__libc_start_main", lx_libc_start_main},
    {"clock_gettime", lx_clock_gettime}, {"__clock_gettime", lx_clock_gettime}, {"clock_getres", lx_clock_getres},
    {"gettimeofday", lx_gettimeofday}, {"time", lx_time}, {"clock", lx_clock}, {"nanosleep", lx_nanosleep},
    {"clock_nanosleep", lx_clock_nanosleep}, {"usleep", lx_usleep}, {"sleep", lx_sleep},
    {"sched_yield", lx_sched_yield}, {"localtime_r", lx_localtime_r}, {"localtime", lx_localtime},
    {"gmtime_r", lx_gmtime_r}, {"gmtime", lx_gmtime}, {"mktime", lx_mktime}, {"timegm", lx_timegm},
    {"strftime", lx_strftime}, {"__strftime_l", lx_strftime_l}, {"strftime_l", lx_strftime_l},
    {"asctime_r", lx_asctime_r}, {"tzset", lx_tzset},
    {"getpid", lx_getpid}, {"getppid", lx_getppid}, {"getuid", lx_getuid}, {"geteuid", lx_getuid},
    {"getgid", lx_getuid}, {"getegid", lx_getuid}, {"gettid", lx_gettid}, {"uname", lx_uname},
    {"gethostname", lx_gethostname}, {"sysconf", lx_sysconf}, {"__sysconf", lx_sysconf},
    {"get_nprocs", lx_get_nprocs}, {"get_nprocs_conf", lx_get_nprocs}, {"getpagesize", lx_getpagesize},
    {"sched_getaffinity", lx_sched_getaffinity}, {"sched_setaffinity", lx_zero}, {"setpriority", lx_zero},
    {"nice", lx_zero}, {"system", lx_system}, {"popen", lx_popen}, {"pclose", lx_pclose}, {"kill", lx_kill},
    {"raise", lx_raise}, {"waitpid", lx_minus1}, {"fork", lx_minus1}, {"posix_spawn", lx_minus1},
    {"setsid", lx_zero}, {"alarm", lx_zero}, {"sync", lx_zero}, {"getrusage", lx_zero},
    {"getlogin_r", lx_minus1}, {"getpwuid", lx_null}, {"getpwuid_r", lx_minus1}, {"settimeofday", lx_zero},
    {"open", lx_open}, {"open64", lx_open}, {"__open_2", lx_open_2}, {"__open64_2", lx_open_2},
    {"openat", lx_openat}, {"openat64", lx_openat}, {"__openat_2", lx_openat_2}, {"close", lx_close},
    {"read", lx_read}, {"__read_chk", lx_read_chk}, {"write", lx_write}, {"writev", lx_writev},
    {"lseek", lx_lseek}, {"lseek64", lx_lseek}, {"fsync", lx_fsync}, {"fdatasync", lx_fsync},
    {"ftruncate", lx_ftruncate}, {"ftruncate64", lx_ftruncate}, {"truncate", lx_truncate}, {"dup", lx_dup},
    {"dup2", lx_dup2}, {"pipe", lx_pipe}, {"isatty", lx_isatty}, {"ioctl", lx_ioctl}, {"fcntl", lx_fcntl},
    {"fcntl64", lx_fcntl}, {"flock", lx_flock},
    {"stat", lxStat}, {"stat64", lxStat}, {"lstat", lxStat}, {"lstat64", lxStat}, {"fstat", lx_fstat},
    {"fstat64", lx_fstat}, {"__xstat", lx_xstat}, {"__xstat64", lx_xstat}, {"__lxstat", lx_xstat},
    {"__lxstat64", lx_xstat}, {"__fxstat", lx_fxstat}, {"__fxstat64", lx_fxstat}, {"fstatat", lx_fstatat},
    {"fstatat64", lx_fstatat}, {"statfs", lx_statfs}, {"statfs64", lx_statfs}, {"statvfs", lx_statvfs},
    {"statvfs64", lx_statvfs},
    {"access", lx_access}, {"unlink", lx_unlink}, {"unlinkat", lx_minus1}, {"rmdir", lx_rmdir},
    {"mkdir", lx_mkdir}, {"chdir", lx_chdir}, {"chmod", lx_chmod}, {"fchmod", lx_zero}, {"fchmodat", lx_zero},
    {"remove", lx_remove}, {"rename", lx_rename}, {"getcwd", lx_getcwd}, {"readlink", lx_readlink},
    {"realpath", lx_realpath}, {"__realpath_chk", lx_realpath_chk}, {"umask", lx_umask},
    {"symlink", lx_minus1}, {"link", lx_minus1}, {"utimensat", lx_zero}, {"mkostemp", lx_mkostemp},
    {"mkstemp", lx_mkstemp}, {"chown", lx_zero},
    {"opendir", lx_opendir}, {"fdopendir", lx_fdopendir}, {"readdir", lx_readdir}, {"readdir64", lx_readdir},
    {"closedir", lx_closedir}, {"dirfd", lx_dirfd}, {"rewinddir", lx_rewinddir},
    {"mmap", lx_mmap}, {"mmap64", lx_mmap}, {"munmap", lx_munmap}, {"mprotect", lx_mprotect},
    {"madvise", lx_zero}, {"mlock", lx_zero}, {"getauxval", lx_getauxval},
    {"__libc_single_threaded", &libcSingleThreaded},
};

static const LxSym dataSyms[] = {
    {"environ", &lxEnviron}, {"__environ", &lxEnviron}, {"_environ", &lxEnviron},
};

void lxLibcInit(void)
{
    libcInitOnce();
    lxRegister(syms, LX_COUNT(syms));
    lxRegister(dataSyms, LX_COUNT(dataSyms));
}
