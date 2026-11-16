// libGL and libGLU (see ll64.h): System V entry points for Windows' GL. Their
// real functions are looked up once the first context is current.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glad/gl.h>
#include "ll64.h"
#include "gl64Wrappers.h"

static HMODULE opengl32, glu32;
static int resolved;

void lxGlResolve(void)
{
    int found = 0;
    if (!opengl32)
        opengl32 = LoadLibraryA("opengl32.dll");
    for (size_t i = 0; i < LX_COUNT(glFunctions); i++)
    {
        PROC p = wglGetProcAddress(glFunctions[i].name);
        if ((uintptr_t)p <= 3 || p == (PROC)-1)
            p = GetProcAddress(opengl32, glFunctions[i].name);
        *glFunctions[i].real = (void *)p;
        found += p != NULL;
    }
    resolved = 1;
    lxLog("GL: %d of %zu functions\n", found, LX_COUNT(glFunctions));
}

// Shader and program failures, always logged.
static SYSV void c_glCompileShader(GLuint sh)
{
    GLint ok = 0;
    r_glCompileShader(sh);
    r_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = "", src[600] = "";
        r_glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        r_glGetShaderSource(sh, sizeof(src), NULL, src);
        lxLog("shader %u does not compile: %s\n--- source:\n%s\n---\n", sh, log, src);
    }
}
static SYSV void c_glLinkProgram(GLuint p)
{
    GLint ok = 0;
    r_glLinkProgram(p);
    r_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024] = "";
        r_glGetProgramInfoLog(p, sizeof(log), NULL, log);
        lxLog("program %u does not link: %s\n", p, log);
    }
}
static const LxSym checkSyms[] = {{"glCompileShader", c_glCompileShader}, {"glLinkProgram", c_glLinkProgram}};
// LL64_GLCALLS: each GL function's calls, every 300 frames.
void lxGlCountReport(void)
{
    static int report = -1;
    if (report < 0)
        report = getenv("LL64_GLCALLS") != NULL;
    if (report)
    {
        char line[4096];
        size_t o = 0;
        for (size_t i = 0; i < LX_COUNT(glFunctions); i++)
        {
            if (*glFunctions[i].calls && o + 64 < sizeof(line))
                o += (size_t)snprintf(line + o, sizeof(line) - o, " %s=%u", glFunctions[i].name, *glFunctions[i].calls);
            *glFunctions[i].calls = 0;
        }
        lxLog("GL calls over 300 frames:%s\n", line);
    }
}

void *lxGlProc(const char *name)
{
    for (size_t i = 0; i < LX_COUNT(checkSyms); i++)
        if (!strcmp(checkSyms[i].name, name))
            return checkSyms[i].fn;

    for (size_t i = 0; i < LX_COUNT(glFunctions); i++)
        if (!strcmp(glFunctions[i].name, name))
            return !resolved || *glFunctions[i].real ? glFunctions[i].wrapper : NULL;
    return NULL;
}

// GLU, from glu32.
static FARPROC gluProc(const char *name)
{
    if (!glu32)
        glu32 = LoadLibraryA("glu32.dll");
    return glu32 ? GetProcAddress(glu32, name) : NULL;
}
#define GLU(ret, name, params, args)                                                                                   \
    static SYSV ret lx_##name params                                                                                   \
    {                                                                                                                  \
        static ret(WINAPI * real) params;                                                                              \
        if (!real)                                                                                                     \
            *(FARPROC *)&real = gluProc(#name);                                                                        \
        return real args;                                                                                              \
    }
GLU(const GLubyte *, gluErrorString, (GLenum e), (e))
GLU(int, gluBuild2DMipmaps, (GLenum t, GLint f, GLsizei w, GLsizei h, GLenum fmt, GLenum ty, const void *d),
    (t, f, w, h, fmt, ty, d))
GLU(int, gluBuild1DMipmaps, (GLenum t, GLint f, GLsizei w, GLenum fmt, GLenum ty, const void *d), (t, f, w, fmt, ty, d))
GLU(int, gluScaleImage, (GLenum fmt, GLint wi, GLint hi, GLenum ti, const void *di, GLint wo, GLint ho, GLenum to,
                         void *dout), (fmt, wi, hi, ti, di, wo, ho, to, dout))
GLU(void, gluPerspective, (GLdouble a, GLdouble b, GLdouble c, GLdouble d), (a, b, c, d))
GLU(void, gluOrtho2D, (GLdouble a, GLdouble b, GLdouble c, GLdouble d), (a, b, c, d))
GLU(void, gluLookAt, (GLdouble a, GLdouble b, GLdouble c, GLdouble d, GLdouble e, GLdouble f, GLdouble g, GLdouble h,
                      GLdouble i), (a, b, c, d, e, f, g, h, i))
GLU(int, gluProject, (GLdouble x, GLdouble y, GLdouble z, const GLdouble *m, const GLdouble *p, const GLint *v,
                      GLdouble *wx, GLdouble *wy, GLdouble *wz), (x, y, z, m, p, v, wx, wy, wz))
GLU(int, gluUnProject, (GLdouble x, GLdouble y, GLdouble z, const GLdouble *m, const GLdouble *p, const GLint *v,
                        GLdouble *ox, GLdouble *oy, GLdouble *oz), (x, y, z, m, p, v, ox, oy, oz))

static const LxSym gluSyms[] = {
    {"gluErrorString", lx_gluErrorString}, {"gluBuild2DMipmaps", lx_gluBuild2DMipmaps},
    {"gluBuild1DMipmaps", lx_gluBuild1DMipmaps}, {"gluScaleImage", lx_gluScaleImage},
    {"gluPerspective", lx_gluPerspective}, {"gluOrtho2D", lx_gluOrtho2D}, {"gluLookAt", lx_gluLookAt},
    {"gluProject", lx_gluProject}, {"gluUnProject", lx_gluUnProject},
};

void lxGlInit(void)
{
    static LxSym syms[LX_COUNT(glFunctions)];
    for (size_t i = 0; i < LX_COUNT(glFunctions); i++)
    {
        syms[i].name = glFunctions[i].name;
        syms[i].fn = glFunctions[i].wrapper;
    }
    lxRegister(syms, LX_COUNT(syms));
    lxRegister(gluSyms, LX_COUNT(gluSyms));
    lxRegister(checkSyms, LX_COUNT(checkSyms));
}
