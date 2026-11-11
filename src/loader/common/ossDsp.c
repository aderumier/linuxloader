// OSS /dev/dsp emulation on top of SDL3 audio, for the PC-based arcade
// systems: the Raw Thrills g5 engine's JPS sound engine, the OSS backend of
// the OpenAL the Namco ES1 games ship, the Teamplay and Global VR games.
// The games only use the basic OSS API: format/channels/rate setup, fragment
// setup, GETOSPACE, mixer volume ioctls and blocking writes. The IGS games
// open /dev/mixer too: see ossMixerIsPath.

#if !defined(_WIN32) && !defined(__MINGW32__)
#include <dlfcn.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(__MINGW32__)
#include <sys/soundcard.h>
#else
// The Linux soundcard definitions the games were built against.
typedef struct
{
    int fragstotal;
    int fragsize;
    int fragments;
    int bytes;
} audio_buf_info;
#define AFMT_S16_LE 0x0008
#define AFMT_U8 0x0020
#define _IOC(dir, t, nr, sz) (((dir) << 30) | ((t) << 8) | ((nr) << 0) | ((sz) << 16))
#define _IO(t, nr) _IOC(0, (t), (nr), 0)
#define _IOR(t, nr, sz) _IOC(2, (t), (nr), sizeof(sz))
#define _IOW(t, nr, sz) _IOC(1, (t), (nr), sizeof(sz))
#define _IOWR(t, nr, sz) _IOC(3, (t), (nr), sizeof(sz))
#define SNDCTL_DSP_RESET _IOW('A', 0x00, int)
#define SNDCTL_DSP_SYNC _IO('A', 0x01)
#define SNDCTL_DSP_SPEED _IOWR('A', 0x02, int)
#define SNDCTL_DSP_STEREO _IOWR('A', 0x03, int)
#define SNDCTL_DSP_SETFMT _IOWR('A', 0x04, int)
#define SNDCTL_DSP_GETFMTS _IOR('A', 0x05, int)
#define SNDCTL_DSP_CHANNELS _IOWR('A', 0x06, int)
#define SNDCTL_DSP_GETBLKSIZE _IO('A', 0x07)
#define SNDCTL_DSP_SETFRAGMENT _IOWR('A', 0x08, int)
#define SNDCTL_DSP_GETOSPACE _IOR('A', 0x09, audio_buf_info)
#define SNDCTL_DSP_GETODELAY _IOR('A', 0x0a, int)
#endif
#include <unistd.h>

#include <SDL3/SDL.h>

#include "ossDsp.h"
#include "../rawthrills/rawthrills.h"
#include "../namco/namcoEs1.h"
#include "../teamplay/teamplay.h"
#include "../globalvr/gvr.h"
#include "../igs/igs.h"
#include "../log/log.h"

#define DSP_FRAGMENTS 4
#define DSP_FRAGMENT_SIZE 4096

// The game links SDL 1.2, whose SDL_Init/SDL_WasInit/SDL_Delay/... shadow
// SDL3's in the global scope: call SDL3 through its own library handle.
static struct
{
    SDL_InitFlags (*WasInit)(SDL_InitFlags);
    bool (*InitSubSystem)(SDL_InitFlags);
    const char *(*GetError)(void);
    SDL_AudioStream *(*OpenAudioDeviceStream)(SDL_AudioDeviceID, const SDL_AudioSpec *, SDL_AudioStreamCallback, void *);
    bool (*ResumeAudioStreamDevice)(SDL_AudioStream *);
    void (*DestroyAudioStream)(SDL_AudioStream *);
    int (*GetAudioStreamQueued)(SDL_AudioStream *);
    bool (*PutAudioStreamData)(SDL_AudioStream *, const void *, int);
    bool (*ClearAudioStream)(SDL_AudioStream *);
    void (*Delay)(Uint32);
} sdl3;

static int loadSdl3(void)
{
    void *h;
    if (sdl3.Delay)
        return 0;
#if defined(_WIN32) || defined(__MINGW32__)
    // The Windows loader links SDL3 statically: the names are local.
    sdl3.WasInit = SDL_WasInit;
    sdl3.InitSubSystem = SDL_InitSubSystem;
    sdl3.GetError = SDL_GetError;
    sdl3.OpenAudioDeviceStream = SDL_OpenAudioDeviceStream;
    sdl3.ResumeAudioStreamDevice = SDL_ResumeAudioStreamDevice;
    sdl3.DestroyAudioStream = SDL_DestroyAudioStream;
    sdl3.GetAudioStreamQueued = SDL_GetAudioStreamQueued;
    sdl3.PutAudioStreamData = SDL_PutAudioStreamData;
    sdl3.ClearAudioStream = SDL_ClearAudioStream;
    sdl3.Delay = SDL_Delay;
    return 0;
#else
    if (!(h = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_NOLOAD)) && !(h = dlopen("libSDL3.so.0", RTLD_NOW)))
    {
        log_error("Raw Thrills: libSDL3.so.0 not found: %s", dlerror());
        return -1;
    }
#define LOAD(name) *(void **)&sdl3.name = dlsym(h, "SDL_" #name)
    LOAD(WasInit);
    LOAD(InitSubSystem);
    LOAD(GetError);
    LOAD(OpenAudioDeviceStream);
    LOAD(ResumeAudioStreamDevice);
    LOAD(DestroyAudioStream);
    LOAD(GetAudioStreamQueued);
    LOAD(PutAudioStreamData);
    LOAD(ClearAudioStream);
    LOAD(Delay);
#undef LOAD
    if (!sdl3.WasInit || !sdl3.InitSubSystem || !sdl3.OpenAudioDeviceStream || !sdl3.Delay)
        return -1;
    return 0;
#endif
}

static int dspFd = -1;
static SDL_AudioStream *dspStream;
static SDL_AudioSpec dspSpec = {SDL_AUDIO_S16LE, 2, 44100};
static int fragments = DSP_FRAGMENTS;
static int fragmentSize = DSP_FRAGMENT_SIZE;

int ossDspIsPath(const char *path)
{
    return (isRawThrillsGame() || isNamcoEs1Game() || isTeamplayGame()
#ifdef __linux__
            || isGvrGame() || isIgsGame()
#endif
            ) &&
           path && strcmp(path, "/dev/dsp") == 0;
}

int ossDspIsFd(int fd)
{
    return fd >= 0 && fd == dspFd;
}

// The device is (re)created on the first write, once the game has chosen
// its format.
static int dspStart(void)
{
    if (dspStream)
        return 0;
    if (loadSdl3() != 0)
        return -1;
    if (!sdl3.WasInit(SDL_INIT_AUDIO) && !sdl3.InitSubSystem(SDL_INIT_AUDIO))
    {
        log_error("Raw Thrills: SDL audio init failed: %s", sdl3.GetError());
        return -1;
    }
    dspStream = sdl3.OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &dspSpec, NULL, NULL);
    if (!dspStream)
    {
        log_error("Raw Thrills: cannot open audio stream: %s", sdl3.GetError());
        return -1;
    }
    sdl3.ResumeAudioStreamDevice(dspStream);
    return 0;
}

static void dspStop(void)
{
    if (dspStream)
    {
        sdl3.DestroyAudioStream(dspStream);
        dspStream = NULL;
    }
}

int ossDspOpen(int flags)
{
    if ((flags & O_ACCMODE) == O_RDONLY)
    {
        errno = ENODEV;
        return -1;
    }
    if (dspFd >= 0)
        return dspFd;
    // A real descriptor the game can keep; all I/O on it is intercepted.
#if defined(_WIN32) || defined(__MINGW32__)
    dspFd = _open("nul", O_WRONLY);
#else
    dspFd = open("/dev/null", O_WRONLY | O_CLOEXEC);
#endif
    return dspFd;
}

void ossDspClose(void)
{
    dspStop();
    dspFd = -1;
}

// OSS /dev/mixer, for the IGS games: they play no sound unless it opens
// (Gold Captain) or exit unless it answers its devices (Pirate Revenge). A
// master and a PCM volume, kept and read back; the sound itself is not
// scaled.
#if !defined(_WIN32) && !defined(__MINGW32__)
static int mixerFd = -1;
static int mixerVolumes[SOUND_MIXER_NRDEVICES];

int ossMixerIsPath(const char *path)
{
    return isIgsGame() && path && strcmp(path, "/dev/mixer") == 0;
}

int ossMixerIsFd(int fd)
{
    return fd >= 0 && fd == mixerFd;
}

int ossMixerOpen(void)
{
    if (mixerFd >= 0)
        return mixerFd;
    for (int i = 0; i < SOUND_MIXER_NRDEVICES; i++)
        mixerVolumes[i] = 100 | 100 << 8;
    mixerFd = open("/dev/null", O_RDWR | O_CLOEXEC);
    return mixerFd;
}

void ossMixerClose(void)
{
    mixerFd = -1;
}

int ossMixerIoctl(unsigned long request, void *arg)
{
    int *value = arg;
    unsigned int device = _IOC_NR(request);

    if (!value || _IOC_TYPE(request) != 'M')
        return 0;
    switch (request)
    {
    case SOUND_MIXER_READ_DEVMASK:
    case SOUND_MIXER_READ_STEREODEVS:
        *value = SOUND_MASK_VOLUME | SOUND_MASK_PCM;
        return 0;
    case SOUND_MIXER_READ_RECMASK:
    case SOUND_MIXER_READ_RECSRC:
    case SOUND_MIXER_READ_CAPS:
        *value = 0;
        return 0;
    }
    if (device < SOUND_MIXER_NRDEVICES)
    {
        // MIXER_WRITE is read-write: the volume set is handed back.
        if (_IOC_DIR(request) & _IOC_WRITE)
            mixerVolumes[device] = *value;
        else
            *value = mixerVolumes[device];
    }
    return 0;
}
#else
// No IGS game on Windows: no mixer to open.
int ossMixerIsPath(const char *path)
{
    (void)path;
    return 0;
}

int ossMixerIsFd(int fd)
{
    (void)fd;
    return 0;
}

int ossMixerOpen(void)
{
    return -1;
}

void ossMixerClose(void)
{
}

int ossMixerIoctl(unsigned long request, void *arg)
{
    (void)request;
    (void)arg;
    return 0;
}
#endif

static int bufferSize(void)
{
    return fragments * fragmentSize;
}

static int queuedBytes(void)
{
    return dspStream ? sdl3.GetAudioStreamQueued(dspStream) : 0;
}

// Like OSS, block while the device buffer is full.
ssize_t ossDspWrite(const void *buf, size_t count)
{
    if (dspStart() != 0)
        return (ssize_t)count;
    while (queuedBytes() > bufferSize())
        sdl3.Delay(1);
    if (!sdl3.PutAudioStreamData(dspStream, buf, (int)count))
        return -1;
    return (ssize_t)count;
}

int ossDspIoctl(unsigned long request, void *arg)
{
    static int trace = -1;
    int *value = arg;

    if (trace < 0)
        trace = getenv("OSS_DSP_TRACE") != NULL;
    if (trace && request != SNDCTL_DSP_GETOSPACE && request != SNDCTL_DSP_GETODELAY)
        fprintf(stderr, "OSS dsp: ioctl %08lx %d\n", request, value ? *value : 0);

    switch (request)
    {
    case SNDCTL_DSP_RESET:
    case SNDCTL_DSP_SYNC:
        if (dspStream)
            sdl3.ClearAudioStream(dspStream);
        return 0;
    case SNDCTL_DSP_GETFMTS:
        *value = AFMT_S16_LE | AFMT_U8;
        return 0;
    case SNDCTL_DSP_SETFMT:
        if (*value == AFMT_U8)
            dspSpec.format = SDL_AUDIO_U8;
        else
            *value = AFMT_S16_LE, dspSpec.format = SDL_AUDIO_S16LE;
        dspStop();
        return 0;
    case SNDCTL_DSP_CHANNELS:
        dspSpec.channels = *value > 0 ? *value : 2;
        dspStop();
        return 0;
    case SNDCTL_DSP_STEREO:
        dspSpec.channels = *value ? 2 : 1;
        dspStop();
        return 0;
    case SNDCTL_DSP_SPEED:
        dspSpec.freq = *value > 0 ? *value : 44100;
        dspStop();
        return 0;
    case SNDCTL_DSP_SETFRAGMENT:
        // 0xMMMMSSSS: max fragments, log2 of the fragment size.
        fragments = (*value >> 16) & 0xffff;
        fragmentSize = 1 << (*value & 0xffff);
        if (fragments <= 0 || fragments > 64)
            fragments = DSP_FRAGMENTS;
        if (fragmentSize < 256 || fragmentSize > 65536)
            fragmentSize = DSP_FRAGMENT_SIZE;
        return 0;
    case SNDCTL_DSP_GETBLKSIZE:
        *value = fragmentSize;
        return 0;
    case SNDCTL_DSP_GETOSPACE:
    {
        audio_buf_info *info = arg;
        int space = bufferSize() - queuedBytes();
        if (space < 0)
            space = 0;
        info->fragstotal = fragments;
        info->fragsize = fragmentSize;
        info->fragments = space / fragmentSize;
        info->bytes = space;
        return 0;
    }
    case SNDCTL_DSP_GETODELAY:
        *value = queuedBytes();
        return 0;
    default:
        // Mixer volume settings (SOUND_MIXER_WRITE_*) and anything else.
        return 0;
    }
}
