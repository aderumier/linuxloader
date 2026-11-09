// Symbols the Raw Thrills games import that the other bridges do not cover:
// the glibc internals and FORTIFY wrappers their 2015 build was made with,
// the udev/ogg/theora libraries the cabinet ran (stubs), and the CRT and
// win32 functions a 32-bit game looks up by its Linux names.

#include <string.h>
#if defined(_WIN32) || defined(__MINGW32__)

#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unwind.h>
#include <wchar.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <cerrno>

// <sys/mman.h>'s protections (the game's glibc values), the toolchain has no
// mman header on Windows.
#ifndef PROT_NONE
#define PROT_NONE 0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#endif

#include "libcBridge.hpp"
#include "symbolResolver.hpp"
#include "rtBridge.hpp"
#include "memoryManager.hpp"
#include "wide32.hpp"

#include "../log/log.h"
#include "../rawthrills/rawthrills.h"
#include "../redirections/libcShared.h"

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

// The loader's emulated semaphore (pthreadSemaphore.cpp), as the game's sem_t.
extern "C" int emuSemTrywait(void *sem);

namespace RtBridge
{
    int bridgeVasprintf(char **ptr, const char *fmt, va_list args);

    // ------------------------------------------------------------------
    // C++ exception support: the personality of the Itanium ABI, from the
    // exe's statically linked libgcc (unwind-dw2). Referencing it pulls the
    // object into the exe; the _Unwind_* entry points come from the
    // libgcc_s_dw2-1.dll the resolver loads (see symbolResolver.cpp).

    // ------------------------------------------------------------------
    // The games' popen and system: the cabinet's paths in their commands,
    // as for the game's own file calls (see rawthrills.c). The others'
    // (the Roxor games') keep the libc bridge's.

    FILE *bridgeRtPopen(const char *command, const char *type)
    {
        if (!isRawThrillsGame())
            return LibcBridge::bridgePopen(command, type);
        char buf[4096];
        const char *cmd = rtRedirectCommand(command, buf, sizeof(buf));
        log_info("Intercepted popen: %s %s", cmd, type);
        return popen(cmd, type);
    }

    int bridgeRtSystem(const char *command)
    {
        if (!isRawThrillsGame())
            return LibcBridge::bridgeSystem(command);
        char buf[4096];
        const char *cmd = rtRedirectCommand(command, buf, sizeof(buf));
        log_info("Intercepted system: %s", cmd);
        return system(cmd);
    }

    // ------------------------------------------------------------------
    // FORTIFY wrappers: the plain functions, the flag ignored.

    int bridgeAsprintfChk(char **ptr, int flag, const char *fmt, ...)
    {
        (void)flag;
        va_list args;
        va_start(args, fmt);
        int r = bridgeVasprintf(ptr, fmt, args);
        va_end(args);
        return r;
    }

    int bridgePrintfChk(int flag, const char *fmt, ...)
    {
        (void)flag;
        va_list args;
        va_start(args, fmt);
        int n = vprintf(fmt, args);
        va_end(args);
        return n;
    }

    int bridgeSprintfChk(char *s, int flag, size_t destlen, const char *fmt, ...)
    {
        (void)flag;
        (void)destlen;
        va_list args;
        va_start(args, fmt);
        int n = vsprintf(s, fmt, args);
        va_end(args);
        return n;
    }

    int bridgeFwprintfChk(FILE *f, int flag, const uint32_t *fmt, ...)
    {
        (void)flag;
        va_list args;
        va_start(args, fmt);
        int n = wide32Vfprintf(f, fmt, args);
        va_end(args);
        return n;
    }

    int bridgeSwprintfChk(uint32_t *s, size_t n, int flag, size_t destlen, const uint32_t *fmt, ...)
    {
        (void)flag;
        (void)destlen;
        va_list args;
        va_start(args, fmt);
        int r = wide32Vsnprintf(s, n, fmt, args);
        va_end(args);
        return r;
    }

    int bridgeVsnprintfChk(char *s, size_t n, int flag, size_t destlen, const char *fmt, va_list args)
    {
        (void)flag;
        (void)destlen;
        return _vsnprintf(s, n, fmt, args);
    }

    int bridgeVsprintfChk(char *s, int flag, size_t destlen, const char *fmt, va_list args)
    {
        (void)flag;
        (void)destlen;
        return _vsnprintf(s, 0, fmt, args);
    }

    char *bridgeStpcpy(char *dst, const char *src)
    {
        char *p = dst;
        while ((*p++ = *src++))
            ;
        return p - 1;
    }

    // glibc's _chk: (dest, src, destlen); the size is the destination's.
    char *bridgeStpcpyChk(char *dst, const char *src, size_t destlen)
    {
        (void)destlen;
        return bridgeStpcpy(dst, src);
    }

    char *bridgeStrcpyChk(char *dst, const char *src, size_t destlen)
    {
        (void)destlen;
        return strcpy(dst, src);
    }

    char *bridgeStrcatChk(char *dst, const char *src, size_t destlen)
    {
        (void)destlen;
        return strcat(dst, src);
    }

    uint32_t *bridgeWcscpyChk(uint32_t *dst, const uint32_t *src, size_t destlen)
    {
        (void)destlen;
        uint32_t *d = dst;
        while ((*d++ = *src++))
            ;
        return dst;
    }

    char *bridgeStrncpyChk(char *dst, size_t n, const char *src, size_t dstsz)
    {
        (void)dstsz;
        return strncpy(dst, src, n);
    }

    char *bridgeStrncatChk(char *dst, size_t dstmax, const char *src, size_t len)
    {
        (void)dstmax;
        return strncat(dst, src, len);
    }

    // glibc's __fread_chk: (ptr, destlen, size, n, stream).
    size_t bridgeFreadChk(void *ptr, size_t destlen, size_t size, size_t n, FILE *f)
    {
        (void)destlen;
        return fread(ptr, size, n, f);
    }

    int bridgeFdeltaChk(int fd)
    {
        // glibc's __fdelt_chk: how many __fd_mask words select() copies.
        // Inert on Windows (sharedSelect's select returns 0), kept for the import.
        if (fd < 0)
            return 0;
        if (fd >= FD_SETSIZE)
            return FD_SETSIZE;
        return fd + 1;
    }

    // ------------------------------------------------------------------
    // glibc internals, as the MinGW CRT's or plain stand-ins.

    int bridgeFinitef(float x)
    {
        return isfinite(x);
    }

    int bridgeIsoFscanf(FILE *f, const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int n = vfscanf(f, fmt, args);
        va_end(args);
        return n;
    }

    int bridgeIsoSscanf(const char *s, const char *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int n = vsscanf(s, fmt, args);
        va_end(args);
        return n;
    }

    int bridgeOpen2(const char *path, int flags, ...)
    {
        mode_t mode = 0;
        if (flags & O_CREAT)
        {
            va_list args;
            va_start(args, flags);
            mode = (mode_t)va_arg(args, int);
            va_end(args);
        }
        return open(path, flags, mode);
    }

    char *bridgeStrtokR(char *s, const char *delims, char **save)
    {
        char *start, *end;

        if (!s)
            s = save ? *save : NULL;
        if (!s)
            return NULL;
        start = s;
        while (*start && strpbrk(start, delims) == start)
            start++;
        if (!*start)
        {
            if (save)
                *save = start;
            return NULL;
        }
        end = start;
        while (*end && strpbrk(end, delims) != end)
            end++;
        if (*end)
        {
            *end = '\0';
            end++;
        }
        if (save)
            *save = end;
        return start;
    }

    int *bridgeHErrnoLocation(void)
    {
        static int h = 0;
        return &h;
    }

    int bridgeSigRtMin(void)
    {
        return 32;
    }

    void (*bridgeSysvSignal(int signum, void (*handler)(int)))(int)
    {
        return (void (*)(int))signal(signum, (void (*)(int))handler);
    }

    // ------------------------------------------------------------------
    // The CRT, looked up by the names the game was built with.

    char *bridgeAsctimeR(const struct tm *t, char *buf)
    {
        const char *s = asctime(t);
        if (buf && s)
            strcpy(buf, s);
        return buf;
    }

    long bridgeAtol(const char *s)
    {
        return atol(s);
    }

    long long bridgeAtoll(const char *s)
    {
        return atoll(s);
    }

    int bridgeBacktrace(void **buffer, int max)
    {
        (void)buffer;
        (void)max;
        return 0;
    }

    char **bridgeBacktraceSymbols(void *const *buffer, int n)
    {
        (void)buffer;
        static const char unknown[] = "?";
        char **r = (char **)calloc((size_t)(n > 0 ? n : 1), sizeof(char *));
        for (int i = 0; i < n; i++)
            r[i] = (char *)unknown;
        return r;
    }

    float bridgeCeilf(float x)
    {
        return (float)ceil(x);
    }

    float bridgeFloorf(float x)
    {
        return (float)floor(x);
    }

    clock_t bridgeClock(void)
    {
        return clock();
    }

    // glibc's sysconf, for the names the games ask (Linux _SC_ values).
    long bridgeSysconf(int name)
    {
        SYSTEM_INFO si;
        MEMORYSTATUSEX ms;
        GetSystemInfo(&si);
        ms.dwLength = sizeof(ms);
        GlobalMemoryStatusEx(&ms);
        switch (name)
        {
        case 2: // _SC_CLK_TCK
            return 100;
        case 4: // _SC_OPEN_MAX
            return 1024;
        case 30: // _SC_PAGESIZE
            return 4096;
        case 83: // _SC_NPROCESSORS_CONF
        case 84: // _SC_NPROCESSORS_ONLN
            return (long)si.dwNumberOfProcessors;
        case 85: // _SC_PHYS_PAGES
            return (long)(ms.ullTotalPhys / 4096 > 0x7fffffff ? 0x7fffffff : ms.ullTotalPhys / 4096);
        case 86: // _SC_AVPHYS_PAGES
            return (long)(ms.ullAvailPhys / 4096 > 0x7fffffff ? 0x7fffffff : ms.ullAvailPhys / 4096);
        }
        log_warn("sysconf(%d): not answered", name);
        errno = EINVAL;
        return -1;
    }

    int bridgeClockGetres(clockid_t clockid, struct linux_timespec *res)
    {
        (void)clockid;
        if (res)
        {
            res->tv_sec = 0;
            res->tv_nsec = 10000000;
        }
        return 0;
    }

    // glibc's Dl_info, as of the 2.19 the games were built with.
    struct RtDlInfo
    {
        const char *dli_fname;
        void *dli_fbase;
        const char *dli_sname;
        void *dli_saddr;
    };

    int bridgeDladdr(void *addr, RtDlInfo *info)
    {
        (void)addr;
        if (info)
        {
            info->dli_fname = "linuxloader";
            info->dli_fbase = NULL;
            info->dli_sname = NULL;
            info->dli_saddr = NULL;
        }
        return 0;
    }

    void *bridgeDlvsym(void *handle, const char *name, const char *version)
    {
        (void)version;
        return sharedDlsym(handle, name);
    }

    int bridgeEndmntent(void *stream)
    {
        (void)stream;
        return 0;
    }

    int bridgeEndutent(void)
    {
        return 0;
    }

    void bridgeFreeAddrInfo(struct addrinfo *ai)
    {
        (void)ai; // nothing was allocated (see bridgeGetAddrInfo)
    }

    int bridgeFgetpos(FILE *stream, fpos_t *pos)
    {
        return fgetpos(stream, pos);
    }

    int bridgeFsetpos(FILE *stream, const fpos_t *pos)
    {
        return fsetpos(stream, pos);
    }

    int bridgeFwprintf(FILE *stream, const uint32_t *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int n = wide32Vfprintf(stream, fmt, args);
        va_end(args);
        return n;
    }

    // The win32 getaddrinfo's last argument is a pointer-to-pointer, the
    // glibc one's is the result itself: forwarding would need a copy and a
    // matching free, and the games' network path is not ported anyway.
    // Fail cleanly so they take their offline branch.
    int bridgeGetAddrInfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **result)
    {
        (void)node;
        (void)service;
        (void)hints;
        // The caller passes a 4-byte pointer slot, not an addrinfo object:
        // clear only the pointer, never the struct behind it.
        if (result)
            *result = NULL;
        return 1; // an EAI_-like failure
    }

    int bridgeGetGid(void)
    {
        return 0;
    }

    int bridgeGetloginR(char *buf, size_t size)
    {
        if (size)
        {
            strncpy(buf, "loader", size - 1);
            buf[size - 1] = '\0';
        }
        return 0;
    }

    void *bridgeGetmntent(void)
    {
        return NULL;
    }

    int bridgeGetPpid(void)
    {
        return (int)GetCurrentProcessId();
    }

    struct RtPasswd
    {
        char *pw_name;
    };

    int bridgeGetPwuidR(unsigned int uid, RtPasswd *pwd, char *buf, size_t size, RtPasswd **result)
    {
        (void)buf;
        (void)size;
        (void)uid;
        static RtPasswd pw;
        static char name[] = "loader";
        pw.pw_name = name;
        if (pwd)
            *pwd = pw;
        if (result)
            *result = &pw;
        return 0;
    }

    int bridgeGetUtentR(void *ubuf, void *ubufec)
    {
        (void)ubuf;
        (void)ubufec;
        return -1;
    }

    const char *bridgeInetNtop(int af, const void *src, char *dst, int size)
    {
        return InetNtopA(af, (const IN_ADDR *)src, dst, (DWORD)size);
    }

    void *bridgeSetmntent(const char *file, const char *type)
    {
        (void)file;
        (void)type;
        return NULL;
    }

    void *bridgeSetutent(void)
    {
        return NULL;
    }

    // glibc's iconv, the cabinet's text decoder: there is no multibyte
    // conversion in the game's own data (its scripts are UTF-8), so the
    // conversions the engine asks for are the identity's.

    void *bridgeIconvOpen(const char *tocode, const char *fromcode)
    {
        (void)tocode;
        (void)fromcode;
        return (void *)1; // a handle meaning "the identity"
    }

    int bridgeIconvClose(void *cd)
    {
        (void)cd;
        return 0;
    }

    size_t bridgeIconv(void *cd, char **inbuf, size_t *inbytesleft, char **outbuf, size_t *outbytesleft)
    {
        (void)cd;
        if (!inbuf || !*inbuf || !outbuf || !*outbuf)
            return (size_t)-1;
        size_t n = *inbytesleft > *outbytesleft ? *outbytesleft : *inbytesleft;
        memcpy(*outbuf, *inbuf, n);
        *inbuf += n;
        *inbytesleft -= n;
        *outbuf += n;
        *outbytesleft -= n;
        return *inbytesleft ? (size_t)-1 : 0;
    }

    double bridgeLdexp(double x, int exp)
    {
        return ldexp(x, exp);
    }

    long bridgeLrint(double x)
    {
        return (long)lrint(x);
    }

    // glibc's sincos takes double outputs (the float one is sincosf, below).
    void bridgeSincos(double x, double *sin, double *cos)
    {
        double s, c;
        sincos(x, &s, &c);
        if (sin)
            *sin = s;
        if (cos)
            *cos = c;
    }

    void bridgeSincosf(float x, float *sin, float *cos)
    {
        double s, c;
        sincos(x, &s, &c);
        if (sin)
            *sin = (float)s;
        if (cos)
            *cos = (float)c;
    }

    unsigned long long bridgeStrtoull(const char *s, char **end, int base)
    {
        return strtoull(s, end, base);
    }

    int bridgeSwprintf(uint32_t *s, size_t n, const uint32_t *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int r = wide32Vsnprintf(s, n, fmt, args);
        va_end(args);
        return r;
    }

    int bridgeSwscanf(const uint32_t *s, const uint32_t *fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        int r = wide32Vsscanf(s, fmt, args);
        va_end(args);
        return r;
    }

    long bridgeSyscall(long number, ...)
    {
        (void)number;
        return -1;
    }

    // glibc's struct sysinfo, i386 layout (72 bytes): the game reads its
    // memory out of it, so the offsets and size must match exactly.
    struct RtSysInfo
    {
        long uptime;
        unsigned long loads[3];
        unsigned long totalram, freeram, sharedram, bufferram, totalswap, freeswap;
        unsigned short procs;
        unsigned short padding;
        unsigned long totalhigh, freehigh;
        char _f[20];
    };
    static_assert(sizeof(RtSysInfo) == 72, "RtSysInfo must match the i386 guest struct");
    static_assert(offsetof(RtSysInfo, totalhigh) == 44 && offsetof(RtSysInfo, freehigh) == 48,
                  "RtSysInfo field offsets");

    int bridgeSysinfo(RtSysInfo *info)
    {
        if (!info)
            return -1;
        memset(info, 0, sizeof(*info));
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        if (GlobalMemoryStatusEx(&ms))
        {
            info->totalram = (unsigned long)(ms.ullTotalPhys / 1024);
            info->freeram = (unsigned long)(ms.ullAvailPhys / 1024);
        }
        info->uptime = (long)(GetTickCount() / 1000);
        info->procs = 1;
        return 0;
    }

    int bridgeTimerfdCreate(int flags)
    {
        (void)flags;
        return -1;
    }

    int bridgeTimerfdSettime(int fd, int flags, const void *new_setting, void *old_setting)
    {
        (void)fd;
        (void)flags;
        (void)new_setting;
        (void)old_setting;
        return 0;
    }

    int bridgeTtynameR(int fd, char *buffer, size_t size)
    {
        (void)fd;
        if (size)
            buffer[0] = '\0';
        return -1;
    }

    int bridgeVasprintf(char **ptr, const char *fmt, va_list args)
    {
        int n = vsnprintf(NULL, 0, fmt, args);
        if (n < 0)
            return -1;
        char *s = (char *)malloc((size_t)n + 1);
        if (!s)
            return -1;
        vsnprintf(s, (size_t)n + 1, fmt, args);
        *ptr = s;
        return n;
    }

    int bridgeWcscasecmp(const uint32_t *a, const uint32_t *b)
    {
        return wide32Casecmp(a, b);
    }

    uint32_t *bridgeWcscat(uint32_t *dst, const uint32_t *src)
    {
        return wide32Cat(dst, src);
    }

    uint32_t *bridgeWcsdup(const uint32_t *s)
    {
        size_t size = sizeof(uint32_t);
        for (const uint32_t *p = s; *p; p++)
            size += sizeof(uint32_t);
        // The game frees it: its allocator's.
        uint32_t *copy = (uint32_t *)MemoryManager::customMalloc(size);
        if (copy)
            memcpy(copy, s, size);
        return copy;
    }

    uint32_t *bridgeWcsncat(uint32_t *dst, const uint32_t *src, size_t n)
    {
        return wide32Ncat(dst, src, n);
    }

    uint32_t *bridgeWcstok(uint32_t *s, const uint32_t *delims, uint32_t **save)
    {
        return wide32Tok(s, delims, save);
    }

    long long bridgeWcstoll(const uint32_t *s, uint32_t **end, int base)
    {
        return wide32Toll(s, end, base);
    }

    unsigned long long bridgeWcstoull(const uint32_t *s, uint32_t **end, int base)
    {
        return wide32Toull(s, end, base);
    }

    // ------------------------------------------------------------------
    // Signals and threads: the libc-side signal set is glibc's 16-byte one,
    // and Windows has no signal mask to keep, so those are no-ops; the
    // pthread-side ones go to winpthread's, which read and write only their
    // own 4-byte word of the set.

    int bridgeSigdelset(void *set, int signum)
    {
        (void)set;
        (void)signum;
        return 0;
    }

    int bridgeSigprocmask(int how, void *set, void *oldset)
    {
        (void)how;
        (void)set;
        (void)oldset;
        return 0;
    }

    int bridgePthreadKill(pthread_t thread, int sig)
    {
        return pthread_kill(thread, sig);
    }

    int bridgePthreadSigmask(int how, const void *set, void *oldset)
    {
        return pthread_sigmask(how, (const _sigset_t *)set, (_sigset_t *)oldset);
    }

    int bridgePthreadAttrSetinheritsched(pthread_attr_t *attr, int inheritsched)
    {
        return pthread_attr_setinheritsched(attr, inheritsched);
    }

    // ------------------------------------------------------------------
    // udev, the cabinet's hotplug: none, the engine's poll finds nothing.

    void *bridgeUdevNew(void)
    {
        return NULL;
    }

    int bridgeUdevUnref(void *udev)
    {
        (void)udev;
        return 0;
    }

    void *bridgeUdevEnumerateNew(void *udev)
    {
        (void)udev;
        return NULL;
    }

    int bridgeUdevEnumerateAddMatchSubsystem(void *en, const char *subsystem)
    {
        (void)en;
        (void)subsystem;
        return 0;
    }

    int bridgeUdevEnumerateScanDevices(void *en)
    {
        (void)en;
        return 0;
    }

    void *bridgeUdevEnumerateGetListEntry(void *en)
    {
        (void)en;
        return NULL;
    }

    int bridgeUdevEnumerateUnref(void *en)
    {
        (void)en;
        return 0;
    }

    const char *bridgeUdevListEntryGetName(void *entry)
    {
        (void)entry;
        return NULL;
    }

    void *bridgeUdevListEntryGetNext(void *list)
    {
        (void)list;
        return NULL;
    }

    void *bridgeUdevDeviceNewFromSyspath(void *udev, const char *syspath)
    {
        (void)udev;
        (void)syspath;
        return NULL;
    }

    void *bridgeUdevDeviceNewFromDevnum(void *udev, char type, unsigned long major, unsigned long minor)
    {
        (void)udev;
        (void)type;
        (void)major;
        (void)minor;
        return NULL;
    }

    void *bridgeUdevDeviceGetParentWithSubsystemDevtype(void *device, const char *subsystem, const char *devtype)
    {
        (void)device;
        (void)subsystem;
        (void)devtype;
        return NULL;
    }

    const char *bridgeUdevDeviceGetAction(void *device)
    {
        (void)device;
        return NULL;
    }

    const char *bridgeUdevDeviceGetDevnode(void *device)
    {
        (void)device;
        return NULL;
    }

    const char *bridgeUdevDeviceGetSysname(void *device)
    {
        (void)device;
        return NULL;
    }

    const char *bridgeUdevDeviceGetSysattrValue(void *device, const char *sysattr)
    {
        (void)device;
        (void)sysattr;
        return NULL;
    }

    int bridgeUdevDeviceUnref(void *device)
    {
        (void)device;
        return 0;
    }

    void *bridgeUdevMonitorNewFromNetlink(void *udev, int type)
    {
        (void)udev;
        (void)type;
        return NULL;
    }

    int bridgeUdevMonitorEnableReceiving(void *monitor)
    {
        (void)monitor;
        return -1;
    }

    int bridgeUdevMonitorFilterAddMatchSubsystemDevtype(void *monitor, const char *subsystem, const char *devtype)
    {
        (void)monitor;
        (void)subsystem;
        (void)devtype;
        return 0;
    }

    int bridgeUdevMonitorGetFd(void *monitor)
    {
        (void)monitor;
        return -1;
    }

    void *bridgeUdevMonitorReceiveDevice(void *monitor, void **device)
    {
        (void)monitor;
        if (device)
            *device = NULL;
        return NULL;
    }

    int bridgeUdevMonitorUnref(void *monitor)
    {
        (void)monitor;
        return 0;
    }

    // ------------------------------------------------------------------
    // ogg/theora, the cabinet's video: none, the engine's player fails its
    // inits and takes its no-video path. The inits zero the state, a dump's
    // .bss being what it is.

    int bridgeOggSyncInit(void *state)
    {
        if (state)
            memset(state, 0, 256);
        return -1;
    }

    void *bridgeOggSyncBuffer(void *state)
    {
        (void)state;
        return NULL;
    }

    long bridgeOggSyncWrote(void *state, long size)
    {
        (void)state;
        (void)size;
        return 0;
    }

    int bridgeOggSyncClear(void *state)
    {
        (void)state;
        return 0;
    }

    int bridgeOggSyncPageout(void *state, void *page)
    {
        (void)state;
        (void)page;
        return -1;
    }

    int bridgeOggPageBos(void *page)
    {
        (void)page;
        return -1;
    }

    long long bridgeOggPageSerialno(void *page)
    {
        (void)page;
        return 0;
    }

    int bridgeOggStreamInit(void *state)
    {
        if (state)
            memset(state, 0, 512);
        return -1;
    }

    int bridgeOggStreamPagein(void *state, void *page)
    {
        (void)state;
        (void)page;
        return -1;
    }

    int bridgeOggStreamFlush(void *state, void *page)
    {
        (void)state;
        (void)page;
        return -1;
    }

    int bridgeOggStreamPageout(void *state, void *page)
    {
        (void)state;
        (void)page;
        return -1;
    }

    int bridgeOggStreamPacketin(void *state, void *packet)
    {
        (void)state;
        (void)packet;
        return -1;
    }

    int bridgeOggStreamPacketout(void *state, void *packet, long *eos)
    {
        (void)state;
        (void)packet;
        if (eos)
            *eos = 0;
        return -1;
    }

    int bridgeOggStreamClear(void *state)
    {
        (void)state;
        return 0;
    }

    int bridgeTheoraInfoInit(void *info)
    {
        if (info)
            memset(info, 0, 64);
        return 0;
    }

    void bridgeTheoraInfoClear(void *info)
    {
        if (info)
            memset(info, 0, 64);
    }

    int bridgeTheoraDecodeInit(void *dec)
    {
        if (dec)
            memset(dec, 0, 512);
        return -1;
    }

    int bridgeTheoraDecodeHeader(void *dec, void *info, void *comment)
    {
        (void)dec;
        (void)info;
        (void)comment;
        return -1;
    }

    int bridgeTheoraDecodePacketin(void *dec, void *packet)
    {
        (void)dec;
        (void)packet;
        return -1;
    }

    int bridgeTheoraDecodeYUVout(void *dec, void *img)
    {
        (void)dec;
        (void)img;
        return -1;
    }

    long long bridgeTheoraGranuleFrame(void *granule)
    {
        (void)granule;
        return 0;
    }

    int bridgeTheoraGranuleTime(void *granule)
    {
        (void)granule;
        return 0;
    }

    void bridgeTheoraCommentInit(void *comment)
    {
        if (comment)
            memset(comment, 0, 64);
    }

    void bridgeTheoraCommentClear(void *comment)
    {
        if (comment)
            memset(comment, 0, 64);
    }

    const char *bridgeTheoraCommentQuery(void *comment, const char *field, unsigned long *num)
    {
        (void)comment;
        (void)field;
        if (num)
            *num = 0;
        return NULL;
    }

    int bridgeTheoraCommentAdd(void *comment, const char *field, const char *values)
    {
        (void)comment;
        (void)field;
        (void)values;
        return -1;
    }

    int bridgeTheoraEncodeInit(void *enc)
    {
        if (enc)
            memset(enc, 0, 512);
        return -1;
    }

    int bridgeTheoraEncodeInit2(void *enc)
    {
        if (enc)
            memset(enc, 0, 512);
        return -1;
    }

    int bridgeTheoraEncodeHeader(void *enc, void *info, void *comment)
    {
        (void)enc;
        (void)info;
        (void)comment;
        return -1;
    }

    int bridgeTheoraEncodePacketout(void *enc, void *packet, int *end)
    {
        (void)enc;
        (void)packet;
        if (end)
            *end = 0;
        return -1;
    }

    void bridgeTheoraEncodeYUVin(void *enc, unsigned char Y, unsigned char U, unsigned char V)
    {
        (void)enc;
        (void)Y;
        (void)U;
        (void)V;
    }

    int bridgeTheoraEncodeComment(void *enc, void *comment)
    {
        (void)enc;
        (void)comment;
        return -1;
    }

    int bridgeTheoraEncodeTables(void *enc, void *buf, long size)
    {
        (void)enc;
        (void)buf;
        (void)size;
        return -1;
    }

    // Not in the public libtheora API (the engine's build has its own):
    // the decoder's clear, a no-op here.
    void bridgeTheoraClear(void *dec)
    {
        (void)dec;
    }

    // ------------------------------------------------------------------
    // NxCooking: the library's own entry, its functions come from the
    // cabinet's libNxCooking.so the loader loads as a native ELF.

    int bridgeNxGetCookingLib(void)
    {
        return 0;
    }

    // ------------------------------------------------------------------
    // Misc libc and glibc internals the games import: on Windows, or plain
    // stand-ins (the cabinet's on the cabinet; here enough not to crash).

    int bridgeMprotect(void *addr, size_t len, int prot)
    {
        DWORD newProt;
        if (prot & PROT_EXEC)
            newProt = (prot & PROT_WRITE) ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
        else if (prot & PROT_WRITE)
            newProt = PAGE_READWRITE;
        else if (prot & PROT_READ)
            newProt = PAGE_READONLY;
        else
            newProt = PAGE_NOACCESS;
        DWORD oldProt = 0;
        return VirtualProtect(addr, len, newProt, &oldProt) ? 0 : -1;
    }

    // The i386 guest struct rlimit: two 4-byte rlim_t (8 bytes total).
    struct RtRlimit
    {
        unsigned long rlim_cur;
        unsigned long rlim_max;
    };
    static_assert(sizeof(RtRlimit) == 8, "RtRlimit must match the i386 guest struct");

    int bridgeGetrlimit(int resource, void *rlim)
    {
        (void)resource;
        // RLIM_INFINITY for both.
        if (rlim)
        {
            RtRlimit *r = (RtRlimit *)rlim;
            r->rlim_cur = 0xFFFFFFFFUL;
            r->rlim_max = 0xFFFFFFFFUL;
        }
        return 0;
    }

    int bridgePtrace(int request, int pid, void *addr, void *data)
    {
        (void)request;
        (void)pid;
        (void)addr;
        (void)data;
        return 0;
    }

    int bridgeSetpriority(int which, int who, int prio)
    {
        (void)which;
        (void)who;
        (void)prio;
        return 0;
    }

    int bridgePrctl(int option, unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5)
    {
        (void)option;
        (void)a2;
        (void)a3;
        (void)a4;
        (void)a5;
        return 0;
    }

    time_t bridgeTimegm(struct tm *t)
    {
        // Broken-down UTC time to a time_t (the POSIX days-from-civil).
        long long y = t->tm_year + 1900;
        int m = t->tm_mon + 1;
        if (m <= 2)
            y -= 1;
        long long era = (y >= 0 ? y : y - 399) / 400;
        int yoe = (int)(y - era * 400);
        int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t->tm_mday - 1;
        int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        long long days = era * 146097 + doe - 719468;
        return (time_t)(days * 86400LL + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec);
    }

    double bridgeScalbn(double x, int exp)
    {
        return scalbn(x, exp);
    }

    float bridgeScalbnf(float x, int exp)
    {
        return scalbnf(x, exp);
    }

    long long bridgeStrtoll(const char *nptr, char **endptr, int base)
    {
        return strtoll(nptr, endptr, base);
    }

    int bridgeIsoVsscanf(const char *s, const char *fmt, va_list args)
    {
        return vsscanf(s, fmt, args);
    }

    char *bridgeGetlogin(void)
    {
        static const char *login = "root";
        return (char *)login;
    }

    char *bridgeTtyname(int fd)
    {
        (void)fd;
        static const char *tty = "/dev/tty";
        return (char *)tty;
    }

    void *bridgeGetutxent(void)
    {
        return NULL;
    }

    int bridgeSetutxent(void)
    {
        return 0;
    }

    void bridgeEndutxent(void)
    {
    }

    long bridgeGetpgid(int pid)
    {
        (void)pid;
        return (long)_getpid();
    }

    int bridgeSetpgid(int pid, int pgid)
    {
        (void)pid;
        (void)pgid;
        return 0;
    }

    unsigned int bridgeGnuDevMajor(unsigned long dev)
    {
        return (unsigned int)(((dev >> 8) & 0xff) | ((dev >> 16) & 0xffffff00));
    }

    unsigned int bridgeGnuDevMinor(unsigned long dev)
    {
        return (unsigned int)((dev & 0xff) | ((dev >> 8) & 0xffffff00));
    }

    unsigned long bridgeGnuMakedev(unsigned int major, unsigned int minor)
    {
        unsigned long dev = 0;
        dev |= (minor & 0xff);
        dev |= ((unsigned long)(major & 0xffff) << 8);
        dev |= ((unsigned long)(minor >> 8) << 12);
        return dev;
    }

    int bridgeSigqueue(int pid, int sig, void *value)
    {
        (void)pid;
        (void)sig;
        (void)value;
        return 0;
    }

    int bridgeGetifaddrs(void **ifap)
    {
        if (ifap)
            *ifap = NULL;
        return -1;
    }

    void bridgeFreeifaddrs(void *ifa)
    {
        (void)ifa;
    }

    int bridgeGetnameinfo(void *sa, int salen, char *host, size_t hostlen, char *serv, size_t servlen, int flags)
    {
        (void)sa;
        (void)salen;
        (void)flags;
        if (host && hostlen)
            host[0] = 0;
        if (serv && servlen)
            serv[0] = 0;
        return -1;
    }

    int bridgeSemTimedwait(void *sem, void *ts)
    {
        (void)ts;
        // Never block past the (unused) timeout: a ready semaphore is
        // consumed, an empty one times out, as the game's path expects.
        if (emuSemTrywait(sem) == 0)
            return 0;
        errno = ETIMEDOUT;
        return -1;
    }

    int bridgeStatvfs(const char *path, void *buf)
    {
        (void)path;
        if (buf)
            memset(buf, 0, 64);
        return 0;
    }

    int bridgeStatfs(const char *path, void *buf)
    {
        (void)path;
        if (buf)
            memset(buf, 0, 64);
        return 0;
    }

    int bridgeDlIteratePhdr(void *callback, void *data)
    {
        (void)callback;
        (void)data;
        return 0;
    }

    // ------------------------------------------------------------------
    // The remaining libc/libm names the 32-bit games import that MinGW
    // does not export under their Linux names.

    char *bridgeCtime(const time_t *t)
    {
        return ctime(t);
    }

    int bridgePutenv(const char *s)
    {
        return _putenv(s);
    }

    double bridgeTrunc(double x)
    {
        return trunc(x);
    }

    size_t bridgeStrnlen(const char *s, size_t max)
    {
        return strnlen(s, max);
    }

    int bridgeIsWalpha(wint_t wc)
    {
        return iswalpha(wc);
    }

    unsigned long bridgeWcstoul(const uint32_t *s, uint32_t **end_ptr, int base)
    {
        return (unsigned long)wide32Toull(s, end_ptr, base);
    }

    void bridgeTzset(void)
    {
        _tzset();
    }

    // BSD bcopy: (source, destination, count), unlike memcpy.
    void bridgeBcopy(const void *src, void *dst, size_t n)
    {
        memcpy(dst, src, n);
    }

    void *bridgeGetnetbyname(const char *name)
    {
        (void)name;
        return nullptr;
    }

    int bridgeFpclassifyf(float x)
    {
        return fpclassify(x);
    }

    int bridgeIsoc99Swscanf(const uint32_t *s, const uint32_t *format, ...)
    {
        va_list ap;
        va_start(ap, format);
        int result = wide32Vsscanf(s, format, ap);
        va_end(ap);
        return result;
    }

    // FORTIFY wrappers: same as the plain function, the size argument is a
    // canary glibc checks against; the game built fine without it.
    int bridgeVfprintfChk(FILE *stream, int flag, const char *format, va_list arg)
    {
        (void)flag;
        return vfprintf(stream, format, arg);
    }

    // glibc's __wcscat_chk: (dest, src, destlen).
    uint32_t *bridgeWcscatChk(uint32_t *dest, const uint32_t *src, size_t destlen)
    {
        (void)destlen;
        return wide32Cat(dest, src);
    }

    uint32_t *bridgeWcsncatChk(uint32_t *dest, const uint32_t *src, size_t n, size_t destlen)
    {
        (void)destlen;
        return wide32Ncat(dest, src, n);
    }

    void *bridgeMemsetChk(void *dest, int c, size_t n, size_t dstlen)
    {
        (void)dstlen;
        return memset(dest, c, n);
    }

    // glibc's strerror_r, GNU variant (the toolchain's is the XSI one):
    // writes into the caller's buffer and returns a pointer to the string.
    char *bridgeStrerrorRGlibc(int errnum, char *buf, size_t buflen)
    {
        if (buf && buflen > 0)
        {
            const char *msg = strerror(errnum);
            size_t i = 0;
            while (msg && msg[i] && i + 1 < buflen)
            {
                buf[i] = msg[i];
                i++;
            }
            buf[i] = '\0';
        }
        return buf;
    }

    // caca's versioned (libcaca.so.0) symbols: terminal diagnostics the g6
    // games draw to a canvas that is never shown on Windows.
    void *bridgeCaca0CreateBitmap(int width, int height)
    {
        (void)width;
        (void)height;
        return calloc(1, 256);
    }

    void bridgeCaca0End(void)
    {
    }

    void bridgeCaca0FreeBitmap(void *bitmap)
    {
        (void)bitmap;
    }

    int bridgeCaca0GetEvent(void *canvas, void *event, int timeout)
    {
        (void)canvas;
        (void)event;
        (void)timeout;
        return 0;
    }

    int bridgeCaca0Init(void)
    {
        return 0;
    }

    // The few OpenSSL symbols the 1.0.x imports list needs beyond the
    // certificate/PEM set below.
    const void *bridgeTLSv1_2_client_method(void)
    {
        static int s_dummySslMethod;
        return &s_dummySslMethod;
    }

    unsigned long bridgeRAND_SSLeay(void)
    {
        return 0x10001000UL; // "OpenSSL 1.0.1"
    }

    int bridgeRAND_bytes(unsigned char *buf, int num)
    {
        if (buf && num > 0)
            memset(buf, 0, num);
        return 0;
    }

    int bridgeRAND_seed(const void *buf, int num)
    {
        (void)buf;
        (void)num;
        return 0;
    }

    int bridgeRAND_set_rand_method(const void *meth)
    {
        (void)meth;
        return 1;
    }

    int bridgeASN1_INTEGER_set(void *a, long v)
    {
        (void)a;
        (void)v;
        return 1;
    }

    int bridgeASN1_UTCTIME_set(void *a, long t)
    {
        (void)a;
        (void)t;
        return 1;
    }

    // ------------------------------------------------------------------
    // OpenSSL / caca / csv: the g6 games' (Angry Birds, The Walking
    // Dead) telemetry and HTTPS subsystem. It builds a local self-signed
    // cert and posts stats; the network is stubbed out, so the objects
    // are dummies that let the subsystem run to completion with nothing
    // behind it, instead of hitting the crash stub. Objects are left
    // allocated (freed by nothing): the process is short-lived, and
    // never freeing removes any double-free risk.

    static void *s_osslDummy(void)
    {
        return calloc(1, 1024);
    }

    static int s_dummyEvpMd[4] = {1, 2, 3, 4};
    static int s_dummyEvpCipher[6] = {1, 2, 3, 4, 5, 6};

    void bridgeOpenSSLAddAllAlgorithmsNoconf(void)
    {
    }

    void bridgeSSLLibraryInit(void)
    {
    }

    void bridgeErrLoadBIOStrings(void)
    {
    }

    void bridgeSSLLoadErrorStrings(void)
    {
    }

    unsigned long bridgeErrGetError(void)
    {
        return 0;
    }

    size_t bridgeErrErrorStringN(unsigned long e, char *buf, size_t n)
    {
        (void)e;
        if (buf && n)
        {
            size_t len = 2 < n ? 2 : n - 1;
            memcpy(buf, "ok", len);
            buf[len] = 0;
        }
        return 2;
    }

    void *bridgeBIO_new(const void *method)
    {
        (void)method;
        return s_osslDummy();
    }

    void *bridgeBIO_new_file(const char *filename, const char *mode)
    {
        (void)filename;
        (void)mode;
        return s_osslDummy();
    }

    void *bridgeBIO_new_mem_buf(const void *buf, int len)
    {
        (void)buf;
        (void)len;
        return s_osslDummy();
    }

    void *bridgeBIO_new_ssl_connect(void *ctx)
    {
        (void)ctx;
        return s_osslDummy();
    }

    void *bridgeBIO_s_mem(void)
    {
        return s_osslDummy();
    }

    void *bridgeBIO_s_null(void)
    {
        return s_osslDummy();
    }

    void *bridgeBIO_f_base64(void)
    {
        return s_osslDummy();
    }

    void *bridgeBIO_f_cipher(void)
    {
        return s_osslDummy();
    }

    void *bridgeBIO_f_md(void)
    {
        return s_osslDummy();
    }

    void *bridgeBIO_push(void *b, void *append)
    {
        (void)append;
        return b;
    }

    void *bridgeBIO_pop(void *b)
    {
        return b;
    }

    int bridgeBIO_read(void *bio, void *buf, int len)
    {
        (void)bio;
        (void)buf;
        (void)len;
        return 0;
    }

    int bridgeBIO_write(void *bio, const void *buf, int len)
    {
        (void)bio;
        (void)buf;
        return len;
    }

    long bridgeBIO_ctrl(void *bp, int cmd, long larg, void *parg)
    {
        (void)bp;
        (void)cmd;
        (void)larg;
        (void)parg;
        return 0;
    }

    int bridgeBIO_set_flags(void *b, int flags)
    {
        (void)b;
        (void)flags;
        return 1;
    }

    int bridgeBIO_test_flags(const void *s, int flags)
    {
        (void)s;
        (void)flags;
        return 0;
    }

    int bridgeBIO_set_cipher(void *b, const void *cipher, const void *key, const void *key2, const void *key3)
    {
        (void)b;
        (void)cipher;
        (void)key;
        (void)key2;
        (void)key3;
        return 1;
    }

    void bridgeBIO_free(void *a)
    {
        (void)a;
    }

    void bridgeBIO_free_all(void *a)
    {
        (void)a;
    }

    const void *bridgeEVP_md5(void)
    {
        return &s_dummyEvpMd[0];
    }

    const void *bridgeEVP_sha1(void)
    {
        return &s_dummyEvpMd[1];
    }

    const void *bridgeEVP_md_null(void)
    {
        return &s_dummyEvpMd[2];
    }

    const void *bridgeEVP_aes_128_cbc(void)
    {
        return &s_dummyEvpCipher[0];
    }

    const void *bridgeEVP_aes_256_cbc(void)
    {
        return &s_dummyEvpCipher[1];
    }

    const void *bridgeEVP_aes_256_cfb128(void)
    {
        return &s_dummyEvpCipher[2];
    }

    const void *bridgeEVP_aes_256_ofb(void)
    {
        return &s_dummyEvpCipher[3];
    }

    const void *bridgeEVP_bf_cbc(void)
    {
        return &s_dummyEvpCipher[4];
    }

    const void *bridgeEVP_enc_null(void)
    {
        return &s_dummyEvpCipher[5];
    }

    int bridgeEVP_MD_size(const void *md)
    {
        (void)md;
        return 16;
    }

    const void *bridgeEVP_MD_CTX_md(const void *ctx)
    {
        (void)ctx;
        return &s_dummyEvpMd[0];
    }

    int bridgeEVP_DigestFinal(unsigned char *md, void *ctx, const void *type)
    {
        (void)ctx;
        (void)type;
        if (md)
            memset(md, 0, 16);
        return 1;
    }

    int bridgeEVP_CIPHER_iv_length(const void *cipher)
    {
        (void)cipher;
        return 16;
    }

    int bridgeEVP_CIPHER_CTX_set_key_length(void *ctx, int len)
    {
        (void)ctx;
        (void)len;
        return 1;
    }

    void *bridgeEVP_PKEY_new(void)
    {
        return s_osslDummy();
    }

    int bridgeEVP_PKEY_assign(void *pkey, int type, void *key)
    {
        (void)pkey;
        (void)type;
        (void)key;
        return 1;
    }

    void bridgeEVP_PKEY_free(void *pkey)
    {
        (void)pkey;
    }

    void *bridgeRSA_generate_key(int bits, unsigned long e, void *callback, void *cb_data)
    {
        (void)bits;
        (void)e;
        (void)callback;
        (void)cb_data;
        return s_osslDummy();
    }

    int bridgeSSL_COMP_add_compression_method(int n, void *comp)
    {
        (void)n;
        (void)comp;
        return 0;
    }

    long bridgeSSL_ctrl(void *ssl, int cmd, long larg, void *parg)
    {
        (void)ssl;
        (void)cmd;
        (void)larg;
        (void)parg;
        return 0;
    }

    void *bridgeSSL_CTX_new(const void *method)
    {
        (void)method;
        return s_osslDummy();
    }

    int bridgeSSL_CTX_use_certificate(void *ctx, void *x)
    {
        (void)ctx;
        (void)x;
        return 1;
    }

    int bridgeSSL_CTX_use_certificate_chain_file(void *ctx, const char *file)
    {
        (void)ctx;
        (void)file;
        return 1;
    }

    int bridgeSSL_CTX_use_PrivateKey(void *ctx, void *pkey)
    {
        (void)ctx;
        (void)pkey;
        return 1;
    }

    int bridgeSSL_CTX_use_PrivateKey_file(void *ctx, const char *file, int type)
    {
        (void)ctx;
        (void)file;
        (void)type;
        return 1;
    }

    int bridgeSSL_CTX_check_private_key(const void *ctx)
    {
        (void)ctx;
        return 1;
    }

    int bridgeSSL_CTX_load_verify_locations(void *ctx, const char *file, const char *path)
    {
        (void)ctx;
        (void)file;
        (void)path;
        return 1;
    }

    int bridgeSSL_CTX_set_cipher_list(void *ctx, const char *str)
    {
        (void)ctx;
        (void)str;
        return 1;
    }

    void bridgeSSL_CTX_set_verify(void *ctx, int mode, void *callback)
    {
        (void)ctx;
        (void)mode;
        (void)callback;
    }

    void bridgeSSL_CTX_free(void *ctx)
    {
        (void)ctx;
    }

    void *bridgeSSL_get1_session(const void *ssl)
    {
        (void)ssl;
        return s_osslDummy();
    }

    int bridgeSSL_set_session(void *ssl, void *sess)
    {
        (void)ssl;
        (void)sess;
        return 1;
    }

    void bridgeSSL_SESSION_free(void *s)
    {
        (void)s;
    }

    void *bridgeX509_new(void)
    {
        return s_osslDummy();
    }

    void bridgeX509_free(void *x)
    {
        (void)x;
    }

    int bridgeX509_set_version(void *x, long version)
    {
        (void)x;
        (void)version;
        return 1;
    }

    int bridgeX509_set_issuer_name(void *x, void *name)
    {
        (void)x;
        (void)name;
        return 1;
    }

    int bridgeX509_set_pubkey(void *x, void *pkey)
    {
        (void)x;
        (void)pkey;
        return 1;
    }

    void *bridgeX509_get_serialNumber(void *x)
    {
        (void)x;
        return s_osslDummy();
    }

    void *bridgeX509_get_subject_name(void *x)
    {
        (void)x;
        return s_osslDummy();
    }

    int bridgeX509_NAME_add_entry_by_txt(void *name, const char *field, int type, const unsigned char *value, int len, int loc, int flag)
    {
        (void)name;
        (void)field;
        (void)type;
        (void)value;
        (void)len;
        (void)loc;
        (void)flag;
        return 1;
    }

    int bridgeX509_sign(void *x, void *pkey, const void *md)
    {
        (void)x;
        (void)pkey;
        (void)md;
        return 1;
    }

    int bridgeX509_STORE_CTX_get_error(void *ctx)
    {
        (void)ctx;
        return 0;
    }

    int bridgePEM_write_bio_PrivateKey(void *bp, void *x, const void *enc, void *kstr, int klen, void *callback, void *u)
    {
        (void)bp;
        (void)x;
        (void)enc;
        (void)kstr;
        (void)klen;
        (void)callback;
        (void)u;
        return 1;
    }

    int bridgePEM_write_bio_X509(void *bp, void *x)
    {
        (void)bp;
        (void)x;
        return 1;
    }

    int bridgeCaca_get_canvas_width(const void *cv)
    {
        (void)cv;
        return 80;
    }

    int bridgeCaca_get_canvas_height(const void *cv)
    {
        (void)cv;
        return 24;
    }

    int bridgeCaca_refresh_display(void)
    {
        return 0;
    }

    int bridgeCaca_dither_bitmap(const void *cv, int x, int y, int width, int height, int format, int pitch, const void *data)
    {
        (void)cv;
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        (void)format;
        (void)pitch;
        (void)data;
        return 0;
    }

    int bridgeCsv_write2(void *csv, const char *format, ...)
    {
        (void)csv;
        (void)format;
        return 0;
    }

    void initBridges()
    {
        // No __gxx_personality_v0 here: the game's C++ needs its own
        // libstdc++'s (the native one), libgcc's C personality only runs
        // cleanups and never catches (every throw would terminate).

        MAP("popen", bridgeRtPopen);
        MAP("system", bridgeRtSystem);

        MAP("__asprintf_chk", bridgeAsprintfChk);
        MAP("__fwprintf_chk", bridgeFwprintfChk);
        MAP("__printf_chk", bridgePrintfChk);
        MAP("__sprintf_chk", bridgeSprintfChk);
        MAP("__stpcpy_chk", bridgeStpcpyChk);
        MAP("__strcat_chk", bridgeStrcatChk);
        MAP("__strcpy_chk", bridgeStrcpyChk);
        MAP("__swprintf_chk", bridgeSwprintfChk);
        MAP("__vsnprintf_chk", bridgeVsnprintfChk);
        MAP("__vsprintf_chk", bridgeVsprintfChk);
        MAP("__wcscpy_chk", bridgeWcscpyChk);
        MAP("__strncpy_chk", bridgeStrncpyChk);
        MAP("__strncat_chk", bridgeStrncatChk);
        MAP("__fread_chk", bridgeFreadChk);
        MAP("__fdelt_chk", bridgeFdeltaChk);

        MAP("__finitef", bridgeFinitef);
        MAP("__isoc99_fscanf", bridgeIsoFscanf);
        MAP("__isoc99_sscanf", bridgeIsoSscanf);
        MAP("__open_2", bridgeOpen2);
        MAP("__strtok_r", bridgeStrtokR);
        MAP("__h_errno_location", bridgeHErrnoLocation);
        MAP("__libc_current_sigrtmin", bridgeSigRtMin);
        MAP("__sysv_signal", bridgeSysvSignal);

        // Misc libc and glibc internals (see the definitions above).
        MAP("mprotect", bridgeMprotect);
        MAP("getrlimit", bridgeGetrlimit);
        MAP("ptrace", bridgePtrace);
        MAP("setpriority", bridgeSetpriority);
        MAP("prctl", bridgePrctl);
        MAP("timegm", bridgeTimegm);
        MAP("scalbn", bridgeScalbn);
        MAP("scalbnf", bridgeScalbnf);
        MAP("strtoll", bridgeStrtoll);
        MAP("__isoc99_vsscanf", bridgeIsoVsscanf);
        MAP("getlogin", bridgeGetlogin);
        MAP("ttyname", bridgeTtyname);
        MAP("getutxent", bridgeGetutxent);
        MAP("setutxent", bridgeSetutxent);
        MAP("endutxent", bridgeEndutxent);
        MAP("getpgid", bridgeGetpgid);
        MAP("setpgid", bridgeSetpgid);
        MAP("gnu_dev_major", bridgeGnuDevMajor);
        MAP("gnu_dev_minor", bridgeGnuDevMinor);
        MAP("gnu_dev_makedev", bridgeGnuMakedev);
        MAP("sigqueue", bridgeSigqueue);
        MAP("getifaddrs", bridgeGetifaddrs);
        MAP("freeifaddrs", bridgeFreeifaddrs);
        MAP("getnameinfo", bridgeGetnameinfo);
        MAP("sem_timedwait", bridgeSemTimedwait);
        MAP("statvfs", bridgeStatvfs);
        MAP("statfs", bridgeStatfs);
        MAP("dl_iterate_phdr", bridgeDlIteratePhdr);

        // Remaining libc/libm names.
        MAP("ctime", bridgeCtime);
        MAP("putenv", bridgePutenv);
        MAP("trunc", bridgeTrunc);
        MAP("strnlen", bridgeStrnlen);
        MAP("iswalpha", bridgeIsWalpha);
        MAP("wcstoul", bridgeWcstoul);
        MAP("tzset", bridgeTzset);
        MAP("bcopy", bridgeBcopy);
        MAP("getnetbyname", bridgeGetnetbyname);
        MAP("__fpclassifyf", bridgeFpclassifyf);
        MAP("__isoc99_swscanf", bridgeIsoc99Swscanf);
        MAP("__vfprintf_chk", bridgeVfprintfChk);
        MAP("__wcscat_chk", bridgeWcscatChk);
        MAP("__wcsncat_chk", bridgeWcsncatChk);
        MAP("__memset_chk", bridgeMemsetChk);
        MAP("strerror_r", bridgeStrerrorRGlibc);
        MAP("__caca0_create_bitmap", bridgeCaca0CreateBitmap);
        MAP("__caca0_end", bridgeCaca0End);
        MAP("__caca0_free_bitmap", bridgeCaca0FreeBitmap);
        MAP("__caca0_get_event", bridgeCaca0GetEvent);
        MAP("__caca0_init", bridgeCaca0Init);
        MAP("TLSv1_2_client_method", bridgeTLSv1_2_client_method);
        MAP("RAND_SSLeay", bridgeRAND_SSLeay);
        MAP("RAND_bytes", bridgeRAND_bytes);
        MAP("RAND_seed", bridgeRAND_seed);
        MAP("RAND_set_rand_method", bridgeRAND_set_rand_method);
        MAP("ASN1_INTEGER_set", bridgeASN1_INTEGER_set);
        MAP("ASN1_UTCTIME_set", bridgeASN1_UTCTIME_set);

        // OpenSSL / caca / csv: the g6 telemetry and HTTPS subsystem.
        MAP("OPENSSL_add_all_algorithms_noconf", bridgeOpenSSLAddAllAlgorithmsNoconf);
        MAP("SSL_library_init", bridgeSSLLibraryInit);
        MAP("ERR_load_BIO_strings", bridgeErrLoadBIOStrings);
        MAP("SSL_load_error_strings", bridgeSSLLoadErrorStrings);
        MAP("ERR_get_error", bridgeErrGetError);
        MAP("ERR_error_string_n", bridgeErrErrorStringN);
        MAP("BIO_new", bridgeBIO_new);
        MAP("BIO_new_file", bridgeBIO_new_file);
        MAP("BIO_new_mem_buf", bridgeBIO_new_mem_buf);
        MAP("BIO_new_ssl_connect", bridgeBIO_new_ssl_connect);
        MAP("BIO_s_mem", bridgeBIO_s_mem);
        MAP("BIO_s_null", bridgeBIO_s_null);
        MAP("BIO_f_base64", bridgeBIO_f_base64);
        MAP("BIO_f_cipher", bridgeBIO_f_cipher);
        MAP("BIO_f_md", bridgeBIO_f_md);
        MAP("BIO_push", bridgeBIO_push);
        MAP("BIO_pop", bridgeBIO_pop);
        MAP("BIO_read", bridgeBIO_read);
        MAP("BIO_write", bridgeBIO_write);
        MAP("BIO_ctrl", bridgeBIO_ctrl);
        MAP("BIO_set_flags", bridgeBIO_set_flags);
        MAP("BIO_test_flags", bridgeBIO_test_flags);
        MAP("BIO_set_cipher", bridgeBIO_set_cipher);
        MAP("BIO_free", bridgeBIO_free);
        MAP("BIO_free_all", bridgeBIO_free_all);
        MAP("EVP_md5", bridgeEVP_md5);
        MAP("EVP_sha1", bridgeEVP_sha1);
        MAP("EVP_md_null", bridgeEVP_md_null);
        MAP("EVP_aes_128_cbc", bridgeEVP_aes_128_cbc);
        MAP("EVP_aes_256_cbc", bridgeEVP_aes_256_cbc);
        MAP("EVP_aes_256_cfb128", bridgeEVP_aes_256_cfb128);
        MAP("EVP_aes_256_ofb", bridgeEVP_aes_256_ofb);
        MAP("EVP_bf_cbc", bridgeEVP_bf_cbc);
        MAP("EVP_enc_null", bridgeEVP_enc_null);
        MAP("EVP_MD_size", bridgeEVP_MD_size);
        MAP("EVP_MD_CTX_md", bridgeEVP_MD_CTX_md);
        MAP("EVP_DigestFinal", bridgeEVP_DigestFinal);
        MAP("EVP_CIPHER_iv_length", bridgeEVP_CIPHER_iv_length);
        MAP("EVP_CIPHER_CTX_set_key_length", bridgeEVP_CIPHER_CTX_set_key_length);
        MAP("EVP_PKEY_new", bridgeEVP_PKEY_new);
        MAP("EVP_PKEY_assign", bridgeEVP_PKEY_assign);
        MAP("EVP_PKEY_free", bridgeEVP_PKEY_free);
        MAP("RSA_generate_key", bridgeRSA_generate_key);
        MAP("SSL_COMP_add_compression_method", bridgeSSL_COMP_add_compression_method);
        MAP("SSL_ctrl", bridgeSSL_ctrl);
        MAP("SSL_CTX_new", bridgeSSL_CTX_new);
        MAP("SSL_CTX_use_certificate", bridgeSSL_CTX_use_certificate);
        MAP("SSL_CTX_use_certificate_chain_file", bridgeSSL_CTX_use_certificate_chain_file);
        MAP("SSL_CTX_use_PrivateKey", bridgeSSL_CTX_use_PrivateKey);
        MAP("SSL_CTX_use_PrivateKey_file", bridgeSSL_CTX_use_PrivateKey_file);
        MAP("SSL_CTX_check_private_key", bridgeSSL_CTX_check_private_key);
        MAP("SSL_CTX_load_verify_locations", bridgeSSL_CTX_load_verify_locations);
        MAP("SSL_CTX_set_cipher_list", bridgeSSL_CTX_set_cipher_list);
        MAP("SSL_CTX_set_verify", bridgeSSL_CTX_set_verify);
        MAP("SSL_CTX_free", bridgeSSL_CTX_free);
        MAP("SSL_get1_session", bridgeSSL_get1_session);
        MAP("SSL_set_session", bridgeSSL_set_session);
        MAP("SSL_SESSION_free", bridgeSSL_SESSION_free);
        MAP("X509_new", bridgeX509_new);
        MAP("X509_free", bridgeX509_free);
        MAP("X509_set_version", bridgeX509_set_version);
        MAP("X509_set_issuer_name", bridgeX509_set_issuer_name);
        MAP("X509_set_pubkey", bridgeX509_set_pubkey);
        MAP("X509_get_serialNumber", bridgeX509_get_serialNumber);
        MAP("X509_get_subject_name", bridgeX509_get_subject_name);
        MAP("X509_NAME_add_entry_by_txt", bridgeX509_NAME_add_entry_by_txt);
        MAP("X509_sign", bridgeX509_sign);
        MAP("X509_STORE_CTX_get_error", bridgeX509_STORE_CTX_get_error);
        MAP("PEM_write_bio_PrivateKey", bridgePEM_write_bio_PrivateKey);
        MAP("PEM_write_bio_X509", bridgePEM_write_bio_X509);
        MAP("caca_get_canvas_width", bridgeCaca_get_canvas_width);
        MAP("caca_get_canvas_height", bridgeCaca_get_canvas_height);
        MAP("caca_refresh_display", bridgeCaca_refresh_display);
        MAP("caca_dither_bitmap", bridgeCaca_dither_bitmap);
        MAP("csv_write2", bridgeCsv_write2);

        MAP("asctime_r", bridgeAsctimeR);
        MAP("atol", bridgeAtol);
        MAP("atoll", bridgeAtoll);
        MAP("backtrace", bridgeBacktrace);
        MAP("backtrace_symbols", bridgeBacktraceSymbols);
        MAP("ceilf", bridgeCeilf);
        MAP("floorf", bridgeFloorf);
        MAP("clock", bridgeClock);
        MAP("clock_getres", bridgeClockGetres);
        MAP("sysconf", bridgeSysconf);
        MAP("dladdr", bridgeDladdr);
        MAP("dlvsym", bridgeDlvsym);
        MAP("endmntent", bridgeEndmntent);
        MAP("endutent", bridgeEndutent);
        MAP("fgetpos", bridgeFgetpos);
        MAP("fsetpos", bridgeFsetpos);
        MAP("fwprintf", bridgeFwprintf);
        MAP("freeaddrinfo", bridgeFreeAddrInfo);
        MAP("getaddrinfo", bridgeGetAddrInfo);
        MAP("getgid", bridgeGetGid);
        MAP("getlogin_r", bridgeGetloginR);
        MAP("getmntent", bridgeGetmntent);
        MAP("getppid", bridgeGetPpid);
        MAP("getpwuid_r", bridgeGetPwuidR);
        MAP("getutent_r", bridgeGetUtentR);
        MAP("iconv", bridgeIconv);
        MAP("iconv_close", bridgeIconvClose);
        MAP("iconv_open", bridgeIconvOpen);
        MAP("inet_ntop", bridgeInetNtop);
        MAP("ldexp", bridgeLdexp);
        MAP("lrint", bridgeLrint);
        MAP("sincos", bridgeSincos);
        MAP("sincosf", bridgeSincosf);
        MAP("setmntent", bridgeSetmntent);
        MAP("setutent", bridgeSetutent);
        MAP("sigdelset", bridgeSigdelset);
        MAP("sigprocmask", bridgeSigprocmask);
        MAP("stpcpy", bridgeStpcpy);
        MAP("strtoull", bridgeStrtoull);
        MAP("swprintf", bridgeSwprintf);
        MAP("swscanf", bridgeSwscanf);
        MAP("syscall", bridgeSyscall);
        MAP("sysinfo", bridgeSysinfo);
        MAP("timerfd_create", bridgeTimerfdCreate);
        MAP("timerfd_settime", bridgeTimerfdSettime);
        MAP("ttyname_r", bridgeTtynameR);
        MAP("vasprintf", bridgeVasprintf);
        MAP("wcscasecmp", bridgeWcscasecmp);
        MAP("wcscat", bridgeWcscat);
        MAP("wcsdup", bridgeWcsdup);
        MAP("wcsncat", bridgeWcsncat);
        MAP("wcstok", bridgeWcstok);
        MAP("wcstoll", bridgeWcstoll);
        MAP("wcstoull", bridgeWcstoull);

        MAP("pthread_attr_setinheritsched", bridgePthreadAttrSetinheritsched);
        MAP("pthread_kill", bridgePthreadKill);
        MAP("pthread_sigmask", bridgePthreadSigmask);

        MAP("udev_new", bridgeUdevNew);
        MAP("udev_unref", bridgeUdevUnref);
        MAP("udev_enumerate_new", bridgeUdevEnumerateNew);
        MAP("udev_enumerate_add_match_subsystem", bridgeUdevEnumerateAddMatchSubsystem);
        MAP("udev_enumerate_scan_devices", bridgeUdevEnumerateScanDevices);
        MAP("udev_enumerate_get_list_entry", bridgeUdevEnumerateGetListEntry);
        MAP("udev_enumerate_unref", bridgeUdevEnumerateUnref);
        MAP("udev_list_entry_get_name", bridgeUdevListEntryGetName);
        MAP("udev_list_entry_get_next", bridgeUdevListEntryGetNext);
        MAP("udev_device_new_from_syspath", bridgeUdevDeviceNewFromSyspath);
        MAP("udev_device_new_from_devnum", bridgeUdevDeviceNewFromDevnum);
        MAP("udev_device_get_parent_with_subsystem_devtype", bridgeUdevDeviceGetParentWithSubsystemDevtype);
        MAP("udev_device_get_action", bridgeUdevDeviceGetAction);
        MAP("udev_device_get_devnode", bridgeUdevDeviceGetDevnode);
        MAP("udev_device_get_sysname", bridgeUdevDeviceGetSysname);
        MAP("udev_device_get_sysattr_value", bridgeUdevDeviceGetSysattrValue);
        MAP("udev_device_unref", bridgeUdevDeviceUnref);
        MAP("udev_monitor_new_from_netlink", bridgeUdevMonitorNewFromNetlink);
        MAP("udev_monitor_enable_receiving", bridgeUdevMonitorEnableReceiving);
        MAP("udev_monitor_filter_add_match_subsystem_devtype", bridgeUdevMonitorFilterAddMatchSubsystemDevtype);
        MAP("udev_monitor_get_fd", bridgeUdevMonitorGetFd);
        MAP("udev_monitor_receive_device", bridgeUdevMonitorReceiveDevice);
        MAP("udev_monitor_unref", bridgeUdevMonitorUnref);

        MAP("ogg_sync_init", bridgeOggSyncInit);
        MAP("ogg_sync_buffer", bridgeOggSyncBuffer);
        MAP("ogg_sync_wrote", bridgeOggSyncWrote);
        MAP("ogg_sync_clear", bridgeOggSyncClear);
        MAP("ogg_sync_pageout", bridgeOggSyncPageout);
        MAP("ogg_page_bos", bridgeOggPageBos);
        MAP("ogg_page_serialno", bridgeOggPageSerialno);
        MAP("ogg_stream_init", bridgeOggStreamInit);
        MAP("ogg_stream_pagein", bridgeOggStreamPagein);
        MAP("ogg_stream_flush", bridgeOggStreamFlush);
        MAP("ogg_stream_pageout", bridgeOggStreamPageout);
        MAP("ogg_stream_packetin", bridgeOggStreamPacketin);
        MAP("ogg_stream_packetout", bridgeOggStreamPacketout);
        MAP("ogg_stream_clear", bridgeOggStreamClear);

        MAP("theora_info_init", bridgeTheoraInfoInit);
        MAP("theora_info_clear", bridgeTheoraInfoClear);
        MAP("theora_decode_init", bridgeTheoraDecodeInit);
        MAP("theora_decode_header", bridgeTheoraDecodeHeader);
        MAP("theora_decode_packetin", bridgeTheoraDecodePacketin);
        MAP("theora_decode_YUVout", bridgeTheoraDecodeYUVout);
        MAP("theora_granule_frame", bridgeTheoraGranuleFrame);
        MAP("theora_granule_time", bridgeTheoraGranuleTime);
        MAP("theora_comment_init", bridgeTheoraCommentInit);
        MAP("theora_comment_clear", bridgeTheoraCommentClear);
        MAP("theora_comment_query", bridgeTheoraCommentQuery);
        MAP("theora_comment_add", bridgeTheoraCommentAdd);
        MAP("theora_encode_init", bridgeTheoraEncodeInit);
        MAP("theora_encode_init2", bridgeTheoraEncodeInit2);
        MAP("theora_encode_header", bridgeTheoraEncodeHeader);
        MAP("theora_encode_packetout", bridgeTheoraEncodePacketout);
        MAP("theora_encode_YUVin", bridgeTheoraEncodeYUVin);
        MAP("theora_encode_comment", bridgeTheoraEncodeComment);
        MAP("theora_encode_tables", bridgeTheoraEncodeTables);
        MAP("theora_clear", bridgeTheoraClear);

        MAP("NxGetCookingLib", bridgeNxGetCookingLib);
    }
} // namespace RtBridge

#endif
