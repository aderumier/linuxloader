// libasound (see ll64.h): the PCM playback calls FMOD makes (Halo), on
// waveOut. One device, "default", 16-bit interleaved; the ALSA buffer is
// a ring of waveOut buffers of a period each.
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include "ll64.h"

SYSV void *lxStrdup(const char *s);

#define FORMAT_S16_LE 2
#define MAX_PERIODS 16
#define EAGAIN 11
#define EINVAL 22
#define EIO 5

typedef struct
{
    int channels, rate, nonblock, running;
    lx_ulong periodFrames, bufferFrames;
    int periods, current;
    size_t fill;   // bytes in the period being filled
    HWAVEOUT out;
    HANDLE done;
    WAVEHDR hdr[MAX_PERIODS];
    uint8_t *data[MAX_PERIODS];
} Pcm;

static size_t frameBytes(const Pcm *p) { return (size_t)p->channels * 2; }

static SYSV const char *alsaVersion(void) { return "1.1.3"; }
static SYSV const char *alsaStrerror(int e) { (void)e; return "ALSA error"; }

// Device hints: one, the default device.
static char *const hintList[] = {(char *)"default", NULL};
static SYSV int nameHint(int card, const char *iface, void ***hints)
{
    (void)card;
    (void)iface;
    *hints = (void **)hintList;
    return 0;
}
static SYSV int nameFreeHint(void **hints) { (void)hints; return 0; }
static SYSV char *nameGetHint(const void *hint, const char *id)
{
    if (!strcmp(id, "NAME"))
        return lxStrdup("default");
    if (!strcmp(id, "DESC"))
        return lxStrdup("Windows default output");
    return NULL;   // IOID: both directions
}

static SYSV int pcmOpen(Pcm **pcm, const char *name, int stream, int mode)
{
    Pcm *p;
    if (stream != 0)
        return -EINVAL;   // playback only
    if (!(p = calloc(1, sizeof(*p))))
        return -EINVAL;
    p->channels = 2;
    p->rate = 48000;
    p->periodFrames = 1024;
    p->bufferFrames = 4096;
    p->nonblock = (mode & 1) != 0;
    *pcm = p;
    lxLog("alsa: %s open\n", name ? name : "(null)");
    return 0;
}

static void stop(Pcm *p)
{
    if (!p->out)
        return;
    waveOutReset(p->out);
    for (int i = 0; i < p->periods; i++)
    {
        if (p->hdr[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(p->out, &p->hdr[i], sizeof(WAVEHDR));
        free(p->data[i]);
        p->data[i] = NULL;
    }
    waveOutClose(p->out);
    CloseHandle(p->done);
    p->out = NULL;
}

static SYSV int pcmClose(Pcm *p)
{
    if (!p)
        return 0;
    stop(p);
    free(p);
    return 0;
}

// Hardware parameters: kept in the PCM itself, the params block unused.
static SYSV size_t hwSizeof(void) { return 1024; }
static SYSV int hwAny(Pcm *p, void *hw) { (void)p; (void)hw; return 0; }
static SYSV int hwSetAccess(Pcm *p, void *hw, int access)
{
    (void)p;
    (void)hw;
    return access == 3 ? 0 : -EINVAL;   // SND_PCM_ACCESS_RW_INTERLEAVED
}
static SYSV int hwSetFormat(Pcm *p, void *hw, int format)
{
    (void)p;
    (void)hw;
    return format == FORMAT_S16_LE ? 0 : -EINVAL;
}
static SYSV int hwSetChannels(Pcm *p, void *hw, unsigned n)
{
    (void)hw;
    if (n < 1 || n > 8)
        return -EINVAL;
    p->channels = (int)n;
    return 0;
}
static SYSV int hwSetRate(Pcm *p, void *hw, unsigned rate, int dir)
{
    (void)hw;
    (void)dir;
    p->rate = (int)rate;
    return 0;
}
static SYSV int hwTestRate(Pcm *p, void *hw, unsigned rate, int dir)
{
    (void)p;
    (void)hw;
    (void)dir;
    return rate >= 8000 && rate <= 192000 ? 0 : -EINVAL;
}
static SYSV int hwSetBufferNear(Pcm *p, void *hw, lx_ulong *frames)
{
    (void)hw;
    p->bufferFrames = *frames;
    return 0;
}
static SYSV int hwSetPeriodNear(Pcm *p, void *hw, lx_ulong *frames, int *dir)
{
    (void)hw;
    (void)dir;
    if (*frames < 64)
        *frames = 64;
    p->periodFrames = *frames;
    return 0;
}

static SYSV int hwParams(Pcm *p, void *hw)
{
    WAVEFORMATEX wf = {WAVE_FORMAT_PCM};
    MMRESULT r;
    (void)hw;
    stop(p);
    p->periods = (int)(p->bufferFrames / p->periodFrames);
    if (p->periods < 2)
        p->periods = 2;
    if (p->periods > MAX_PERIODS)
        p->periods = MAX_PERIODS;
    p->bufferFrames = p->periodFrames * (lx_ulong)p->periods;
    wf.nChannels = (WORD)p->channels;
    wf.nSamplesPerSec = (DWORD)p->rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = (WORD)frameBytes(p);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    p->done = CreateEventW(NULL, FALSE, FALSE, NULL);
    r = waveOutOpen(&p->out, WAVE_MAPPER, &wf, (DWORD_PTR)p->done, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR)
    {
        lxLog("alsa: waveOutOpen %d Hz %d channels failed (%u)\n", p->rate, p->channels, r);
        CloseHandle(p->done);
        p->out = NULL;
        return -EIO;
    }
    for (int i = 0; i < p->periods; i++)
    {
        p->data[i] = malloc(p->periodFrames * frameBytes(p));
        p->hdr[i].lpData = (LPSTR)p->data[i];
        p->hdr[i].dwFlags = WHDR_DONE;
    }
    p->current = 0;
    p->fill = 0;
    lxLog("alsa: %d Hz, %d channels, %d periods of %llu frames\n", p->rate, p->channels, p->periods,
          (unsigned long long)p->periodFrames);
    return 0;
}

static SYSV size_t swSizeof(void) { return 256; }
static SYSV int swOk(void) { return 0; }

static SYSV int pcmNonblock(Pcm *p, int nonblock)
{
    p->nonblock = nonblock != 0;
    return 0;
}
static SYSV int pcmOk(Pcm *p) { (void)p; return 0; }

static SYSV lx_long pcmAvail(Pcm *p)
{
    lx_long free = 0;
    if (!p->out)
        return -EIO;
    for (int i = 0; i < p->periods; i++)
        free += (p->hdr[(p->current + i) % p->periods].dwFlags & WHDR_DONE) != 0;
    return free ? free * (lx_long)p->periodFrames - (lx_long)(p->fill / frameBytes(p)) : 0;
}

static SYSV lx_long pcmWritei(Pcm *p, const void *buf, lx_ulong frames)
{
    const uint8_t *src = buf;
    size_t left = frames * frameBytes(p), period = p->periodFrames * frameBytes(p);
    static uint64_t total, lastLog;
    if (!p->out)
        return -EIO;
    total += frames;
    if (GetTickCount64() - lastLog > 5000)
    {
        lastLog = GetTickCount64();
        lxTraceLog("alsa: %llu frames written\n", (unsigned long long)total);
    }
    while (left)
    {
        WAVEHDR *h = &p->hdr[p->current];
        size_t k;
        if (!(h->dwFlags & WHDR_DONE))
        {
            // A card's buffer full: a non-blocking write takes what fit.
            if (p->nonblock)
                break;
            WaitForSingleObject(p->done, 100);
            continue;
        }
        if (h->dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(p->out, h, sizeof(WAVEHDR));
        k = period - p->fill;
        if (k > left)
            k = left;
        memcpy(p->data[p->current] + p->fill, src, k);
        p->fill += k;
        src += k;
        left -= k;
        if (p->fill == period)
        {
            h->dwBufferLength = (DWORD)period;
            h->dwFlags = 0;
            waveOutPrepareHeader(p->out, h, sizeof(WAVEHDR));
            waveOutWrite(p->out, h, sizeof(WAVEHDR));
            p->current = (p->current + 1) % p->periods;
            p->fill = 0;
        }
    }
    if (left == frames * frameBytes(p))
        return -EAGAIN;
    return (lx_long)(frames - left / frameBytes(p));
}

static SYSV lx_long pcmReadi(Pcm *p, void *buf, lx_ulong frames)
{
    (void)p;
    (void)buf;
    (void)frames;
    return -EIO;
}

static const LxSym alsaSyms[] = {
    {"snd_asoundlib_version", alsaVersion}, {"snd_strerror", alsaStrerror},
    {"snd_device_name_hint", nameHint}, {"snd_device_name_free_hint", nameFreeHint},
    {"snd_device_name_get_hint", nameGetHint},
    {"snd_pcm_open", pcmOpen}, {"snd_pcm_close", pcmClose},
    {"snd_pcm_hw_params_sizeof", hwSizeof}, {"snd_pcm_hw_params_any", hwAny},
    {"snd_pcm_hw_params_set_access", hwSetAccess}, {"snd_pcm_hw_params_set_format", hwSetFormat},
    {"snd_pcm_hw_params_set_channels", hwSetChannels}, {"snd_pcm_hw_params_set_rate", hwSetRate},
    {"snd_pcm_hw_params_test_rate", hwTestRate}, {"snd_pcm_hw_params_set_buffer_size_near", hwSetBufferNear},
    {"snd_pcm_hw_params_set_period_size_near", hwSetPeriodNear}, {"snd_pcm_hw_params", hwParams},
    {"snd_pcm_sw_params_sizeof", swSizeof}, {"snd_pcm_sw_params_current", swOk},
    {"snd_pcm_sw_params_set_avail_min", swOk}, {"snd_pcm_sw_params_set_start_threshold", swOk},
    {"snd_pcm_sw_params", swOk},
    {"snd_pcm_nonblock", pcmNonblock}, {"snd_pcm_prepare", pcmOk}, {"snd_pcm_start", pcmOk},
    {"snd_pcm_resume", pcmOk}, {"snd_pcm_avail_update", pcmAvail}, {"snd_pcm_writei", pcmWritei},
    {"snd_pcm_readi", pcmReadi},
};

void lxAlsaInit(void)
{
    lxRegister(alsaSyms, LX_COUNT(alsaSyms));
}
