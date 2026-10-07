// NVIDIA's extended assembly programs (Tank! Tank! Tank!: Cg's vp40/fp40
// output, "OPTION NV_vertex_program3" and "OPTION NV_fragment_program2")
// rewritten as plain ARB_vertex_program / ARB_fragment_program, which Mesa
// runs: it rejects the NV options, and the game's 3D drew white.
//
// Only what the game's programs use is handled:
// - both: the option, the condition-code registers RC/HC, labels;
// - vertex: _SAT (not in ARB vertex programs: MAX/MIN through a temporary),
//   the four-component address register (ARB has A0.x only: ARL stores the
//   floored value in a temporary, each relative operand reloads A0.x from it
//   and is fetched into a temporary);
// - fragment: the precision suffixes (R, H, X) and SHORT; the condition code
//   (C suffix: a temporary CC), conditional writes (GT/NE/EQ.x) and IF/ENDIF
//   blocks made selects (CMP); a LOOP with a constant count, unrolled.
// A program using anything else is returned as it is (and Mesa rejects it).

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "namcoEs1.h"

typedef struct
{
    char *text;
    size_t len, cap;
} Buf;

static void put(Buf *b, const char *fmt, ...)
{
    va_list ap;
    for (;;)
    {
        va_start(ap, fmt);
        int n = vsnprintf(b->text + b->len, b->cap - b->len, fmt, ap);
        va_end(ap);
        if (n >= 0 && b->len + n < b->cap)
        {
            b->len += n;
            return;
        }
        b->cap = (b->cap + n + 1) * 2;
        b->text = realloc(b->text, b->cap);
    }
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return s;
}

// Operands split at top-level commas (not inside [] or ()).
static int splitOperands(char *s, char **ops, int max)
{
    int n = 0, depth = 0;
    char *start = s;
    for (char *p = s;; p++)
    {
        if (*p == '[' || *p == '(' || *p == '{')
            depth++;
        else if (*p == ']' || *p == ')' || *p == '}')
            depth--;
        else if ((*p == ',' && depth == 0) || *p == '\0')
        {
            int end = *p == '\0';
            *p = '\0';
            if (n < max)
                ops[n++] = trim(start);
            if (end)
                break;
            start = p + 1;
        }
    }
    return n;
}

// The opcode's parts: base, condition-code update (C), saturation.
typedef struct
{
    char base[16];
    int updateCC, saturate;
} Opcode;

static const char *const bases[] = {"MOV", "ADD", "MAD", "MUL", "DP3", "DP4", "DPH", "RSQ", "RCP", "MAX", "MIN", "SLT",
                                    "SGE", "SIN", "COS", "TEX", "TXP", "TXB", "FLR", "FRC", "ARL", "EX2", "LG2", "POW",
                                    "LRP", "CMP", "ABS", "SUB", "XPD", "LIT", "KIL", "SCS", "SWZ", NULL};

static int parseOpcode(const char *word, Opcode *op)
{
    memset(op, 0, sizeof(*op));
    for (int i = 0; bases[i]; i++)
    {
        size_t n = strlen(bases[i]);
        if (strncmp(word, bases[i], n))
            continue;
        const char *rest = word + n;
        if (*rest == 'R' || *rest == 'H' || *rest == 'X')
            rest++;
        if (*rest == 'C')
        {
            op->updateCC = 1;
            rest++;
        }
        if (!strcmp(rest, "_SAT"))
        {
            op->saturate = 1;
            rest += 4;
        }
        if (*rest)
            continue;
        strcpy(op->base, bases[i]);
        return 1;
    }
    return 0;
}

// A destination: register and write mask, and its condition ("GT.x"...).
static void splitDestination(const char *dst, char *reg, char *mask, char *cond)
{
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s", dst);
    cond[0] = mask[0] = '\0';
    char *paren = strchr(tmp, '(');
    if (paren)
    {
        char *close = strchr(paren, ')');
        if (close)
            *close = '\0';
        snprintf(cond, 16, "%s", paren + 1);
        *paren = '\0';
    }
    char *dot = strrchr(tmp, '.');
    // a mask follows the last '.', unless that '.' is part of a binding name
    // (result.texcoord[0] has no mask; result.color.secondary.w has one)
    if (dot && strspn(dot + 1, "xyzw") == strlen(dot + 1) && dot[1])
    {
        snprintf(mask, 8, "%s", dot + 1);
        *dot = '\0';
    }
    snprintf(reg, 96, "%s", trim(tmp));
}

static int isCCRegister(const char *reg)
{
    return !strcmp(reg, "RC") || !strcmp(reg, "HC");
}

// ---------------------------------------------------------------------------
// Vertex programs.

static char *vertexProgram(const char *src)
{
    Buf out = {0};
    char *copy = strdup(src), *save = NULL;
    int declared = 0;

    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
    {
        char *s = trim(line);
        if (!strncmp(s, "OPTION NV_vertex_program", 24) || !strncmp(s, "TEMP RC", 7) || !strcmp(s, "BB1:"))
            continue;
        if (!strncmp(s, "ADDRESS", 7))
        {
            put(&out, "ADDRESS A0;\n");
            continue;
        }
        Opcode op;
        char word[32] = "";
        sscanf(s, "%31s", word);
        if (!parseOpcode(word, &op))
        {
            put(&out, "%s\n", s);
            continue;
        }
        // the loader's temporaries and constants, before the first instruction
        if (!declared)
        {
            put(&out, "TEMP nvAddr, nvRel0, nvRel1, nvRel2, nvSat;\nPARAM nvSat01 = { 0, 1, 0, 0 };\n");
            declared = 1;
        }

        char body[1024];
        snprintf(body, sizeof(body), "%s", s + strlen(word));
        char *semi = strrchr(body, ';');
        if (semi)
            *semi = '\0';
        char *ops[4];
        int n = splitOperands(body, ops, 4);
        if (n < 1 || op.updateCC)
            goto unsupported;

        if (!strcmp(op.base, "ARL"))
        {
            char reg[96], mask[8], cond[16];
            splitDestination(ops[0], reg, mask, cond);
            put(&out, "FLR nvAddr.%s, %s;\n", mask[0] ? mask : "xyzw", ops[1]);
            continue;
        }
        // relative operands: c[A0.k + n] fetched into temporaries
        char fixed[4][160];
        for (int i = 1; i < n; i++)
        {
            char *rel = strstr(ops[i], "[A0.");
            if (!rel)
            {
                snprintf(fixed[i], sizeof(fixed[i]), "%s", ops[i]);
                continue;
            }
            char comp = rel[4];
            const char *close = strchr(rel, ']');
            char inside[64];
            snprintf(inside, sizeof(inside), "%.*s", (int)(close - (rel + 5)), rel + 5);
            // the array name before '['
            const char *name = ops[i];
            int negate = 0;
            if (*name == '-')
            {
                negate = 1;
                name++;
            }
            put(&out, "ARL A0.x, nvAddr.%c;\nMOV nvRel%d, %.*s[A0.x%s];\n", comp, i - 1, (int)(rel - name), name, inside);
            snprintf(fixed[i], sizeof(fixed[i]), "%snvRel%d%s", negate ? "-" : "", i - 1, close + 1);
        }
        char srcs[512] = "";
        for (int i = 1; i < n; i++)
        {
            strcat(srcs, ", ");
            strcat(srcs, fixed[i]);
        }
        if (!op.saturate)
        {
            put(&out, "%s %s%s;\n", op.base, ops[0], srcs);
            continue;
        }
        char reg[96], mask[8], cond[16];
        splitDestination(ops[0], reg, mask, cond);
        const char *m = mask[0] ? mask : "xyzw";
        put(&out, "%s nvSat.%s%s;\nMAX nvSat.%s, nvSat, nvSat01.x;\nMIN %s, nvSat, nvSat01.y;\n", op.base, m, srcs, m, ops[0]);
    }
    free(copy);
    return out.text;

unsupported:
    free(copy);
    free(out.text);
    return NULL;
}

// ---------------------------------------------------------------------------
// Fragment programs.

#define MAX_DEPTH 16

typedef struct
{
    Buf out;
    int depth; // IF nesting: nvMask0..depth-1 hold each level's mask (0/1)
} Fragment;

// cond ("GT.x"...) evaluated into nvCond.x as 0 or 1, combined with the IF
// masks; returns 0 when the write is unconditional.
static int predicate(Fragment *f, const char *cond)
{
    if (cond[0])
    {
        char test[4] = "";
        char comp = 'x';
        sscanf(cond, "%2s", test);
        const char *dot = strchr(cond, '.');
        if (dot)
            comp = dot[1];
        if (!strcmp(test, "GT"))
            put(&f->out, "SLT nvCond.x, nvZero.x, nvCC.%c;\n", comp);
        else if (!strcmp(test, "NE"))
            put(&f->out, "ABS nvCond.x, nvCC.%c;\nSLT nvCond.x, nvZero.x, nvCond.x;\n", comp);
        else if (!strcmp(test, "EQ"))
            put(&f->out, "ABS nvCond.x, nvCC.%c;\nSGE nvCond.x, nvZero.x, nvCond.x;\n", comp);
        else
            return -1;
        if (f->depth)
            put(&f->out, "MUL nvCond.x, nvCond.x, nvMask%d.x;\n", f->depth - 1);
        return 1;
    }
    if (f->depth)
    {
        put(&f->out, "MOV nvCond.x, nvMask%d.x;\n", f->depth - 1);
        return 1;
    }
    return 0;
}

// One instruction (the LOOP body is replayed through it too).
static int fragmentInstruction(Fragment *f, char *s)
{
    char word[32] = "";
    sscanf(s, "%31s", word);

    if (!strcmp(word, "IF"))
    {
        char cond[16] = "";
        sscanf(s + 2, " %15[^;]", cond);
        if (f->depth >= MAX_DEPTH || predicate(f, cond) <= 0)
            return 0;
        put(&f->out, "MOV nvMask%d.x, nvCond.x;\n", f->depth);
        f->depth++;
        return 1;
    }
    if (!strncmp(word, "ENDIF", 5))
    {
        if (!f->depth)
            return 0;
        f->depth--;
        return 1;
    }

    Opcode op;
    if (!parseOpcode(word, &op))
        return 0;
    char body[1024];
    snprintf(body, sizeof(body), "%s", s + strlen(word));
    char *semi = strrchr(body, ';');
    if (semi)
        *semi = '\0';
    char *ops[5];
    int n = splitOperands(body, ops, 5);
    if (n < 1)
        return 0;

    char reg[96], mask[8], cond[16];
    splitDestination(ops[0], reg, mask, cond);
    const char *m = mask[0] ? mask : "xyzw";
    int toCC = isCCRegister(reg);
    if (toCC)
        snprintf(reg, sizeof(reg), "nvCC");

    char srcs[512] = "";
    for (int i = 1; i < n; i++)
    {
        strcat(srcs, ", ");
        strcat(srcs, ops[i]);
    }
    const char *sat = op.saturate ? "_SAT" : "";
    int predicated = predicate(f, cond);
    if (predicated < 0)
        return 0;
    if (!predicated)
        put(&f->out, "%s%s %s.%s%s;\n", op.base, sat, reg, m, srcs);
    else
    {
        // computed apart, then selected into the destination where it holds
        put(&f->out, "%s%s nvTmp.%s%s;\nCMP %s.%s, -nvCond.xxxx, nvTmp, %s;\n", op.base, sat, m, srcs, reg, m, reg);
    }
    // the condition code follows the result
    if (op.updateCC && !toCC)
        put(&f->out, "MOV nvCC.%s, %s;\n", m, reg);
    return 1;
}

// A literal of the program's "PARAM c[N] = { {a, b, c, d}, ... }": c[row].x.
static int paramLiteral(const char *src, int row, float *value)
{
    const char *p = strstr(src, "PARAM c[");
    if (!p || !(p = strchr(p, '{')))
        return 0;
    p++;
    for (int r = 0; r <= row; r++)
    {
        p = strchr(p, '{');
        if (!p)
            return 0;
        if (r < row)
            p = strchr(p, '}');
        if (!p)
            return 0;
        p++;
    }
    return sscanf(p, " %f", value) == 1;
}

static char *fragmentProgram(const char *src)
{
    Fragment f = {0};
    char *copy = strdup(src), *save = NULL;
    int declared = 0;
    char *loopBody[64];
    int loopLines = -1, loopCount = 0;
    char outputName[4][64], outputBinding[4][96];
    int outputs = 0;

    for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
    {
        char *s = trim(line);
        if (!strncmp(s, "OPTION NV_fragment_program", 26) || !strcmp(s, "TEMP RC;") || !strcmp(s, "TEMP HC;"))
            continue;
        if (!strncmp(s, "SHORT ", 6) || !strncmp(s, "LONG ", 5))
            s = trim(strchr(s, ' '));
        if (!strncmp(s, "LOOP", 4))
        {
            int row;
            float count;
            if (loopLines >= 0 || sscanf(s, "LOOP c[%d]", &row) != 1 || !paramLiteral(src, row, &count) ||
                count < 1 || count > 64)
                goto unsupported;
            loopLines = 0;
            loopCount = (int)count;
            continue;
        }
        if (!strncmp(s, "ENDLOOP", 7))
        {
            if (loopLines < 0)
                goto unsupported;
            for (int k = 0; k < loopCount; k++)
                for (int i = 0; i < loopLines; i++)
                {
                    char tmp[1024];
                    snprintf(tmp, sizeof(tmp), "%s", loopBody[i]);
                    if (!fragmentInstruction(&f, tmp))
                        goto unsupported;
                }
            loopLines = -1;
            continue;
        }
        if (loopLines >= 0)
        {
            if (loopLines == 64)
                goto unsupported;
            loopBody[loopLines++] = s;
            continue;
        }

        char word[32] = "";
        sscanf(s, "%31s", word);
        // Outputs cannot be read back (the selects do): a temporary each,
        // copied to the output at the end.
        char name[64], binding[96];
        if (sscanf(s, "OUTPUT %63s = %95[^;];", name, binding) == 2)
        {
            if (outputs == 4)
                goto unsupported;
            snprintf(outputName[outputs], sizeof(outputName[0]), "%s", name);
            snprintf(outputBinding[outputs], sizeof(outputBinding[0]), "%s", binding);
            outputs++;
            put(&f.out, "TEMP %s;\n", name);
            continue;
        }
        if (!strcmp(s, "END"))
        {
            for (int i = 0; i < outputs; i++)
                put(&f.out, "MOV %s, %s;\n", outputBinding[i], outputName[i]);
            put(&f.out, "END\n");
            continue;
        }
        Opcode op;
        if (strcmp(word, "IF") && strncmp(word, "ENDIF", 5) && !parseOpcode(word, &op))
        {
            put(&f.out, "%s\n", s);
            continue;
        }
        if (!declared)
        {
            put(&f.out, "TEMP nvCC, nvCond, nvTmp");
            for (int d = 0; d < MAX_DEPTH; d++)
                put(&f.out, ", nvMask%d", d);
            put(&f.out, ";\nPARAM nvZero = { 0, 0, 0, 0 };\n");
            // The output components a program leaves unwritten (blur writes
            // only alpha) are 0, as NVIDIA's: not a temporary's leftovers.
            for (int i = 0; i < outputs; i++)
                put(&f.out, "MOV %s, nvZero;\n", outputName[i]);
            declared = 1;
        }
        if (!fragmentInstruction(&f, s))
            goto unsupported;
    }
    free(copy);
    if (f.depth)
    {
        free(f.out.text);
        return NULL;
    }
    return f.out.text;

unsupported:
    free(copy);
    free(f.out.text);
    return NULL;
}

char *namcoEs1ArbTranslate(const char *src, int len)
{
    char *text = malloc(len + 1), *out = NULL;
    if (!text)
        return NULL;
    memcpy(text, src, len);
    text[len] = '\0';
    if (strstr(text, "OPTION NV_vertex_program"))
        out = vertexProgram(text);
    else if (strstr(text, "OPTION NV_fragment_program"))
        out = fragmentProgram(text);
    free(text);
    return out;
}
