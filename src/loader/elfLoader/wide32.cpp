#include "wide32.hpp"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{
// A conversion of a format: its flags, width and precision (as a narrow
// printf or scanf spelling), its length modifier and its letter.
struct Spec
{
    char text[64];
    size_t textLen;
    bool leftAlign, hasPrecision, suppress;
    int width, precision;
    char length[3];
    uint32_t conversion;
};

void specAppend(Spec &spec, const char *s)
{
    size_t n = strlen(s);
    if (spec.textLen + n < sizeof(spec.text))
    {
        memcpy(spec.text + spec.textLen, s, n + 1);
        spec.textLen += n;
    }
}

void specAppendChar(Spec &spec, char c)
{
    char s[2] = {c, 0};
    specAppend(spec, s);
}

// The length modifier and the letter, after flags, width and precision.
const uint32_t *parseLength(Spec &spec, const uint32_t *f)
{
    size_t n = 0;
    while (*f == 'h' || *f == 'l' || *f == 'L' || *f == 'q' || *f == 'j' || *f == 'z' || *f == 't')
    {
        if (n < 2)
            spec.length[n++] = (char)*f;
        f++;
    }
    spec.length[n] = 0;
    spec.conversion = *f;
    return f;
}

bool isWideLength(const Spec &spec)
{
    return spec.length[0] == 'l';
}

void pushPadded(std::vector<uint32_t> &out, const uint32_t *s, size_t len, const Spec &spec)
{
    size_t pad = spec.width > 0 && (size_t)spec.width > len ? (size_t)spec.width - len : 0;
    if (!spec.leftAlign)
        out.insert(out.end(), pad, ' ');
    out.insert(out.end(), s, s + len);
    if (spec.leftAlign)
        out.insert(out.end(), pad, ' ');
}

void pushNarrow(std::vector<uint32_t> &out, const char *s, int len)
{
    for (int i = 0; i < len; i++)
        out.push_back((unsigned char)s[i]);
}

// %s and %ls: at most the precision, (null) as glibc for a null one.
void pushString(std::vector<uint32_t> &out, const void *arg, bool wide, const Spec &spec)
{
    static const uint32_t null[] = {'(', 'n', 'u', 'l', 'l', ')', 0};
    std::vector<uint32_t> text;
    size_t limit = spec.hasPrecision ? (size_t)spec.precision : (size_t)-1;

    if (!arg)
    {
        arg = null;
        wide = true;
    }
    for (size_t i = 0; i < limit; i++)
    {
        uint32_t c = wide ? ((const uint32_t *)arg)[i] : ((const unsigned char *)arg)[i];
        if (!c)
            break;
        text.push_back(c);
    }
    pushPadded(out, text.data(), text.size(), spec);
}

void format(std::vector<uint32_t> &out, const uint32_t *f, va_list args)
{
    char buf[512];

    for (; *f; f++)
    {
        if (*f != '%')
        {
            out.push_back(*f);
            continue;
        }
        const uint32_t *start = f++;
        Spec spec = {};
        specAppend(spec, "%");
        for (; *f && strchr("-+ #0'", (int)*f); f++)
        {
            if (*f == '-')
                spec.leftAlign = true;
            // The thousands' grouping of the C locale is none.
            if (*f != '\'')
                specAppendChar(spec, (char)*f);
        }
        if (*f == '*')
        {
            spec.width = va_arg(args, int);
            if (spec.width < 0)
            {
                spec.leftAlign = true;
                spec.width = -spec.width;
                specAppend(spec, "-");
            }
            f++;
        }
        else
            for (; *f >= '0' && *f <= '9'; f++)
                spec.width = spec.width * 10 + (int)(*f - '0');
        if (spec.width)
        {
            snprintf(buf, sizeof(buf), "%d", spec.width);
            specAppend(spec, buf);
        }
        if (*f == '.')
        {
            f++;
            spec.hasPrecision = true;
            if (*f == '*')
            {
                spec.precision = va_arg(args, int);
                if (spec.precision < 0)
                    spec.hasPrecision = false;
                f++;
            }
            else
                for (; *f >= '0' && *f <= '9'; f++)
                    spec.precision = spec.precision * 10 + (int)(*f - '0');
            if (spec.hasPrecision)
            {
                snprintf(buf, sizeof(buf), ".%d", spec.precision);
                specAppend(spec, buf);
            }
        }
        f = parseLength(spec, f);
        if (!*f)
        {
            out.insert(out.end(), start, f);
            break;
        }

        int n = 0;
        const char *len = spec.length;
        switch (*f)
        {
        case '%':
            out.push_back('%');
            continue;
        case 'c':
        case 'C':
        {
            uint32_t c = va_arg(args, uint32_t);
            pushPadded(out, &c, 1, spec);
            continue;
        }
        case 's':
        case 'S':
            pushString(out, va_arg(args, const void *), *f == 'S' || isWideLength(spec), spec);
            continue;
        case 'n':
            *va_arg(args, int *) = (int)out.size();
            continue;
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            specAppend(spec, !strcmp(len, "ll") || !strcmp(len, "q") ? "ll" : !strcmp(len, "hh") ? "hh" : len[0] == 'h' ? "h" : "");
            specAppendChar(spec, (char)*f);
            if (!strcmp(len, "ll") || !strcmp(len, "q"))
                n = snprintf(buf, sizeof(buf), spec.text, va_arg(args, long long));
            else
                n = snprintf(buf, sizeof(buf), spec.text, va_arg(args, int));
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            specAppendChar(spec, (char)*f);
            n = snprintf(buf, sizeof(buf), spec.text, len[0] == 'L' ? (double)va_arg(args, long double) : va_arg(args, double));
            break;
        case 'p':
        {
            // glibc's spelling, not the CRT's.
            void *p = va_arg(args, void *);
            char hex[32];
            if (p)
                snprintf(hex, sizeof(hex), "0x%lx", (unsigned long)(uintptr_t)p);
            else
                snprintf(hex, sizeof(hex), "(nil)");
            std::vector<uint32_t> text;
            pushNarrow(text, hex, (int)strlen(hex));
            pushPadded(out, text.data(), text.size(), spec);
            continue;
        }
        default:
            out.insert(out.end(), start, f + 1);
            continue;
        }
        if (n > 0)
            pushNarrow(out, buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
    }
}

// A wide string's characters as bytes, for the CRT's number parsers.
std::vector<char> narrowed(const uint32_t *s)
{
    std::vector<char> text;
    for (; *s; s++)
        text.push_back(*s < 0x80 ? (char)*s : '?');
    text.push_back(0);
    return text;
}

bool isSpace(uint32_t c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}
} // namespace

int wide32Vsnprintf(uint32_t *s, size_t n, const uint32_t *fmt, va_list args)
{
    std::vector<uint32_t> out;
    format(out, fmt, args);
    if (!n)
        return -1;
    size_t copied = out.size() < n ? out.size() : n - 1;
    memcpy(s, out.data(), copied * sizeof(uint32_t));
    s[copied] = 0;
    // As glibc: -1 when it did not fit.
    return out.size() < n ? (int)out.size() : -1;
}

int wide32Vfprintf(FILE *stream, const uint32_t *fmt, va_list args)
{
    std::vector<uint32_t> out;
    format(out, fmt, args);
    for (uint32_t c : out)
        fputc(c < 0x100 ? (int)c : '?', stream);
    return (int)out.size();
}

int wide32Vsscanf(const uint32_t *s, const uint32_t *fmt, va_list args)
{
    const uint32_t *in = s, *f = fmt;
    std::vector<char> narrow = narrowed(s);
    int assigned = 0;

    for (; *f; f++)
    {
        if (isSpace(*f))
        {
            while (isSpace(*in))
                in++;
            continue;
        }
        if (*f != '%' || f[1] == '%')
        {
            if (*f == '%')
                f++;
            if (!*in)
                return assigned ? assigned : -1;
            if (*in != *f)
                return assigned;
            in++;
            continue;
        }
        f++;
        Spec spec = {};
        if (*f == '*')
        {
            spec.suppress = true;
            f++;
        }
        for (; *f >= '0' && *f <= '9'; f++)
            spec.width = spec.width * 10 + (int)(*f - '0');
        f = parseLength(spec, f);
        if (!*f)
            break;

        if (*f == 'n')
        {
            if (!spec.suppress)
                *va_arg(args, int *) = (int)(in - s);
            continue;
        }
        if (*f != 'c' && *f != 'C')
            while (isSpace(*in))
                in++;
        if (!*in)
            return assigned ? assigned : -1;

        if (*f == 'c' || *f == 'C')
        {
            int count = spec.width ? spec.width : 1;
            bool wide = *f == 'C' || isWideLength(spec);
            void *dst = spec.suppress ? nullptr : va_arg(args, void *);
            for (int i = 0; i < count && in[0]; i++, in++)
            {
                if (dst && wide)
                    ((uint32_t *)dst)[i] = *in;
                else if (dst)
                    ((char *)dst)[i] = (char)*in;
            }
            assigned += dst != nullptr;
            continue;
        }
        if (*f == 's' || *f == 'S')
        {
            bool wide = *f == 'S' || isWideLength(spec);
            void *dst = spec.suppress ? nullptr : va_arg(args, void *);
            int i = 0;
            for (; *in && !isSpace(*in) && (!spec.width || i < spec.width); i++, in++)
            {
                if (dst && wide)
                    ((uint32_t *)dst)[i] = *in;
                else if (dst)
                    ((char *)dst)[i] = (char)*in;
            }
            if (dst && wide)
                ((uint32_t *)dst)[i] = 0;
            else if (dst)
                ((char *)dst)[i] = 0;
            assigned += dst != nullptr;
            continue;
        }

        // A number: the CRT's parser, on the same text narrowed.
        char spelled[32];
        const char *len = spec.length;
        const char *modifier = !strcmp(len, "ll") || !strcmp(len, "q") ? "ll" : !strcmp(len, "hh") ? "hh" : len[0] == 'h' ? "h" : len[0] == 'l' ? "l" : "";
        bool isFloat = strchr("feEgGaAF", (int)*f) != nullptr;
        if (!isFloat && !strchr("diuxXo", (int)*f))
            return assigned; // A conversion no game of these uses (%[).
        const char *from = narrow.data() + (in - s);
        int used = -1;
        snprintf(spelled, sizeof(spelled), "%%%s%.0d%s%c%%n", spec.suppress ? "*" : "", spec.width,
                 isFloat ? (len[0] == 'L' ? "l" : len[0] == 'l' ? "l" : "") : modifier, (char)*f);
        if (spec.suppress)
        {
            sscanf(from, spelled, &used);
            if (used < 0)
                return assigned;
        }
        else if (isFloat && len[0] == 'L')
        {
            double value;
            if (sscanf(from, spelled, &value, &used) != 1)
                return assigned;
            *va_arg(args, long double *) = value;
            assigned++;
        }
        else
        {
            if (sscanf(from, spelled, va_arg(args, void *), &used) != 1)
                return assigned;
            assigned++;
        }
        in += used;
    }
    return assigned;
}

uint32_t *wide32Cat(uint32_t *dst, const uint32_t *src)
{
    uint32_t *d = dst;
    while (*d)
        d++;
    while ((*d++ = *src++))
        ;
    return dst;
}

uint32_t *wide32Ncat(uint32_t *dst, const uint32_t *src, size_t n)
{
    uint32_t *d = dst;
    while (*d)
        d++;
    for (; n && *src; n--)
        *d++ = *src++;
    *d = 0;
    return dst;
}

int wide32Casecmp(const uint32_t *a, const uint32_t *b)
{
    for (;; a++, b++)
    {
        uint32_t ca = *a < 0x80 ? (uint32_t)tolower((int)*a) : *a;
        uint32_t cb = *b < 0x80 ? (uint32_t)tolower((int)*b) : *b;
        if (ca != cb)
            return ca < cb ? -1 : 1;
        if (!ca)
            return 0;
    }
}

uint32_t *wide32Tok(uint32_t *s, const uint32_t *delims, uint32_t **save)
{
    auto isDelim = [delims](uint32_t c)
    {
        for (const uint32_t *d = delims; *d; d++)
            if (*d == c)
                return true;
        return false;
    };

    if (!s)
        s = *save;
    while (*s && isDelim(*s))
        s++;
    if (!*s)
    {
        *save = s;
        return nullptr;
    }
    uint32_t *token = s;
    while (*s && !isDelim(*s))
        s++;
    if (*s)
        *s++ = 0;
    *save = s;
    return token;
}

long long wide32Toll(const uint32_t *s, uint32_t **end, int base)
{
    std::vector<char> text = narrowed(s);
    char *stop = nullptr;
    long long value = strtoll(text.data(), &stop, base);
    if (end)
        *end = (uint32_t *)s + (stop - text.data());
    return value;
}

unsigned long long wide32Toull(const uint32_t *s, uint32_t **end, int base)
{
    std::vector<char> text = narrowed(s);
    char *stop = nullptr;
    unsigned long long value = strtoull(text.data(), &stop, base);
    if (end)
        *end = (uint32_t *)s + (stop - text.data());
    return value;
}
