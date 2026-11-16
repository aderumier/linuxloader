// The rest of libc (see ll64.h): 32-bit wide characters, the C locale the
// GNU locale model of libstdc++ reads, setjmp, and what a cabinet has and a
// Windows desktop not (sockets, udev, terminals, shared memory): refused.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <ctype.h>
#include "ll64.h"

typedef uint32_t lx_wchar;

SYSV void *lxMalloc(size_t n);

// ---------------------------------------------------------------------------
// Wide strings.

static SYSV size_t lx_wcslen(const lx_wchar *s) { size_t n = 0; while (s[n]) n++; return n; }
static SYSV size_t lx_wcsnlen(const lx_wchar *s, size_t m) { size_t n = 0; while (n < m && s[n]) n++; return n; }
static SYSV int lx_wcscmp(const lx_wchar *a, const lx_wchar *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a < *b ? -1 : *a > *b;
}
static SYSV int lx_wcsncmp(const lx_wchar *a, const lx_wchar *b, size_t n)
{
    for (; n && *a && *a == *b; n--)
        a++, b++;
    return !n ? 0 : *a < *b ? -1 : *a > *b;
}
static lx_wchar lower(lx_wchar c) { return c < 0x10000 ? (lx_wchar)towlower((wint_t)c) : c; }
static lx_wchar upper(lx_wchar c) { return c < 0x10000 ? (lx_wchar)towupper((wint_t)c) : c; }
static SYSV int lx_wcscasecmp(const lx_wchar *a, const lx_wchar *b)
{
    while (*a && lower(*a) == lower(*b))
        a++, b++;
    return lower(*a) < lower(*b) ? -1 : lower(*a) > lower(*b);
}
static SYSV lx_wchar *lx_wcscpy(lx_wchar *d, const lx_wchar *s) { lx_wchar *r = d; while ((*d++ = *s++)); return r; }
static SYSV lx_wchar *lx_wcscpy_chk(lx_wchar *d, const lx_wchar *s, size_t n) { (void)n; return lx_wcscpy(d, s); }
static SYSV lx_wchar *lx_wcsncpy(lx_wchar *d, const lx_wchar *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}
static SYSV lx_wchar *lx_wcscat(lx_wchar *d, const lx_wchar *s) { lx_wcscpy(d + lx_wcslen(d), s); return d; }
static SYSV lx_wchar *lx_wcsncat(lx_wchar *d, const lx_wchar *s, size_t n)
{
    lx_wchar *e = d + lx_wcslen(d);
    while (n-- && *s)
        *e++ = *s++;
    *e = 0;
    return d;
}
static SYSV lx_wchar *lx_wcschr(const lx_wchar *s, lx_wchar c)
{
    for (;; s++)
    {
        if (*s == c)
            return (lx_wchar *)s;
        if (!*s)
            return NULL;
    }
}
static SYSV lx_wchar *lx_wcsrchr(const lx_wchar *s, lx_wchar c)
{
    const lx_wchar *r = NULL;
    for (;; s++)
    {
        if (*s == c)
            r = s;
        if (!*s)
            return (lx_wchar *)r;
    }
}
static SYSV lx_wchar *lx_wcsstr(const lx_wchar *h, const lx_wchar *n)
{
    size_t l = lx_wcslen(n);
    for (; *h; h++)
        if (!memcmp(h, n, l * 4))
            return (lx_wchar *)h;
    return l ? NULL : (lx_wchar *)h;
}
static SYSV lx_wchar *lx_wcstok(lx_wchar *s, const lx_wchar *d, lx_wchar **save)
{
    if (!s)
        s = *save;
    while (*s && lx_wcschr(d, *s))
        s++;
    if (!*s)
        return *save = s, NULL;
    lx_wchar *t = s;
    while (*s && !lx_wcschr(d, *s))
        s++;
    if (*s)
        *s++ = 0;
    *save = s;
    return t;
}
static SYSV lx_wchar *lx_wmemchr(const lx_wchar *s, lx_wchar c, size_t n)
{
    for (; n--; s++)
        if (*s == c)
            return (lx_wchar *)s;
    return NULL;
}
static SYSV int lx_wmemcmp(const lx_wchar *a, const lx_wchar *b, size_t n)
{
    for (; n--; a++, b++)
        if (*a != *b)
            return *a < *b ? -1 : 1;
    return 0;
}
static SYSV lx_wchar *lx_wmemcpy(lx_wchar *d, const lx_wchar *s, size_t n) { return memcpy(d, s, n * 4); }
static SYSV lx_wchar *lx_wmemcpy_chk(lx_wchar *d, const lx_wchar *s, size_t n, size_t dn) { (void)dn; return memcpy(d, s, n * 4); }
static SYSV lx_wchar *lx_wmemmove(lx_wchar *d, const lx_wchar *s, size_t n) { return memmove(d, s, n * 4); }
static SYSV lx_wchar *lx_wmemset(lx_wchar *d, lx_wchar c, size_t n) { for (size_t i = 0; i < n; i++) d[i] = c; return d; }

// Numbers from wide strings: through a narrow copy.
static char *narrow(const lx_wchar *s, char *buf, size_t n)
{
    size_t i = 0;
    for (; i + 1 < n && s[i]; i++)
        buf[i] = s[i] < 128 ? (char)s[i] : '?';
    buf[i] = 0;
    return buf;
}
#define WIDE_NUM(name, type, call)                                                                                     \
    static SYSV type name(const lx_wchar *s, lx_wchar **end, int base)                                                 \
    {                                                                                                                  \
        char b[128], *e;                                                                                               \
        type r = (type)call(narrow(s, b, sizeof(b)), &e, base);                                                        \
        if (end)                                                                                                       \
            *end = (lx_wchar *)s + (e - b);                                                                            \
        return r;                                                                                                      \
    }
WIDE_NUM(lx_wcstol, int64_t, strtoll)
WIDE_NUM(lx_wcstoul, uint64_t, strtoull)
static SYSV double lx_wcstod(const lx_wchar *s, lx_wchar **end)
{
    char b[128], *e;
    double r = strtod(narrow(s, b, sizeof(b)), &e);
    if (end)
        *end = (lx_wchar *)s + (e - b);
    return r;
}
static SYSV float lx_wcstof(const lx_wchar *s, lx_wchar **end) { return (float)lx_wcstod(s, end); }

// Classes and conversions: the C locale, bytes 0..255 as the characters.
static SYSV int lx_iswalpha(uint32_t c) { return c < 0x10000 && iswalpha((wint_t)c); }
static SYSV int lx_iswdigit(uint32_t c) { return c >= '0' && c <= '9'; }
static SYSV int lx_iswspace(uint32_t c) { return c == ' ' || (c >= 9 && c <= 13); }
static SYSV int lx_iswupper(uint32_t c) { return c < 0x10000 && iswupper((wint_t)c); }
static SYSV int lx_iswlower(uint32_t c) { return c < 0x10000 && iswlower((wint_t)c); }
static SYSV int lx_iswalnum(uint32_t c) { return lx_iswalpha(c) || lx_iswdigit(c); }
static SYSV int lx_iswprint(uint32_t c) { return c >= 32 && c != 127; }
static SYSV int lx_iswpunct(uint32_t c) { return c < 0x10000 && iswpunct((wint_t)c); }
static SYSV int lx_iswxdigit(uint32_t c) { return c < 128 && isxdigit((int)c); }
static SYSV uint32_t lx_towlower(uint32_t c) { return lower(c); }
static SYSV uint32_t lx_towupper(uint32_t c) { return upper(c); }
static SYSV uint32_t lx_towlower_l(uint32_t c, void *l) { (void)l; return lower(c); }
static SYSV uint32_t lx_towupper_l(uint32_t c, void *l) { (void)l; return upper(c); }

// wctype: glibc's descriptors are opaque, ours small numbers.
static const char *const classes[] = {"", "alnum", "alpha", "blank", "cntrl", "digit", "graph", "lower", "print",
                                      "punct", "space", "upper", "xdigit"};
static SYSV uint64_t lx_wctype(const char *name)
{
    for (size_t i = 1; i < LX_COUNT(classes); i++)
        if (!strcmp(name, classes[i]))
            return i;
    return 0;
}
static SYSV uint64_t lx_wctype_l(const char *name, void *l) { (void)l; return lx_wctype(name); }
static SYSV int lx_iswctype(uint32_t c, uint64_t t)
{
    switch (t)
    {
    case 1: return lx_iswalnum(c);
    case 2: return lx_iswalpha(c);
    case 3: return c == ' ' || c == '\t';
    case 4: return c < 32 || c == 127;
    case 5: return lx_iswdigit(c);
    case 6: return c > 32 && c != 127;
    case 7: return lx_iswlower(c);
    case 8: return lx_iswprint(c);
    case 9: return lx_iswpunct(c);
    case 10: return lx_iswspace(c);
    case 11: return lx_iswupper(c);
    case 12: return lx_iswxdigit(c);
    }
    return 0;
}
static SYSV int lx_iswctype_l(uint32_t c, uint64_t t, void *l) { (void)l; return lx_iswctype(c, t); }
static SYSV uint64_t lx_wctrans(const char *name) { return !strcmp(name, "tolower") ? 1 : !strcmp(name, "toupper") ? 2 : 0; }
static SYSV uint32_t lx_towctrans(uint32_t c, uint64_t t) { return t == 1 ? lower(c) : t == 2 ? upper(c) : c; }
static SYSV int lx_wcscoll(const lx_wchar *a, const lx_wchar *b) { return lx_wcscmp(a, b); }
static SYSV int lx_wcscoll_l(const lx_wchar *a, const lx_wchar *b, void *l) { (void)l; return lx_wcscmp(a, b); }
static SYSV size_t lx_wcsxfrm_l(lx_wchar *d, const lx_wchar *s, size_t n, void *l)
{
    size_t len = lx_wcslen(s);
    (void)l;
    if (n)
        lx_wcsncpy(d, s, n);
    return len;
}
static SYSV size_t lx_wcsftime_l(lx_wchar *s, size_t n, const lx_wchar *f, const void *tm, void *l)
{
    (void)f; (void)tm; (void)l;
    if (n)
        s[0] = 0;
    return 0;
}

static SYSV int lx_btowc(int c) { return c == -1 ? -1 : (c & 0xff); }
static SYSV int lx_wctob(uint32_t c) { return c < 256 ? (int)c : -1; }
static SYSV size_t lx_mbrtowc(lx_wchar *w, const char *s, size_t n, void *st)
{
    (void)st;
    if (!s)
        return 0;
    if (!n)
        return (size_t)-2;
    if (w)
        *w = (uint8_t)*s;
    return *s ? 1 : 0;
}
static SYSV size_t lx_mbrlen(const char *s, size_t n, void *st) { return lx_mbrtowc(NULL, s, n, st); }
static SYSV size_t lx_wcrtomb(char *s, lx_wchar w, void *st)
{
    (void)st;
    if (!s)
        return 1;
    if (w > 255)
    {
        lxSetErrno(84);
        return (size_t)-1;
    }
    *s = (char)w;
    return 1;
}
static SYSV int lx_mbsinit(const void *st) { (void)st; return 1; }
static SYSV size_t lx_mbsnrtowcs(lx_wchar *d, const char **src, size_t nms, size_t len, void *st)
{
    const char *s = *src;
    size_t i = 0;
    (void)st;
    for (; i < nms && (!d || i < len); i++)
    {
        if (d)
            d[i] = (uint8_t)s[i];
        if (!s[i])
        {
            if (d)
                *src = NULL;
            return i;
        }
    }
    if (d)
        *src = s + i;
    return i;
}
static SYSV size_t lx_mbsrtowcs(lx_wchar *d, const char **src, size_t len, void *st) { return lx_mbsnrtowcs(d, src, (size_t)-1, len, st); }
static SYSV size_t lx_mbsrtowcs_chk(lx_wchar *d, const char **src, size_t len, void *st, size_t dl) { (void)dl; return lx_mbsrtowcs(d, src, len, st); }
static SYSV size_t lx_wcsnrtombs(char *d, const lx_wchar **src, size_t nwc, size_t len, void *st)
{
    const lx_wchar *s = *src;
    size_t i = 0;
    (void)st;
    for (; i < nwc && (!d || i < len); i++)
    {
        if (d)
            d[i] = s[i] < 256 ? (char)s[i] : '?';
        if (!s[i])
        {
            if (d)
                *src = NULL;
            return i;
        }
    }
    if (d)
        *src = s + i;
    return i;
}
static SYSV size_t lx_wcsrtombs(char *d, const lx_wchar **src, size_t len, void *st) { return lx_wcsnrtombs(d, src, (size_t)-1, len, st); }
static SYSV size_t lx_mbstowcs(lx_wchar *d, const char *s, size_t n) { const char *p = s; return lx_mbsrtowcs(d, &p, n, NULL); }
static SYSV size_t lx_mbstowcs_chk(lx_wchar *d, const char *s, size_t n, size_t dl) { (void)dl; return lx_mbstowcs(d, s, n); }
static SYSV size_t lx_wcstombs(char *d, const lx_wchar *s, size_t n) { const lx_wchar *p = s; return lx_wcsrtombs(d, &p, n, NULL); }
static SYSV int lx_mbtowc(lx_wchar *w, const char *s, size_t n) { return s ? (int)lx_mbrtowc(w, s, n, NULL) : 0; }
static SYSV int lx_wctomb(char *s, lx_wchar w) { return s ? (int)lx_wcrtomb(s, w, NULL) : 0; }

// ---------------------------------------------------------------------------
// The C locale, as glibc's __locale_struct: 13 data pointers, then the ctype
// tables, then the names.

typedef struct
{
    void *locales[13];
    const unsigned short *ctypeB;
    const int32_t *ctypeTolower, *ctypeToupper;
    const char *names[13];
} LxLocale;

static LxLocale cLocale;
static __thread LxLocale *threadLocale;

static void localeInit(void)
{
    SYSV const unsigned short **(*b)(void) = lxBridge("__ctype_b_loc");
    SYSV const int32_t **(*lo)(void) = lxBridge("__ctype_tolower_loc");
    SYSV const int32_t **(*up)(void) = lxBridge("__ctype_toupper_loc");
    cLocale.ctypeB = *b();
    cLocale.ctypeTolower = *lo();
    cLocale.ctypeToupper = *up();
    for (int i = 0; i < 13; i++)
        cLocale.names[i] = "C";
}

static SYSV void *lx_newlocale(int mask, const char *name, void *base)
{
    (void)mask; (void)name; (void)base;
    return &cLocale;
}
static SYSV void *lx_duplocale(void *l) { return l; }
static SYSV void lx_freelocale(void *l) { (void)l; }
static SYSV void *lx_uselocale(void *l)
{
    void *old = threadLocale ? (void *)threadLocale : (void *)-1;
    if (l)
        threadLocale = l == (void *)-1 ? NULL : l;
    return old;
}
static SYSV char *lx_setlocale(int cat, const char *name) { (void)cat; (void)name; return "C"; }

// nl_langinfo: the items libstdc++'s facets ask for.
static SYSV const char *lx_nl_langinfo(int item)
{
    switch (item)
    {
    case 14: return "ANSI_X3.4-1968";           // CODESET
    case 0x10000: return ".";                   // RADIXCHAR / DECIMAL_POINT
    case 0x10001: return "";                    // THOUSEP
    case 0x10002: return "";                    // GROUPING
    case 0x20028: return "%a %b %e %H:%M:%S %Y"; // D_T_FMT
    case 0x20029: return "%m/%d/%y";            // D_FMT
    case 0x2002a: return "%H:%M:%S";            // T_FMT
    case 0x2002b: return "%I:%M:%S %p";         // T_FMT_AMPM
    case 0x20026: return "AM";
    case 0x20027: return "PM";
    }
    if (item >= 0x20000 && item < 0x20007) { static const char *d[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}; return d[item - 0x20000]; }
    if (item >= 0x20007 && item < 0x2000e) { static const char *d[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}; return d[item - 0x20007]; }
    if (item >= 0x2000e && item < 0x2001a) { static const char *m[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}; return m[item - 0x2000e]; }
    if (item >= 0x2001a && item < 0x20026) { static const char *m[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"}; return m[item - 0x2001a]; }
    return "";
}
static SYSV const char *lx_nl_langinfo_l(int item, void *l) { (void)l; return lx_nl_langinfo(item); }

// No translations.
static SYSV const char *lx_gettext(const char *s) { return s; }
static SYSV const char *lx_dgettext(const char *d, const char *s) { (void)d; return s; }
static SYSV const char *lx_dcgettext(const char *d, const char *s, int c) { (void)d; (void)c; return s; }
static SYSV const char *lx_bindtextdomain(const char *d, const char *dir) { (void)d; return dir ? dir : "/usr/share/locale"; }
static SYSV const char *lx_bind_textdomain_codeset(const char *d, const char *c) { (void)d; return c; }
static SYSV const char *lx_textdomain(const char *d) { return d ? d : "messages"; }

// iconv between the encodings the games' text takes: UTF-8 (and the C
// locale's, "" or ASCII), UCS-4 / WCHAR_T (32 bits) and UTF-16, little endian.
enum { ENC_UTF8, ENC_UCS4, ENC_UTF16 };

typedef struct
{
    int from, to;
} LxIconv;

static int encoding(const char *name)
{
    char n[32];
    size_t i = 0;
    for (; name[i] && i < sizeof(n) - 1; i++)
        n[i] = (char)toupper((unsigned char)name[i]);
    n[i] = 0;
    if ((i = strcspn(n, "/")) < strlen(n))
        n[i] = 0;   // //TRANSLIT, //IGNORE
    if (!n[0] || !strcmp(n, "UTF-8") || !strcmp(n, "UTF8") || !strcmp(n, "ASCII") || !strcmp(n, "US-ASCII") ||
        !strcmp(n, "ANSI_X3.4-1968") || !strcmp(n, "ISO-8859-1") || !strcmp(n, "LATIN1") || !strcmp(n, "CHAR"))
        return ENC_UTF8;
    if (!strcmp(n, "WCHAR_T") || !strncmp(n, "UCS-4", 5) || !strncmp(n, "UTF-32", 6) || !strcmp(n, "UCS4"))
        return ENC_UCS4;
    if (!strncmp(n, "UTF-16", 6) || !strncmp(n, "UCS-2", 5))
        return ENC_UTF16;
    return -1;
}

static SYSV void *lx_iconv_open(const char *to, const char *from)
{
    int t = encoding(to), f = encoding(from);
    LxIconv *c;
    if (t < 0 || f < 0)
    {
        lxLog("iconv_open %s <- %s refused\n", to, from);
        lxSetErrno(22);
        return (void *)-1;
    }
    c = malloc(sizeof(*c));
    c->from = f;
    c->to = t;
    return c;
}

static SYSV int lx_iconv_close(void *c)
{
    if (c && c != (void *)-1)
        free(c);
    return 0;
}

// One character from in (0: incomplete input, -1: invalid).
static int decodeOne(int enc, const uint8_t *in, size_t n, uint32_t *c)
{
    if (enc == ENC_UCS4)
    {
        if (n < 4)
            return 0;
        memcpy(c, in, 4);
        return 4;
    }
    if (enc == ENC_UTF16)
    {
        uint16_t u;
        if (n < 2)
            return 0;
        memcpy(&u, in, 2);
        if (u >= 0xd800 && u < 0xdc00)
        {
            uint16_t l;
            if (n < 4)
                return 0;
            memcpy(&l, in + 2, 2);
            *c = 0x10000 + ((uint32_t)(u - 0xd800) << 10) + (l - 0xdc00);
            return 4;
        }
        *c = u;
        return 2;
    }
    {
        uint8_t b = in[0];
        int k = b < 0x80 ? 1 : (b & 0xe0) == 0xc0 ? 2 : (b & 0xf0) == 0xe0 ? 3 : (b & 0xf8) == 0xf0 ? 4 : -1;
        if (k < 0)
            return -1;
        if ((size_t)k > n)
            return 0;
        *c = k == 1 ? b : k == 2 ? b & 0x1f : k == 3 ? b & 0x0f : b & 0x07;
        for (int j = 1; j < k; j++)
        {
            if ((in[j] & 0xc0) != 0x80)
                return -1;
            *c = (*c << 6) | (in[j] & 0x3f);
        }
        return k;
    }
}

static int encodeOne(int enc, uint32_t c, uint8_t *out)
{
    if (enc == ENC_UCS4)
    {
        memcpy(out, &c, 4);
        return 4;
    }
    if (enc == ENC_UTF16)
    {
        if (c >= 0x10000)
        {
            uint16_t h = (uint16_t)(0xd800 + ((c - 0x10000) >> 10)), l = (uint16_t)(0xdc00 + ((c - 0x10000) & 0x3ff));
            memcpy(out, &h, 2);
            memcpy(out + 2, &l, 2);
            return 4;
        }
        uint16_t u = (uint16_t)c;
        memcpy(out, &u, 2);
        return 2;
    }
    if (c < 0x80) { out[0] = (uint8_t)c; return 1; }
    if (c < 0x800) { out[0] = (uint8_t)(0xc0 | c >> 6); out[1] = (uint8_t)(0x80 | (c & 0x3f)); return 2; }
    if (c < 0x10000)
    {
        out[0] = (uint8_t)(0xe0 | c >> 12); out[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3f)); out[2] = (uint8_t)(0x80 | (c & 0x3f));
        return 3;
    }
    out[0] = (uint8_t)(0xf0 | c >> 18); out[1] = (uint8_t)(0x80 | ((c >> 12) & 0x3f));
    out[2] = (uint8_t)(0x80 | ((c >> 6) & 0x3f)); out[3] = (uint8_t)(0x80 | (c & 0x3f));
    return 4;
}

static SYSV size_t lx_iconv(LxIconv *cd, char **in, size_t *inLeft, char **out, size_t *outLeft)
{
    if (!in || !*in)
        return 0;
    while (*inLeft)
    {
        uint32_t c;
        uint8_t buf[4];
        int k = decodeOne(cd->from, (const uint8_t *)*in, *inLeft, &c), m;
        if (k <= 0)
        {
            lxSetErrno(k ? 84 : 22);   // EILSEQ, EINVAL
            return (size_t)-1;
        }
        m = encodeOne(cd->to, c, buf);
        if ((size_t)m > *outLeft)
        {
            lxSetErrno(7);   // E2BIG
            return (size_t)-1;
        }
        memcpy(*out, buf, (size_t)m);
        *out += m;
        *outLeft -= (size_t)m;
        *in += k;
        *inLeft -= (size_t)k;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// setjmp/longjmp, glibc's jmp_buf: rbx, rbp, r12..r15, rsp, rip (unmangled:
// only this process reads them).

extern void lxSetjmp(void), lxLongjmp(void);
__asm__(".globl lxSetjmp\n"
        "lxSetjmp:\n"
        "    mov %rbx, (%rdi)\n"
        "    mov %rbp, 8(%rdi)\n"
        "    mov %r12, 16(%rdi)\n"
        "    mov %r13, 24(%rdi)\n"
        "    mov %r14, 32(%rdi)\n"
        "    mov %r15, 40(%rdi)\n"
        "    lea 8(%rsp), %rdx\n"
        "    mov %rdx, 48(%rdi)\n"
        "    mov (%rsp), %rdx\n"
        "    mov %rdx, 56(%rdi)\n"
        "    movl $0, 64(%rdi)\n"
        "    xor %eax, %eax\n"
        "    ret\n"
        ".globl lxLongjmp\n"
        "lxLongjmp:\n"
        "    mov %esi, %eax\n"
        "    test %eax, %eax\n"
        "    jnz 1f\n"
        "    inc %eax\n"
        "1:  mov (%rdi), %rbx\n"
        "    mov 8(%rdi), %rbp\n"
        "    mov 16(%rdi), %r12\n"
        "    mov 24(%rdi), %r13\n"
        "    mov 32(%rdi), %r14\n"
        "    mov 40(%rdi), %r15\n"
        "    mov 48(%rdi), %rsp\n"
        "    jmp *56(%rdi)\n");

// ---------------------------------------------------------------------------
// Refused: networking, udev, terminals, shared memory, timers.

static SYSV int lx_fail(void) { lxSetErrno(38); return -1; }
static SYSV int lx_fail_net(void) { lxSetErrno(97); return -1; }
static SYSV int lx_zero(void) { return 0; }
static SYSV void *lx_null(void) { return NULL; }
static SYSV int lx_getaddrinfo(const char *n, const char *s, const void *h, void **res)
{
    (void)n; (void)s; (void)h;
    *res = NULL;
    return -4;   // EAI_FAIL
}
static SYSV uint32_t lx_inet_addr(const char *s)
{
    unsigned a, b, c, d;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
        return 0xffffffffu;
    return a | b << 8 | c << 16 | d << 24;
}
static SYSV char *lx_inet_ntoa(uint32_t a)
{
    static __thread char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a & 255, (a >> 8) & 255, (a >> 16) & 255, a >> 24);
    return buf;
}
static SYSV const char *lx_inet_ntop(int af, const void *src, char *dst, unsigned n)
{
    const uint8_t *a = src;
    if (af == 2)
        snprintf(dst, n, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    else
        snprintf(dst, n, "::");
    return dst;
}
static int hErrno;
static SYSV int *lx_h_errno_location(void) { return &hErrno; }

// select and poll: no descriptor is ever ready; they wait out their timeout.
typedef struct
{
    int64_t tv_sec, tv_usec;
} LxTimeval;
static SYSV int lx_select(int n, void *r, void *w, void *e, LxTimeval *tv)
{
    size_t bytes = ((size_t)n + 63) / 64 * 8;
    if (r) memset(r, 0, bytes);
    if (w) memset(w, 0, bytes);
    if (e) memset(e, 0, bytes);
    if (tv && (tv->tv_sec || tv->tv_usec))
        Sleep((DWORD)(tv->tv_sec * 1000 + tv->tv_usec / 1000));
    else if (!tv)
        Sleep(100);
    return 0;
}
typedef struct
{
    int fd;
    short events, revents;
} LxPollfd;
static SYSV int lx_poll(LxPollfd *fds, uint64_t n, int timeout)
{
    for (uint64_t i = 0; i < n; i++)
        fds[i].revents = 0;
    if (timeout > 0)
        Sleep((DWORD)timeout);
    else if (timeout < 0)
        Sleep(100);
    return 0;
}
static SYSV lx_long lx_fdelt_chk(lx_long d) { return d / 64; }

// udev: an empty system, no device ever arriving.
static char udevObject[64];
static SYSV void *lx_udev_object(void) { return udevObject; }
static SYSV int lx_udev_fd(void) { return -1; }

static SYSV int lx_backtrace(void **b, int n) { (void)b; (void)n; return 0; }
static SYSV int lx_gnu_dev_major(uint64_t d) { return (int)((d >> 8) & 0xfff); }
static SYSV int lx_gnu_dev_minor(uint64_t d) { return (int)((d & 0xff) | ((d >> 12) & 0xfff00)); }
static SYSV uint64_t lx_gnu_dev_makedev(unsigned ma, unsigned mi) { return ((uint64_t)ma << 8) | mi; }

static const LxSym syms[] = {
    {"wcslen", lx_wcslen}, {"wcsnlen", lx_wcsnlen}, {"wcscmp", lx_wcscmp}, {"wcsncmp", lx_wcsncmp},
    {"wcscasecmp", lx_wcscasecmp}, {"wcscpy", lx_wcscpy}, {"__wcscpy_chk", lx_wcscpy_chk}, {"wcsncpy", lx_wcsncpy},
    {"wcscat", lx_wcscat}, {"wcsncat", lx_wcsncat}, {"wcschr", lx_wcschr}, {"wcsrchr", lx_wcsrchr},
    {"wcsstr", lx_wcsstr}, {"wcstok", lx_wcstok}, {"wmemchr", lx_wmemchr}, {"wmemcmp", lx_wmemcmp},
    {"wmemcpy", lx_wmemcpy}, {"__wmemcpy_chk", lx_wmemcpy_chk}, {"wmemmove", lx_wmemmove}, {"wmemset", lx_wmemset},
    {"wcstol", lx_wcstol}, {"wcstoll", lx_wcstol}, {"wcstoul", lx_wcstoul}, {"wcstoull", lx_wcstoul},
    {"wcstod", lx_wcstod}, {"wcstof", lx_wcstof},
    {"iswalpha", lx_iswalpha}, {"iswdigit", lx_iswdigit}, {"iswspace", lx_iswspace}, {"iswupper", lx_iswupper},
    {"iswlower", lx_iswlower}, {"iswalnum", lx_iswalnum}, {"iswprint", lx_iswprint}, {"iswpunct", lx_iswpunct},
    {"iswxdigit", lx_iswxdigit}, {"towlower", lx_towlower}, {"towupper", lx_towupper},
    {"__towlower_l", lx_towlower_l}, {"towlower_l", lx_towlower_l}, {"__towupper_l", lx_towupper_l},
    {"towupper_l", lx_towupper_l}, {"wctype", lx_wctype}, {"__wctype_l", lx_wctype_l}, {"wctype_l", lx_wctype_l},
    {"iswctype", lx_iswctype}, {"__iswctype_l", lx_iswctype_l}, {"iswctype_l", lx_iswctype_l},
    {"wctrans", lx_wctrans}, {"towctrans", lx_towctrans}, {"wcscoll", lx_wcscoll}, {"__wcscoll_l", lx_wcscoll_l},
    {"wcscoll_l", lx_wcscoll_l}, {"__wcsxfrm_l", lx_wcsxfrm_l}, {"wcsxfrm_l", lx_wcsxfrm_l},
    {"__wcsftime_l", lx_wcsftime_l}, {"btowc", lx_btowc}, {"wctob", lx_wctob}, {"mbrtowc", lx_mbrtowc},
    {"mbrlen", lx_mbrlen}, {"wcrtomb", lx_wcrtomb}, {"mbsinit", lx_mbsinit}, {"mbsnrtowcs", lx_mbsnrtowcs},
    {"mbsrtowcs", lx_mbsrtowcs}, {"__mbsrtowcs_chk", lx_mbsrtowcs_chk}, {"wcsnrtombs", lx_wcsnrtombs},
    {"wcsrtombs", lx_wcsrtombs}, {"mbstowcs", lx_mbstowcs}, {"__mbstowcs_chk", lx_mbstowcs_chk},
    {"wcstombs", lx_wcstombs}, {"mbtowc", lx_mbtowc}, {"wctomb", lx_wctomb},
    {"newlocale", lx_newlocale}, {"__newlocale", lx_newlocale}, {"duplocale", lx_duplocale},
    {"__duplocale", lx_duplocale}, {"freelocale", lx_freelocale}, {"__freelocale", lx_freelocale},
    {"uselocale", lx_uselocale}, {"__uselocale", lx_uselocale}, {"setlocale", lx_setlocale},
    {"nl_langinfo", lx_nl_langinfo}, {"nl_langinfo_l", lx_nl_langinfo_l}, {"__nl_langinfo_l", lx_nl_langinfo_l},
    {"gettext", lx_gettext}, {"dgettext", lx_dgettext}, {"dcgettext", lx_dcgettext},
    {"bindtextdomain", lx_bindtextdomain}, {"bind_textdomain_codeset", lx_bind_textdomain_codeset},
    {"textdomain", lx_textdomain}, {"iconv_open", lx_iconv_open}, {"iconv", lx_iconv}, {"iconv_close", lx_iconv_close},
    {"_setjmp", lxSetjmp}, {"setjmp", lxSetjmp}, {"__sigsetjmp", lxSetjmp}, {"sigsetjmp", lxSetjmp},
    {"longjmp", lxLongjmp}, {"_longjmp", lxLongjmp}, {"__longjmp_chk", lxLongjmp}, {"siglongjmp", lxLongjmp},
    {"socket", lx_fail_net}, {"connect", lx_fail_net}, {"bind", lx_fail_net}, {"listen", lx_fail_net},
    {"accept", lx_fail_net}, {"send", lx_fail_net}, {"sendto", lx_fail_net}, {"recv", lx_fail_net},
    {"recvfrom", lx_fail_net}, {"shutdown", lx_fail_net}, {"setsockopt", lx_fail_net},
    {"getsockopt", lx_fail_net}, {"getsockname", lx_fail_net}, {"getpeername", lx_fail_net},
    {"getaddrinfo", lx_getaddrinfo}, {"freeaddrinfo", lx_zero}, {"getnameinfo", lx_fail_net},
    {"gethostbyname", lx_null}, {"getservbyname", lx_null}, {"getifaddrs", lx_fail_net}, {"freeifaddrs", lx_zero},
    {"inet_addr", lx_inet_addr}, {"inet_ntoa", lx_inet_ntoa}, {"inet_ntop", lx_inet_ntop},
    {"__h_errno_location", lx_h_errno_location}, {"select", lx_select}, {"poll", lx_poll},
    {"__fdelt_chk", lx_fdelt_chk}, {"sendfile", lx_fail}, {"sendfile64", lx_fail},
    {"udev_new", lx_udev_object}, {"udev_unref", lx_null}, {"udev_ref", lx_udev_object},
    {"udev_enumerate_new", lx_udev_object}, {"udev_enumerate_unref", lx_null},
    {"udev_enumerate_add_match_subsystem", lx_zero}, {"udev_enumerate_add_match_property", lx_zero},
    {"udev_enumerate_scan_devices", lx_zero}, {"udev_enumerate_get_list_entry", lx_null},
    {"udev_list_entry_get_next", lx_null}, {"udev_list_entry_get_name", lx_null},
    {"udev_device_new_from_syspath", lx_null}, {"udev_device_unref", lx_null},
    {"udev_device_get_devnode", lx_null}, {"udev_device_get_sysname", lx_null}, {"udev_device_get_action", lx_null},
    {"udev_device_get_property_value", lx_null}, {"udev_device_get_parent_with_subsystem_devtype", lx_null},
    {"udev_device_get_sysattr_value", lx_null},
    {"udev_monitor_new_from_netlink", lx_udev_object}, {"udev_monitor_unref", lx_null},
    {"udev_monitor_filter_add_match_subsystem_devtype", lx_zero}, {"udev_monitor_enable_receiving", lx_zero},
    {"udev_monitor_get_fd", lx_udev_fd}, {"udev_monitor_receive_device", lx_null},
    {"tcgetattr", lx_fail}, {"tcsetattr", lx_fail}, {"tcflush", lx_fail}, {"tcdrain", lx_fail},
    {"cfgetispeed", lx_zero}, {"cfsetispeed", lx_zero}, {"cfsetospeed", lx_zero}, {"ttyname_r", lx_fail},
    {"shmget", lx_fail}, {"shmat", lx_fail}, {"shmdt", lx_fail}, {"shmctl", lx_fail},
    {"timerfd_create", lx_fail}, {"timerfd_settime", lx_fail}, {"eventfd", lx_fail}, {"inotify_init", lx_fail},
    {"syscall", lx_fail},
    {"setmntent", lx_null}, {"getmntent", lx_null}, {"endmntent", lx_zero}, {"setutent", lx_zero},
    {"getutent_r", lx_fail}, {"endutent", lx_zero}, {"backtrace", lx_backtrace}, {"backtrace_symbols", lx_null},
    {"backtrace_symbols_fd", lx_zero}, {"gnu_dev_major", lx_gnu_dev_major}, {"gnu_dev_minor", lx_gnu_dev_minor},
    {"gnu_dev_makedev", lx_gnu_dev_makedev}, {"register_printf_specifier", lx_zero},
    {"register_printf_modifier", lx_zero}, {"register_printf_type", lx_zero},
    {"__cxa_thread_atexit", lx_zero},
};

void lxMiscInit(void)
{
    localeInit();
    lxRegister(syms, LX_COUNT(syms));
}
