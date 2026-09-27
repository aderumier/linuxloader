// Launcher side of Raw Thrills support.
//
// In the dumps, a few undefined dynamic symbols (malloc, free, ...) carry the
// address of an envelope import stub as their value. ld.so then binds other
// libraries' references to those stubs, and libX11's constructor calls malloc
// through one before the loader can fix the envelope tables. The launcher
// therefore runs a copy of the ELF with those values cleared, kept in the
// user's cache directory; the game directory is left untouched.

#include <elf.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rtGame.h"
#include "../log/log.h"

#define RT_STACK_SIZE (64u << 20)

static int mkdirs(const char *path)
{
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++)
    {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(buf, 0755) != 0 && errno != EEXIST)
            return -1;
        *p = '/';
    }
    return mkdir(buf, 0755) != 0 && errno != EEXIST ? -1 : 0;
}

static int readFile(const char *path, unsigned char **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;

    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        !(*data = malloc(len)) || fread(*data, 1, len, f) != (size_t)len)
    {
        fclose(f);
        return -1;
    }
    fclose(f);
    *size = len;
    return 0;
}

// Map a virtual address to a file offset through the PT_LOAD segments.
static long vaddrToOffset(const unsigned char *elf, size_t size, Elf32_Addr vaddr)
{
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
    for (int i = 0; i < eh->e_phnum; i++)
    {
        const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_LOAD && vaddr >= ph->p_vaddr && vaddr < ph->p_vaddr + ph->p_filesz)
        {
            long off = ph->p_offset + (vaddr - ph->p_vaddr);
            return off < (long)size ? off : -1;
        }
    }
    return -1;
}

// Clear the value of every undefined dynamic symbol that has one.
static int patchUndefinedSymbols(unsigned char *elf, size_t size)
{
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf;
    Elf32_Addr symtab = 0, strtab = 0;
    long dynOff = -1, symOff, strOff;
    int patched = 0;

    if (size < sizeof(*eh) || memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32)
        return -1;
    for (int i = 0; i < eh->e_phnum; i++)
    {
        const Elf32_Phdr *ph = (const Elf32_Phdr *)(elf + eh->e_phoff + i * eh->e_phentsize);
        if (ph->p_type == PT_DYNAMIC)
            dynOff = ph->p_offset;
    }
    if (dynOff < 0)
        return -1;
    for (const Elf32_Dyn *d = (const Elf32_Dyn *)(elf + dynOff); d->d_tag != DT_NULL; d++)
    {
        if (d->d_tag == DT_SYMTAB)
            symtab = d->d_un.d_ptr;
        else if (d->d_tag == DT_STRTAB)
            strtab = d->d_un.d_ptr;
    }
    symOff = vaddrToOffset(elf, size, symtab);
    strOff = vaddrToOffset(elf, size, strtab);
    // The dynamic string table directly follows the symbol table.
    if (symOff < 0 || strOff <= symOff)
        return -1;

    for (Elf32_Sym *sym = (Elf32_Sym *)(elf + symOff) + 1; (unsigned char *)(sym + 1) <= elf + strOff; sym++)
    {
        if (sym->st_shndx == SHN_UNDEF && sym->st_value != 0)
        {
            sym->st_value = 0;
            patched++;
        }
    }
    return patched;
}

static int writeFile(const char *path, const unsigned char *data, size_t size)
{
    char tmp[PATH_MAX];
    FILE *f;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (!(f = fopen(tmp, "wb")))
        return -1;
    if (fwrite(data, 1, size, f) != size)
    {
        fclose(f);
        unlink(tmp);
        return -1;
    }
    fclose(f);
    chmod(tmp, 0755);
    return rename(tmp, path);
}

static void cacheDir(char *buf, size_t size, uint32_t fileCrc32)
{
    const char *xdg = getenv("XDG_CACHE_HOME");
    const char *home = getenv("HOME");

    if (xdg && xdg[0])
        snprintf(buf, size, "%s/linuxloader/rawthrills/%08x", xdg, fileCrc32);
    else
        snprintf(buf, size, "%s/.cache/linuxloader/rawthrills/%08x", home ? home : "/tmp", fileCrc32);
}

// command is "./<elf>" optionally followed by " -t" (test mode).
void rtPrepareCommand(char *command, size_t size, uint32_t fileCrc32)
{
    char elfPath[PATH_MAX], suffix[16] = "", dir[PATH_MAX], copy[PATH_MAX];
    struct stat src, dst;
    unsigned char *data = NULL;
    size_t len;
    const char *space;

    if (!rtGetGameByFileCrc(fileCrc32))
        return;

    snprintf(elfPath, sizeof(elfPath), "%s", command);
    if ((space = strrchr(command, ' ')) && strcmp(space, " -t") == 0)
    {
        elfPath[space - command] = '\0';
        snprintf(suffix, sizeof(suffix), "%s", space);
    }

    cacheDir(dir, sizeof(dir), fileCrc32);
    const char *base = strrchr(elfPath, '/');
    snprintf(copy, sizeof(copy), "%s/%s", dir, base ? base + 1 : elfPath);

    if (stat(elfPath, &src) != 0)
        return;
    if (stat(copy, &dst) != 0 || dst.st_size != src.st_size || dst.st_mtime < src.st_mtime)
    {
        int patched;
        if (mkdirs(dir) != 0 || readFile(elfPath, &data, &len) != 0 ||
            (patched = patchUndefinedSymbols(data, len)) < 0 || writeFile(copy, data, len) != 0)
        {
            log_error("Raw Thrills: cannot prepare a patched copy of %s in %s", elfPath, dir);
            free(data);
            return;
        }
        free(data);
        printf("Raw Thrills: prepared %s (%d symbols patched)\n", copy, patched);
    }

    snprintf(command, size, "%s%s", copy, suffix);

    // The dump's GOT still holds the cabinet's library addresses: have ld.so
    // resolve every relocatable import at startup instead of lazily.
    setenv("LD_BIND_NOW", "1", 1);

    // Some games use more than the usual 8 MB of main thread stack.
    struct rlimit stack;
    if (getrlimit(RLIMIT_STACK, &stack) == 0 && stack.rlim_cur != RLIM_INFINITY && stack.rlim_cur < RT_STACK_SIZE)
    {
        stack.rlim_cur = stack.rlim_max == RLIM_INFINITY || stack.rlim_max >= RT_STACK_SIZE ? RT_STACK_SIZE : stack.rlim_max;
        setrlimit(RLIMIT_STACK, &stack);
    }
}
