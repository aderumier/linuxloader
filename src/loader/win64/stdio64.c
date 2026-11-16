// stdio: the games' FILE is the CRT's, their printf and scanf families read
// System V va_lists (see ll64.h), and their wide characters are 32 bits.
#include <windows.h>
#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ll64.h"

typedef __builtin_sysv_va_list lx_va_list;

SYSV void *lxMalloc(size_t n);
SYSV void *lxRealloc(void *p, size_t n);
SYSV void lxFree(void *p);

static FILE *lxStdin, *lxStdout, *lxStderr;

// ---------------------------------------------------------------------------
// printf

typedef struct
{
    char *buf;
    size_t cap, len;
    int grow;   // buf is ours (lxMalloc'd) and grows
} Out;

static void put(Out *o, const char *s, size_t n)
{
    if (o->grow && o->len + n + 1 > o->cap)
    {
        size_t cap = o->cap ? o->cap : 256;
        while (o->len + n + 1 > cap)
            cap *= 2;
        o->buf = lxRealloc(o->buf, cap);
        o->cap = cap;
    }
    if (o->len < o->cap)
    {
        size_t room = o->cap - o->len - 1, k = n < room ? n : room;
        memcpy(o->buf + o->len, s, k);
    }
    o->len += n;
}

// 32-bit wide characters to UTF-8.
static size_t utf8Encode(uint32_t c, char *out)
{
    if (c < 0x80) { out[0] = (char)c; return 1; }
    if (c < 0x800) { out[0] = (char)(0xc0 | c >> 6); out[1] = (char)(0x80 | (c & 0x3f)); return 2; }
    if (c < 0x10000)
    {
        out[0] = (char)(0xe0 | c >> 12); out[1] = (char)(0x80 | ((c >> 6) & 0x3f)); out[2] = (char)(0x80 | (c & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | c >> 18); out[1] = (char)(0x80 | ((c >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3f)); out[3] = (char)(0x80 | (c & 0x3f));
    return 4;
}

static char *wideToUtf8(const uint32_t *w, int prec)
{
    size_t n = 0, cap = 64;
    char *s = malloc(cap);
    for (; *w && (prec < 0 || (int)n < prec); w++)
    {
        if (n + 5 > cap)
            s = realloc(s, cap *= 2);
        n += utf8Encode(*w, s + n);
    }
    s[n] = 0;
    return s;
}

SYSV int lxVformat(Out *o, const char *fmt, lx_va_list ap)
{
    for (const char *p = fmt; *p;)
    {
        char spec[64], tmp[512], *big = NULL;
        size_t sl = 0;
        int width = -1, prec = -1, lenMod = 0, n;
        const char *start;

        if (*p != '%')
        {
            const char *q = strchr(p, '%');
            size_t k = q ? (size_t)(q - p) : strlen(p);
            put(o, p, k);
            p += k;
            continue;
        }
        start = p++;
        if (*p == '%')
        {
            put(o, "%", 1);
            p++;
            continue;
        }
        spec[sl++] = '%';
        // Positional arguments (%1$d) are not handled: taken in order.
        {
            const char *q = p;
            while (*q >= '0' && *q <= '9')
                q++;
            if (*q == '$' && q > p)
                p = q + 1;
        }
        while (strchr("-+ #0'", *p))
        {
            if (*p != '\'' && sl < 8)
                spec[sl++] = *p;
            p++;
        }
        if (*p == '*')
        {
            width = __builtin_va_arg(ap, int);
            p++;
            sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%d", width);
        }
        else
            while (*p >= '0' && *p <= '9')
                spec[sl++] = *p++;
        if (*p == '.')
        {
            p++;
            spec[sl++] = '.';
            if (*p == '*')
            {
                prec = __builtin_va_arg(ap, int);
                p++;
                if (prec >= 0)
                    sl += (size_t)snprintf(spec + sl, sizeof(spec) - sl, "%d", prec);
                else
                    sl--;
            }
            else
            {
                prec = 0;
                while (*p >= '0' && *p <= '9')
                {
                    prec = prec * 10 + (*p - '0');
                    spec[sl++] = *p++;
                }
            }
        }
        // Length: 1 hh, 2 h, 3 l, 4 ll, 5 L, 6 z/j/t
        for (;;)
        {
            if (*p == 'h') lenMod = lenMod == 2 ? 1 : 2;
            else if (*p == 'l') lenMod = lenMod == 3 ? 4 : 3;
            else if (*p == 'q') lenMod = 4;
            else if (*p == 'L') lenMod = 5;
            else if (*p == 'z' || *p == 'j' || *p == 't') lenMod = 6;
            else break;
            p++;
        }
        char conv = *p ? *p++ : 0;
        switch (conv)
        {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X':
        {
            long long v;
            if (lenMod >= 3 && lenMod != 5)
                v = __builtin_va_arg(ap, long long);
            else
            {
                int iv = __builtin_va_arg(ap, int);
                int isSigned = conv == 'd' || conv == 'i';
                if (lenMod == 1)
                    v = isSigned ? (long long)(signed char)iv : (long long)(unsigned char)iv;
                else if (lenMod == 2)
                    v = isSigned ? (long long)(short)iv : (long long)(unsigned short)iv;
                else
                    v = isSigned ? (long long)iv : (long long)(unsigned)iv;
            }
            spec[sl++] = 'l';
            spec[sl++] = 'l';
            spec[sl++] = conv;
            spec[sl] = 0;
            n = snprintf(tmp, sizeof(tmp), spec, v);
            break;
        }
        case 'c':
        {
            int c = __builtin_va_arg(ap, int);
            if (lenMod == 3)
            {
                char u[8];
                size_t k = utf8Encode((uint32_t)c, u);
                u[k] = 0;
                spec[sl++] = 's';
                spec[sl] = 0;
                n = snprintf(tmp, sizeof(tmp), spec, u);
            }
            else
            {
                spec[sl++] = 'c';
                spec[sl] = 0;
                n = snprintf(tmp, sizeof(tmp), spec, c);
            }
            break;
        }
        case 'C': case 'S': case 's':
        {
            const void *s = __builtin_va_arg(ap, const void *);
            char *conv8 = NULL;
            if (conv == 'C')
            {
                char u[8];
                size_t k = utf8Encode((uint32_t)(uintptr_t)s, u);
                u[k] = 0;
                conv8 = _strdup(u);
                s = conv8;
            }
            else if ((conv == 'S' || lenMod == 3) && s)
            {
                conv8 = wideToUtf8(s, prec);
                s = conv8;
            }
            if (!s)
                s = "(null)";
            spec[sl++] = 's';
            spec[sl] = 0;
            n = snprintf(NULL, 0, spec, (const char *)s);
            if (n >= (int)sizeof(tmp))
            {
                big = malloc((size_t)n + 1);
                snprintf(big, (size_t)n + 1, spec, (const char *)s);
            }
            else
                snprintf(tmp, sizeof(tmp), spec, (const char *)s);
            free(conv8);
            break;
        }
        case 'p':
        {
            void *v = __builtin_va_arg(ap, void *);
            if (!v)
            {
                spec[sl++] = 's';
                spec[sl] = 0;
                n = snprintf(tmp, sizeof(tmp), spec, "(nil)");
            }
            else
            {
                spec[sl++] = '#';
                spec[sl++] = 'l';
                spec[sl++] = 'l';
                spec[sl++] = 'x';
                spec[sl] = 0;
                n = snprintf(tmp, sizeof(tmp), spec, (unsigned long long)(uintptr_t)v);
            }
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            if (lenMod == 5)
            {
                long double v = __builtin_va_arg(ap, long double);
                spec[sl++] = 'L';
                spec[sl++] = conv;
                spec[sl] = 0;
                n = snprintf(NULL, 0, spec, v);
                if (n >= (int)sizeof(tmp))
                {
                    big = malloc((size_t)n + 1);
                    snprintf(big, (size_t)n + 1, spec, v);
                }
                else
                    snprintf(tmp, sizeof(tmp), spec, v);
            }
            else
            {
                double v = __builtin_va_arg(ap, double);
                spec[sl++] = conv;
                spec[sl] = 0;
                n = snprintf(NULL, 0, spec, v);
                if (n >= (int)sizeof(tmp))
                {
                    big = malloc((size_t)n + 1);
                    snprintf(big, (size_t)n + 1, spec, v);
                }
                else
                    snprintf(tmp, sizeof(tmp), spec, v);
            }
            break;
        case 'n':
        {
            void *v = __builtin_va_arg(ap, void *);
            if (lenMod >= 3)
                *(long long *)v = (long long)o->len;
            else if (lenMod == 2)
                *(short *)v = (short)o->len;
            else if (lenMod == 1)
                *(signed char *)v = (signed char)o->len;
            else
                *(int *)v = (int)o->len;
            n = 0;
            tmp[0] = 0;
            break;
        }
        case 'm':
            n = snprintf(tmp, sizeof(tmp), "%s", strerror(*lxErrnoLocation()));
            break;
        default:
            // Unknown: written as it is.
            put(o, start, (size_t)(p - start));
            continue;
        }
        if (n > 0)
            put(o, big ? big : tmp, (size_t)n);
        free(big);
    }
    return (int)o->len;
}

static int formatTo(char *s, size_t size, const char *fmt, lx_va_list ap)
{
    Out o = {s, size, 0, 0};
    lxVformat(&o, fmt, ap);
    if (size)
        s[o.len < size ? o.len : size - 1] = 0;
    return (int)o.len;
}

static int formatFile(FILE *f, const char *fmt, lx_va_list ap)
{
    Out o = {NULL, 0, 0, 1};
    lxVformat(&o, fmt, ap);
    if (o.len)
        fwrite(o.buf, 1, o.len, f);
    if (f == lxStderr || f == lxStdout)
        fflush(f);
    lxFree(o.buf);
    return (int)o.len;
}

static SYSV int lx_vsnprintf(char *s, size_t n, const char *fmt, lx_va_list ap) { return formatTo(s, n, fmt, ap); }
static SYSV int lx_vsprintf(char *s, const char *fmt, lx_va_list ap) { return formatTo(s, (size_t)-1 >> 1, fmt, ap); }
static SYSV int lx_vfprintf(FILE *f, const char *fmt, lx_va_list ap) { return formatFile(f, fmt, ap); }
static SYSV int lx_vprintf(const char *fmt, lx_va_list ap) { return formatFile(lxStdout, fmt, ap); }
static SYSV int lx_vsnprintf_chk(char *s, size_t n, int fl, size_t sl, const char *fmt, lx_va_list ap)
{
    (void)fl; (void)sl;
    return formatTo(s, n, fmt, ap);
}
static SYSV int lx_vsprintf_chk(char *s, int fl, size_t sl, const char *fmt, lx_va_list ap)
{
    (void)fl;
    return formatTo(s, sl, fmt, ap);
}
static SYSV int lx_vfprintf_chk(FILE *f, int fl, const char *fmt, lx_va_list ap) { (void)fl; return formatFile(f, fmt, ap); }
static SYSV int lx_vprintf_chk(int fl, const char *fmt, lx_va_list ap) { (void)fl; return formatFile(lxStdout, fmt, ap); }
static SYSV int lx_vasprintf(char **out, const char *fmt, lx_va_list ap)
{
    Out o = {NULL, 0, 0, 1};
    lxVformat(&o, fmt, ap);
    if (!o.buf)
        o.buf = lxMalloc(1);
    o.buf[o.len] = 0;
    *out = o.buf;
    return (int)o.len;
}

#define VARIADIC(name, last, call)                                                                                     \
    {                                                                                                                  \
        lx_va_list ap;                                                                                                 \
        int r;                                                                                                         \
        __builtin_sysv_va_start(ap, last);                                                                             \
        r = call;                                                                                                      \
        __builtin_sysv_va_end(ap);                                                                                     \
        return r;                                                                                                      \
    }

static SYSV int lx_printf(const char *fmt, ...) VARIADIC(printf, fmt, formatFile(lxStdout, fmt, ap))
static SYSV int lx_fprintf(FILE *f, const char *fmt, ...) VARIADIC(fprintf, fmt, formatFile(f, fmt, ap))
static SYSV int lx_sprintf(char *s, const char *fmt, ...) VARIADIC(sprintf, fmt, formatTo(s, (size_t)-1 >> 1, fmt, ap))
static SYSV int lx_snprintf(char *s, size_t n, const char *fmt, ...) VARIADIC(snprintf, fmt, formatTo(s, n, fmt, ap))
static SYSV int lx_asprintf(char **s, const char *fmt, ...) VARIADIC(asprintf, fmt, lx_vasprintf(s, fmt, ap))
static SYSV int lx_printf_chk(int fl, const char *fmt, ...) VARIADIC(printf, fmt, formatFile(lxStdout, fmt, ap))
static SYSV int lx_fprintf_chk(FILE *f, int fl, const char *fmt, ...) VARIADIC(fprintf, fmt, formatFile(f, fmt, ap))
static SYSV int lx_sprintf_chk(char *s, int fl, size_t sl, const char *fmt, ...)
    VARIADIC(sprintf, fmt, formatTo(s, sl, fmt, ap))
static SYSV int lx_snprintf_chk(char *s, size_t n, int fl, size_t sl, const char *fmt, ...)
    VARIADIC(snprintf, fmt, formatTo(s, n, fmt, ap))
static SYSV int lx_asprintf_chk(char **s, int fl, const char *fmt, ...) VARIADIC(asprintf, fmt, lx_vasprintf(s, fmt, ap))
static SYSV int lx_dprintf(int fd, const char *fmt, ...)
{
    lx_va_list ap;
    Out o = {NULL, 0, 0, 1};
    __builtin_sysv_va_start(ap, fmt);
    lxVformat(&o, fmt, ap);
    __builtin_sysv_va_end(ap);
    if (o.len)
        _write(fd, o.buf, (unsigned)o.len);
    lxFree(o.buf);
    return (int)o.len;
}

// syslog and err: to the log.
static SYSV void lx_syslog(int pri, const char *fmt, ...)
{
    lx_va_list ap;
    (void)pri;
    __builtin_sysv_va_start(ap, fmt);
    formatFile(lxStderr, fmt, ap);
    __builtin_sysv_va_end(ap);
    fputc('\n', lxStderr);
}

// Wide (32-bit) printf: the format and the result through UTF-8.
static size_t wideLen(const uint32_t *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

static size_t utf8ToWide(const char *s, size_t len, uint32_t *out, size_t max)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n < max;)
    {
        uint8_t c = (uint8_t)s[i];
        uint32_t w;
        int k;
        if (c < 0x80) { w = c; k = 1; }
        else if ((c & 0xe0) == 0xc0) { w = c & 0x1f; k = 2; }
        else if ((c & 0xf0) == 0xe0) { w = c & 0x0f; k = 3; }
        else { w = c & 0x07; k = 4; }
        for (int j = 1; j < k && i + j < len; j++)
            w = (w << 6) | ((uint8_t)s[i + j] & 0x3f);
        out[n++] = w;
        i += (size_t)k;
    }
    return n;
}

static int wideFormat(uint32_t *s, size_t n, const uint32_t *fmt, lx_va_list ap)
{
    char *f8 = wideToUtf8(fmt, -1);
    Out o = {NULL, 0, 0, 1};
    size_t w;
    lxVformat(&o, f8, ap);
    free(f8);
    w = utf8ToWide(o.buf ? o.buf : "", o.len, s, n ? n - 1 : 0);
    if (n)
        s[w] = 0;
    lxFree(o.buf);
    return w + 1 >= n && o.len > w ? -1 : (int)w;
}

static SYSV int lx_vswprintf(uint32_t *s, size_t n, const uint32_t *fmt, lx_va_list ap) { return wideFormat(s, n, fmt, ap); }
static SYSV int lx_swprintf(uint32_t *s, size_t n, const uint32_t *fmt, ...) VARIADIC(swprintf, fmt, wideFormat(s, n, fmt, ap))
static SYSV int lx_swprintf_chk(uint32_t *s, size_t n, int fl, size_t sl, const uint32_t *fmt, ...)
    VARIADIC(swprintf, fmt, wideFormat(s, n, fmt, ap))
static SYSV int lx_vswprintf_chk(uint32_t *s, size_t n, int fl, size_t sl, const uint32_t *fmt, lx_va_list ap)
{
    (void)fl; (void)sl;
    return wideFormat(s, n, fmt, ap);
}
static int wideFile(FILE *f, const uint32_t *fmt, lx_va_list ap)
{
    char *f8 = wideToUtf8(fmt, -1);
    int r = formatFile(f, f8, ap);
    free(f8);
    return r;
}
static SYSV int lx_fwprintf(FILE *f, const uint32_t *fmt, ...) VARIADIC(fwprintf, fmt, wideFile(f, fmt, ap))
static SYSV int lx_fwprintf_chk(FILE *f, int fl, const uint32_t *fmt, ...) VARIADIC(fwprintf, fmt, wideFile(f, fmt, ap))
static SYSV int lx_vfwprintf(FILE *f, const uint32_t *fmt, lx_va_list ap) { return wideFile(f, fmt, ap); }
static SYSV int lx_wprintf(const uint32_t *fmt, ...) VARIADIC(wprintf, fmt, wideFile(lxStdout, fmt, ap))

// ---------------------------------------------------------------------------
// scanf: the format's conversions (Linux's long in 64 bits) handed to the
// CRT's, with the pointers taken from the va_list.

#define MAX_SCAN 24

static int scanPrepare(const char *fmt, char *out, size_t outSize, void **ptrs, lx_va_list ap)
{
    size_t o = 0;
    int n = 0;
    for (const char *p = fmt; *p && o + 8 < outSize;)
    {
        if (*p != '%')
        {
            out[o++] = *p++;
            continue;
        }
        out[o++] = *p++;
        if (*p == '%')
        {
            out[o++] = *p++;
            continue;
        }
        int suppress = *p == '*';
        if (suppress)
            out[o++] = *p++;
        while (*p >= '0' && *p <= '9')
            out[o++] = *p++;
        int lenMod = 0;
        for (;;)
        {
            if (*p == 'h') { out[o++] = 'h'; p++; }
            else if (*p == 'l') { lenMod++; p++; }
            else if (*p == 'z' || *p == 'j' || *p == 't' || *p == 'q') { lenMod = 2; p++; }
            else if (*p == 'L') { out[o++] = 'L'; p++; }
            else if (*p == 'm') { p++; }   // allocation: not handled
            else break;
        }
        char conv = *p;
        if (lenMod)
        {
            if (strchr("diouxXn", conv))
            {
                out[o++] = 'l';
                out[o++] = 'l';
            }
            else if (strchr("eEfgGaA", conv))
                out[o++] = 'l';
            // %ls / %lc: wide strings, read narrow (rare in the games).
        }
        if (conv == '[')
        {
            out[o++] = *p++;
            if (*p == '^')
                out[o++] = *p++;
            if (*p == ']')
                out[o++] = *p++;
            while (*p && *p != ']' && o + 2 < outSize)
                out[o++] = *p++;
            if (*p)
                out[o++] = *p++;
        }
        else if (conv)
            out[o++] = *p++;
        if (!suppress && conv && n < MAX_SCAN)
            ptrs[n++] = __builtin_va_arg(ap, void *);
    }
    out[o] = 0;
    return n;
}

#define SCAN_ARGS(p) p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], \
                     p[14], p[15], p[16], p[17], p[18], p[19], p[20], p[21], p[22], p[23]

static int scanString(const char *s, const char *fmt, lx_va_list ap)
{
    char f[1024];
    void *p[MAX_SCAN] = {0};
    scanPrepare(fmt, f, sizeof(f), p, ap);
    return sscanf(s, f, SCAN_ARGS(p));
}

static int scanFile(FILE *file, const char *fmt, lx_va_list ap)
{
    char f[1024];
    void *p[MAX_SCAN] = {0};
    scanPrepare(fmt, f, sizeof(f), p, ap);
    return fscanf(file, f, SCAN_ARGS(p));
}

static SYSV int lx_sscanf(const char *s, const char *fmt, ...) VARIADIC(sscanf, fmt, scanString(s, fmt, ap))
static SYSV int lx_vsscanf(const char *s, const char *fmt, lx_va_list ap) { return scanString(s, fmt, ap); }
static SYSV int lx_fscanf(FILE *f, const char *fmt, ...) VARIADIC(fscanf, fmt, scanFile(f, fmt, ap))
static SYSV int lx_vfscanf(FILE *f, const char *fmt, lx_va_list ap) { return scanFile(f, fmt, ap); }
static SYSV int lx_scanf(const char *fmt, ...) VARIADIC(scanf, fmt, scanFile(lxStdin, fmt, ap))

static int wideScan(const uint32_t *s, const uint32_t *fmt, lx_va_list ap)
{
    char *s8 = wideToUtf8(s, -1), *f8 = wideToUtf8(fmt, -1);
    int r = scanString(s8, f8, ap);
    free(s8);
    free(f8);
    return r;
}
static SYSV int lx_swscanf(const uint32_t *s, const uint32_t *fmt, ...) VARIADIC(swscanf, fmt, wideScan(s, fmt, ap))
static SYSV int lx_vswscanf(const uint32_t *s, const uint32_t *fmt, lx_va_list ap) { return wideScan(s, fmt, ap); }

// ---------------------------------------------------------------------------
// FILE

SYSV FILE *lxFopen(const char *path, const char *mode)
{
    char buf[MAX_PATH], m[16];
    size_t k = 0;
    const char *p = lxMapPath(path, buf, sizeof(buf));
    FILE *f;
    if (!p)
    {
        lxSetErrno(2);
        return NULL;
    }
    // Binary always; glibc's 'e' (close on exec) and 'x' left out.
    for (const char *c = mode; *c && k < sizeof(m) - 2; c++)
        if (*c != 'b' && *c != 'e' && *c != 'x' && *c != 't' && *c != 'm')
            m[k++] = *c;
    m[k++] = 'b';
    m[k] = 0;
    f = fopen(p, m);
    if (!f)
        lxSetErrnoFromCrt();
    lxTraceLog("fopen %s (%s) -> %p\n", path, p, (void *)f);
    return f;
}

static SYSV FILE *lx_fdopen(int fd, const char *mode)
{
    char m[16];
    size_t k = 0;
    for (const char *c = mode; *c && k < sizeof(m) - 2; c++)
        if (*c != 'b' && *c != 'e')
            m[k++] = *c;
    m[k++] = 'b';
    m[k] = 0;
    return _fdopen(fd, m);
}
static SYSV FILE *lx_freopen(const char *path, const char *mode, FILE *f)
{
    char buf[MAX_PATH];
    const char *p = path ? lxMapPath(path, buf, sizeof(buf)) : NULL;
    return p ? freopen(p, mode, f) : NULL;
}
static SYSV int lx_fclose(FILE *f) { return fclose(f); }
static SYSV size_t lx_fread(void *b, size_t s, size_t n, FILE *f) { return fread(b, s, n, f); }
static SYSV size_t lx_fread_chk(void *b, size_t bs, size_t s, size_t n, FILE *f) { (void)bs; return fread(b, s, n, f); }
static SYSV size_t lx_fwrite(const void *b, size_t s, size_t n, FILE *f)
{
    size_t r = fwrite(b, s, n, f);
    if (f == lxStderr)
        fflush(f);
    return r;
}
static SYSV int lx_fflush(FILE *f) { return fflush(f); }
static SYSV int lx_fseek(FILE *f, lx_long off, int w) { return _fseeki64(f, off, w); }
static SYSV lx_long lx_ftell(FILE *f) { return _ftelli64(f); }
static SYSV void lx_rewind(FILE *f) { rewind(f); }
static SYSV int lx_fgetpos(FILE *f, int64_t *pos) { *pos = _ftelli64(f); return *pos < 0 ? -1 : 0; }
static SYSV int lx_fsetpos(FILE *f, const int64_t *pos) { return _fseeki64(f, *pos, SEEK_SET); }
static SYSV int lx_feof(FILE *f) { return feof(f); }
static SYSV int lx_ferror(FILE *f) { return ferror(f); }
static SYSV void lx_clearerr(FILE *f) { clearerr(f); }
static SYSV int lx_fileno(FILE *f) { return _fileno(f); }
static SYSV int lx_fgetc(FILE *f) { return fgetc(f); }
static SYSV int lx_ungetc(int c, FILE *f) { return ungetc(c, f); }
static SYSV char *lx_fgets(char *s, int n, FILE *f) { return fgets(s, n, f); }
static SYSV int lx_fputc(int c, FILE *f) { return fputc(c, f); }
static SYSV int lx_fputs(const char *s, FILE *f)
{
    int r = fputs(s, f);
    if (f == lxStderr)
        fflush(f);
    return r;
}
static SYSV int lx_puts(const char *s) { int r = puts(s); fflush(stdout); return r; }
static SYSV int lx_putchar(int c) { return putchar(c); }
static SYSV int lx_getchar(void) { return getchar(); }
static SYSV int lx_setvbuf(FILE *f, char *b, int mode, size_t n)
{
    // Linux: _IOFBF 0, _IOLBF 1, _IONBF 2.
    int m = mode == 0 ? _IOFBF : mode == 1 ? _IOLBF : _IONBF;
    if (m == _IOLBF)
        m = _IONBF;
    return setvbuf(f, NULL, m, n < 2 ? 512 : n);
}
static SYSV void lx_setbuf(FILE *f, char *b) { setvbuf(f, NULL, b ? _IOFBF : _IONBF, 4096); }
static SYSV void lx_perror(const char *s)
{
    fprintf(stderr, "%s%s%s\n", s ? s : "", s && *s ? ": " : "", strerror(*lxErrnoLocation()));
}
static SYSV FILE *lx_tmpfile(void) { return tmpfile(); }
static SYSV int64_t lx_getline(char **line, size_t *n, FILE *f)
{
    size_t len = 0;
    int c;
    if (!*line || !*n)
    {
        *n = 128;
        *line = lxRealloc(*line, *n);
    }
    while ((c = fgetc(f)) != EOF)
    {
        if (len + 2 > *n)
            *line = lxRealloc(*line, *n *= 2);
        (*line)[len++] = (char)c;
        if (c == '\n')
            break;
    }
    if (!len)
        return -1;
    (*line)[len] = 0;
    return (int64_t)len;
}
static SYSV void lx_flockfile(FILE *f) { _lock_file(f); }
static SYSV void lx_funlockfile(FILE *f) { _unlock_file(f); }

// Wide stdio (rare): narrow underneath.
static SYSV uint32_t lx_getwc(FILE *f) { int c = fgetc(f); return c == EOF ? 0xffffffffu : (uint32_t)c; }
static SYSV uint32_t lx_ungetwc(uint32_t c, FILE *f) { return (uint32_t)ungetc((int)c, f); }
static SYSV uint32_t lx_putwc(uint32_t c, FILE *f)
{
    char u[8];
    size_t k = utf8Encode(c, u);
    fwrite(u, 1, k, f);
    return c;
}

static SYSV char *lx_strerror(int e) { return strerror(e); }
static SYSV int lx_strerror_r(int e, char *buf, size_t n)
{
    snprintf(buf, n, "%s", strerror(e));
    return 0;
}
// The GNU strerror_r returns the string.
static SYSV char *lx_gnu_strerror_r(int e, char *buf, size_t n)
{
    snprintf(buf, n, "%s", strerror(e));
    return buf;
}
static SYSV char *lx_strerror_l(int e, void *l) { (void)l; return strerror(e); }

static const LxSym syms[] = {
    {"vsnprintf", lx_vsnprintf}, {"vsprintf", lx_vsprintf}, {"vfprintf", lx_vfprintf}, {"vprintf", lx_vprintf},
    {"__vsnprintf_chk", lx_vsnprintf_chk}, {"__vsprintf_chk", lx_vsprintf_chk}, {"__vfprintf_chk", lx_vfprintf_chk},
    {"__vprintf_chk", lx_vprintf_chk}, {"vasprintf", lx_vasprintf}, {"printf", lx_printf}, {"fprintf", lx_fprintf},
    {"sprintf", lx_sprintf}, {"snprintf", lx_snprintf}, {"asprintf", lx_asprintf}, {"__printf_chk", lx_printf_chk},
    {"__fprintf_chk", lx_fprintf_chk}, {"__sprintf_chk", lx_sprintf_chk}, {"__snprintf_chk", lx_snprintf_chk},
    {"__asprintf_chk", lx_asprintf_chk}, {"dprintf", lx_dprintf}, {"syslog", lx_syslog},
    {"vswprintf", lx_vswprintf}, {"swprintf", lx_swprintf}, {"__swprintf_chk", lx_swprintf_chk},
    {"__vswprintf_chk", lx_vswprintf_chk}, {"fwprintf", lx_fwprintf}, {"__fwprintf_chk", lx_fwprintf_chk},
    {"vfwprintf", lx_vfwprintf}, {"wprintf", lx_wprintf},
    {"sscanf", lx_sscanf}, {"__isoc99_sscanf", lx_sscanf}, {"__isoc23_sscanf", lx_sscanf}, {"vsscanf", lx_vsscanf},
    {"__isoc99_vsscanf", lx_vsscanf}, {"fscanf", lx_fscanf}, {"__isoc99_fscanf", lx_fscanf},
    {"__isoc23_fscanf", lx_fscanf}, {"vfscanf", lx_vfscanf}, {"__isoc99_vfscanf", lx_vfscanf}, {"scanf", lx_scanf},
    {"__isoc99_scanf", lx_scanf}, {"swscanf", lx_swscanf}, {"__isoc99_swscanf", lx_swscanf},
    {"vswscanf", lx_vswscanf}, {"__isoc99_vswscanf", lx_vswscanf},
    {"fopen", lxFopen}, {"fopen64", lxFopen}, {"fdopen", lx_fdopen}, {"freopen", lx_freopen},
    {"freopen64", lx_freopen}, {"fclose", lx_fclose}, {"fread", lx_fread}, {"__fread_chk", lx_fread_chk},
    {"fread_unlocked", lx_fread}, {"fwrite", lx_fwrite}, {"fwrite_unlocked", lx_fwrite}, {"fflush", lx_fflush},
    {"fflush_unlocked", lx_fflush}, {"fseek", lx_fseek}, {"fseeko", lx_fseek}, {"fseeko64", lx_fseek},
    {"ftell", lx_ftell}, {"ftello", lx_ftell}, {"ftello64", lx_ftell}, {"rewind", lx_rewind},
    {"fgetpos", lx_fgetpos}, {"fgetpos64", lx_fgetpos}, {"fsetpos", lx_fsetpos}, {"fsetpos64", lx_fsetpos},
    {"feof", lx_feof}, {"ferror", lx_ferror}, {"clearerr", lx_clearerr}, {"fileno", lx_fileno},
    {"fgetc", lx_fgetc}, {"getc", lx_fgetc}, {"_IO_getc", lx_fgetc}, {"getc_unlocked", lx_fgetc},
    {"ungetc", lx_ungetc}, {"fgets", lx_fgets}, {"fputc", lx_fputc}, {"putc", lx_fputc}, {"_IO_putc", lx_fputc},
    {"putc_unlocked", lx_fputc}, {"fputs", lx_fputs}, {"puts", lx_puts}, {"putchar", lx_putchar},
    {"getchar", lx_getchar}, {"setvbuf", lx_setvbuf}, {"setbuf", lx_setbuf}, {"perror", lx_perror},
    {"tmpfile", lx_tmpfile}, {"getline", lx_getline}, {"flockfile", lx_flockfile},
    {"funlockfile", lx_funlockfile}, {"getwc", lx_getwc}, {"fgetwc", lx_getwc}, {"ungetwc", lx_ungetwc},
    {"putwc", lx_putwc}, {"fputwc", lx_putwc},
    {"strerror", lx_strerror}, {"__xpg_strerror_r", lx_strerror_r}, {"strerror_r", lx_gnu_strerror_r},
    {"strerror_l", lx_strerror_l},
};

static const LxSym dataSyms[] = {
    {"stdin", &lxStdin}, {"stdout", &lxStdout}, {"stderr", &lxStderr},
    {"_IO_2_1_stdin_", NULL}, {"_IO_2_1_stdout_", NULL}, {"_IO_2_1_stderr_", NULL},
};

void lxStdioInit(void)
{
    lxStdin = stdin;
    lxStdout = stdout;
    lxStderr = stderr;
    lxRegister(syms, LX_COUNT(syms));
    lxRegister(dataSyms, 3);
}
