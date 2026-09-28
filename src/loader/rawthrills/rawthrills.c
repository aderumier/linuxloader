// Raw Thrills (g5 engine) support: game detection, init and the filesystem
// and GL fixes those games need. See rawthrills.h.

#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rawthrills.h"
#include "../config/config.h"
#include "../log/log.h"

extern uint32_t gId;

static const RtGame *game;
static char gameDir[PATH_MAX];
static char usbDir[PATH_MAX];
static char udevDir[PATH_MAX];

const RtGame *rtCurrentGame(void)
{
    static uint32_t cachedId;
    if (cachedId != gId)
    {
        cachedId = gId;
        game = rtGetGame(gId);
    }
    return game;
}

int isRawThrillsGame(void)
{
    return rtCurrentGame() != NULL;
}

const char *rtGameDir(void)
{
    return gameDir;
}

// ---------------------------------------------------------------------------
// Filesystem

static const char *join(char *buf, size_t size, const char *dir, const char *rest)
{
    snprintf(buf, size, "%s%s", dir, rest);
    return buf;
}

const char *rtRedirectPath(const char *path, char *buf, size_t size)
{
    const RtGame *g = rtCurrentGame();
    size_t rootLen;

    if (!g || !path || !gameDir[0])
        return path;

    // The scripts ship encrypted for the dongle; the dump comes with a
    // decrypted copy, which is what the game must load.
    if (g->encryptedScripts && g->decryptedScripts)
    {
        const char *enc = strstr(path, g->encryptedScripts);
        size_t encLen = strlen(g->encryptedScripts);
        if (enc && (enc[encLen] == '/' || enc[encLen] == '\0'))
        {
            snprintf(buf, size, "%s/%s%s", gameDir, g->decryptedScripts, enc + encLen);
            return buf;
        }
    }

    rootLen = strlen(g->rootPath);
    if (!strncmp(path, g->rootPath, rootLen) && (path[rootLen] == '/' || path[rootLen] == '\0'))
    {
        const char *rest = path + rootLen;
        for (const RtPathAlias *alias = g->pathAliases; alias && alias->from; alias++)
        {
            size_t fromLen = strlen(alias->from);
            if (rest[0] == '/' && !strncmp(rest + 1, alias->from, fromLen) &&
                (rest[1 + fromLen] == '/' || rest[1 + fromLen] == '\0'))
            {
                snprintf(buf, size, "%s/%s%s", gameDir, alias->to, rest + 1 + fromLen);
                return buf;
            }
        }
        return join(buf, size, gameDir, rest);
    }
    for (const RtPathAlias *alias = g->rootAliases; alias && alias->from; alias++)
    {
        size_t fromLen = strlen(alias->from);
        if (!strncmp(path, alias->from, fromLen) && (path[fromLen] == '/' || path[fromLen] == '\0'))
        {
            snprintf(buf, size, "%s/%s%s", gameDir, alias->to, path + fromLen);
            return buf;
        }
    }
    if (!strncmp(path, "/mnt/usbflash", 13))
        return join(buf, size, usbDir, path + 13);
    // The game installs a USB automount rule at startup.
    if (!strncmp(path, "/etc/udev", 9))
        return join(buf, size, udevDir, path + 9);
    return path;
}

#define REDIRECT(path)                                                                                                \
    char redirectBuf[PATH_MAX];                                                                                       \
    path = rtRedirectPath(path, redirectBuf, sizeof(redirectBuf))

int access(const char *path, int mode)
{
    static int (*real)(const char *, int);
    if (!real)
        real = dlsym(RTLD_NEXT, "access");
    REDIRECT(path);
    return real(path, mode);
}

int rename(const char *from, const char *to)
{
    static int (*real)(const char *, const char *);
    char toBuf[PATH_MAX];
    if (!real)
        real = dlsym(RTLD_NEXT, "rename");
    REDIRECT(from);
    return real(from, rtRedirectPath(to, toBuf, sizeof(toBuf)));
}

int unlink(const char *path)
{
    static int (*real)(const char *);
    if (!real)
        real = dlsym(RTLD_NEXT, "unlink");
    REDIRECT(path);
    return real(path);
}

int chdir(const char *path)
{
    static int (*real)(const char *);
    if (!real)
        real = dlsym(RTLD_NEXT, "chdir");
    REDIRECT(path);
    return real(path);
}

// Old glibc stat entry points: only versioned compat symbols remain.
static void *nextCompat(const char *name)
{
    void *p = dlvsym(RTLD_NEXT, name, "GLIBC_2.0");
    return p ? p : dlsym(RTLD_NEXT, name);
}

int __xstat(int ver, const char *path, struct stat *st)
{
    static int (*real)(int, const char *, struct stat *);
    if (!real)
        real = nextCompat("__xstat");
    REDIRECT(path);
    return real(ver, path, st);
}

int __lxstat(int ver, const char *path, struct stat *st)
{
    static int (*real)(int, const char *, struct stat *);
    if (!real)
        real = nextCompat("__lxstat");
    REDIRECT(path);
    return real(ver, path, st);
}

int __lxstat64(int ver, const char *path, struct stat64 *st)
{
    static int (*real)(int, const char *, struct stat64 *);
    if (!real)
        real = nextCompat("__lxstat64");
    REDIRECT(path);
    return real(ver, path, st);
}

// ---------------------------------------------------------------------------
// GL: shader sources NVIDIA accepts but not Mesa.
// - g5 engine: the glow shaders write gl_FragData[] but also pull in a bloom
//   include that declares "out vec4 out_Bloom". GLSL forbids mixing
//   gl_FragData with user-defined outputs; NVIDIA tolerates it, Mesa refuses
//   to link. Those shaders never write out_Bloom, so it is demoted to a
//   plain global.
// - g3 engine: shaders without a #version directive (GLSL 1.10) use GLSL
//   1.20 features (matrix casts, transpose()): they are compiled as 1.20.
//   Some index gl_TexCoord[] with a variable, which needs it redeclared
//   with a size.

typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef char GLchar;

#define BLOOM_OUTPUT "out vec4 out_Bloom;"
#define GLSL_120 "#version 120\n"
#define TEXCOORD_SIZED "varying vec4 gl_TexCoord[gl_MaxTextureCoords];\n"

// gl_TexCoord[] indexed by something else than a number.
static int texCoordIndexed(const char *src)
{
    for (const char *p = strstr(src, "gl_TexCoord["); p; p = strstr(p + 1, "gl_TexCoord["))
    {
        const char *i = p + strlen("gl_TexCoord[");
        while (*i == ' ')
            i++;
        if (*i < '0' || *i > '9')
            return 1;
    }
    return 0;
}

void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{
    static void (*real)(GLuint, GLsizei, const GLchar *const *, const GLint *);
    size_t total = 0;
    char *src, *out;

    if (!real)
        real = dlsym(RTLD_NEXT, "glShaderSource");
    if (!isRawThrillsGame() || count <= 0)
        return real(shader, count, string, length);

    for (GLsizei i = 0; i < count; i++)
        total += length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
    if (!(src = malloc(sizeof(GLSL_120) + sizeof(TEXCOORD_SIZED) + total)))
        return real(shader, count, string, length);
    out = src;
    for (GLsizei i = 0; i < count; i++)
    {
        size_t n = length && length[i] >= 0 ? (size_t)length[i] : strlen(string[i]);
        memcpy(out, string[i], n);
        out += n;
    }
    *out = '\0';

    char *decl = strstr(src, BLOOM_OUTPUT);
    if (decl && strstr(src, "gl_FragData"))
        memset(decl, ' ', 4); // "out " -> a plain global
    if (!strstr(src, "#version"))
    {
        const char *prefix = texCoordIndexed(src) ? GLSL_120 TEXCOORD_SIZED : GLSL_120;
        size_t n = strlen(prefix);
        memmove(src + n, src, total + 1);
        memcpy(src, prefix, n);
    }
    const GLchar *one = src;
    real(shader, 1, &one, NULL);
    free(src);
}

// ---------------------------------------------------------------------------

int rtInit(void)
{
    const RtGame *g = rtCurrentGame();
    if (!g)
        return -1;

    // The launcher starts the game from its directory, which stands in for
    // the cabinet's root (e.g. /pm).
    if (!getcwd(gameDir, sizeof(gameDir)))
        strcpy(gameDir, ".");
    snprintf(usbDir, sizeof(usbDir), "%s/rawthrills/usbflash", gameDir);
    snprintf(udevDir, sizeof(udevDir), "%s/rawthrills/udev", gameDir);
    mkdir(join((char[PATH_MAX]){0}, PATH_MAX, gameDir, "/rawthrills"), 0755);
    mkdir(usbDir, 0755);
    mkdir(udevDir, 0755);
    mkdir(join((char[PATH_MAX]){0}, PATH_MAX, udevDir, "/rules.d"), 0755);
    if (g->workDir)
    {
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s/%s", gameDir, g->workDir);
        if (chdir(dir) != 0)
            log_warn("Raw Thrills: cannot enter %s", dir);
    }

    // sdl12-compat's GL scaling renders into its own FBO and relies on
    // catching glBindFramebuffer through SDL_GL_GetProcAddress; the game
    // links GL directly, so the swap would blit an empty FBO (black screen).
    setenv("SDL12COMPAT_OPENGL_SCALING", "0", 1);

    rtClearBss(g);
    rtFixImports(g);
    if (rtReplaceLib(g) != 0)
        return -1;
    rtInstallDongle(g);
    rtInstallIo(g);
    rtInstallInput(g);
    rtInstallJamma(g);
    rtInstallVideo(g);
    return 0;
}
