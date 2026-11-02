#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../../log/log.h"

#define SND_PCM_STREAM_PLAYBACK 0
#define SND_PCM_STREAM_CAPTURE 1

#define SND_PCM_FORMAT_S8 0
#define SND_PCM_FORMAT_U8 1
#define SND_PCM_FORMAT_S16_LE 2
#define SND_PCM_FORMAT_S16_BE 3
#define SND_PCM_FORMAT_U16_LE 4
#define SND_PCM_FORMAT_U16_BE 5
#define SND_PCM_FORMAT_FLOAT_LE 14

#define SND_PCM_ACCESS_RW_INTERLEAVED 3

#define SND_PCM_STATE_OPEN 0
#define SND_PCM_STATE_SETUP 1
#define SND_PCM_STATE_PREPARED 2
#define SND_PCM_STATE_RUNNING 3
#define SND_PCM_STATE_XRUN 4

typedef long snd_pcm_sframes_t;
typedef unsigned long snd_pcm_uframes_t;

struct _snd_pcm_hw_params
{
    int access;
    int format;
    int channels;
    unsigned int rate;
    unsigned int periods;
    snd_pcm_uframes_t buffer_size;
    snd_pcm_uframes_t period_size;
};

struct _snd_pcm_sw_params
{
    int dummy;
};

struct _snd_pcm
{
    SDL_AudioStream *stream;
    SDL_AudioSpec spec;
    int state;
    char name[64];
    snd_pcm_uframes_t buffer_size;
    int nonblock; // SND_PCM_NONBLOCK: writei answers -EAGAIN when full
};

typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_pcm_hw_params snd_pcm_hw_params_t;
typedef struct _snd_pcm_sw_params snd_pcm_sw_params_t;

static SDL_AudioDeviceID g_alsaDevice = 0;

static void EnsureAudioInit()
{
    if (!SDL_WasInit(SDL_INIT_AUDIO))
    {
        SDL_Init(SDL_INIT_AUDIO);
    }
    if (g_alsaDevice == 0)
    {
        g_alsaDevice = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
        if (g_alsaDevice == 0)
        {
            log_error("ALSA: Failed to open SDL audio device: %s", SDL_GetError());
        }
        else
        {
            log_info("ALSA: Opened SDL audio device ID %u", g_alsaDevice);
        }
    }
}

int shared_snd_pcm_open(snd_pcm_t **pcm, const char *name, int stream, int mode)
{
    log_info("snd_pcm_open('%s', stream=%d, mode=%d)", name, stream, mode);

    EnsureAudioInit();

    if (stream != SND_PCM_STREAM_PLAYBACK)
    {
        log_error("ALSA: Only playback streams supported");
        return -22; // -EINVAL
    }

    snd_pcm_t *handle = (snd_pcm_t *)calloc(1, sizeof(snd_pcm_t));
    strncpy(handle->name, name, 63);

    handle->spec.format = SDL_AUDIO_S16LE;
    handle->spec.channels = 2;
    handle->spec.freq = 44100;
    handle->buffer_size = 4096;

    handle->stream = SDL_CreateAudioStream(&handle->spec, NULL);
    if (!handle->stream)
    {
        log_error("ALSA: Failed to create audio stream: %s", SDL_GetError());
        free(handle);
        return -1;
    }

    if (g_alsaDevice)
    {
        SDL_BindAudioStream(g_alsaDevice, handle->stream);
    }

    handle->state = SND_PCM_STATE_OPEN;
    handle->nonblock = mode & 1; // SND_PCM_NONBLOCK

    if (pcm)
        *pcm = handle;
    return 0;
}

int shared_snd_pcm_close(snd_pcm_t *pcm)
{
    log_info("snd_pcm_close(%p)", pcm);
    if (pcm)
    {
        if (pcm->stream)
        {
            SDL_DestroyAudioStream(pcm->stream);
        }
        free(pcm);
    }
    return 0;
}

// HW Params
int shared_snd_pcm_hw_params_malloc(snd_pcm_hw_params_t **ptr)
{
    if (ptr)
        *ptr = (snd_pcm_hw_params_t *)calloc(1, sizeof(snd_pcm_hw_params_t));
    return 0;
}

void shared_snd_pcm_hw_params_free(snd_pcm_hw_params_t *obj)
{
    free(obj);
}

int shared_snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{

    if (params)
    {
        params->access = SND_PCM_ACCESS_RW_INTERLEAVED;
        params->format = SND_PCM_FORMAT_S16_LE;
        params->channels = 2;
        params->rate = 44100;
        params->periods = 4;
        params->buffer_size = 4096;
        params->period_size = 1024;
    }
    return 0;
}

int shared_snd_pcm_hw_params_set_access(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int access)
{
    if (params)
        params->access = access;
    return 0;
}

int shared_snd_pcm_hw_params_set_format(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int val)
{
    if (params)
        params->format = val;
    return 0;
}

int shared_snd_pcm_hw_params_set_channels(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int val)
{
    if (params)
        params->channels = val;
    return 0;
}

int shared_snd_pcm_hw_params_set_rate_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir)
{
    if (params && val)
        params->rate = *val;
    return 0;
}

int shared_snd_pcm_hw_params_set_periods(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val, int dir)
{
    if (params)
        params->periods = val;
    return 0;
}

int shared_snd_pcm_hw_params_set_buffer_size(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t val)
{
    if (params)
        params->buffer_size = val;
    return 0;
}

int shared_snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned long *val)
{
    if (params && val)
        params->buffer_size = *val;
    return 0;
}

int shared_snd_pcm_hw_params_set_period_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned long *val, int *dir)
{
    if (params && val)
        params->period_size = *val;
    return 0;
}

int shared_snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
    if (!pcm || !params)
        return -22; // EINVAL

    pcm->spec.freq = params->rate;
    pcm->spec.channels = params->channels;
    pcm->buffer_size = params->buffer_size;

    switch (params->format)
    {
        case SND_PCM_FORMAT_S8:
            pcm->spec.format = SDL_AUDIO_S8;
            break;
        case SND_PCM_FORMAT_U8:
            pcm->spec.format = SDL_AUDIO_U8;
            break;
        case SND_PCM_FORMAT_S16_LE:
            pcm->spec.format = SDL_AUDIO_S16LE;
            break;
        case SND_PCM_FORMAT_S16_BE:
            pcm->spec.format = SDL_AUDIO_S16BE;
            break;
        case SND_PCM_FORMAT_FLOAT_LE:
            pcm->spec.format = SDL_AUDIO_F32LE;
            break;
        default:
            log_warn("ALSA: Unknown format %d, defaulting to S16LE", params->format);
            pcm->spec.format = SDL_AUDIO_S16LE;
            break;
    }

    log_info("ALSA: Configured %s: %d Hz, %d channels, fmt=%d, buf=%lu", pcm->name, pcm->spec.freq, pcm->spec.channels, pcm->spec.format,
             pcm->buffer_size);

    if (pcm->stream)
    {
        SDL_SetAudioStreamFormat(pcm->stream, &pcm->spec, NULL);
    }

    pcm->state = SND_PCM_STATE_PREPARED;
    return 0;
}

// SW Params
int shared_snd_pcm_sw_params_malloc(snd_pcm_sw_params_t **ptr)
{
    if (ptr)
        *ptr = (snd_pcm_sw_params_t *)calloc(1, sizeof(snd_pcm_sw_params_t));
    return 0;
}
void shared_snd_pcm_sw_params_free(snd_pcm_sw_params_t *obj)
{
    free(obj);
}

int shared_snd_pcm_sw_params_current(snd_pcm_t *pcm, snd_pcm_sw_params_t *params)
{
    return 0;
}
int shared_snd_pcm_sw_params_set_avail_min(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val)
{
    return 0;
}
int shared_snd_pcm_sw_params_set_start_threshold(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val)
{
    return 0;
}
int shared_snd_pcm_sw_params_set_xfer_align(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val)
{
    return 0;
}

int shared_snd_pcm_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t *params)
{
    return 0;
}

// Operations
int shared_snd_pcm_prepare(snd_pcm_t *pcm)
{
    if (pcm)
    {
        if (pcm->stream)
            SDL_ClearAudioStream(pcm->stream);
        pcm->state = SND_PCM_STATE_PREPARED;
    }
    return 0;
}

int shared_snd_pcm_start(snd_pcm_t *pcm)
{
    if (pcm)
    {
        if (pcm->stream)
            SDL_ResumeAudioStreamDevice(pcm->stream);
        pcm->state = SND_PCM_STATE_RUNNING;
    }
    return 0;
}

int shared_snd_pcm_drop(snd_pcm_t *pcm)
{
    if (pcm)
    {
        if (pcm->stream)
            SDL_ClearAudioStream(pcm->stream);
        pcm->state = SND_PCM_STATE_SETUP;
    }
    return 0;
}

int shared_snd_pcm_resume(snd_pcm_t *pcm)
{
    return shared_snd_pcm_start(pcm);
}

int shared_snd_pcm_drain(snd_pcm_t *pcm)
{
    if (pcm && pcm->stream)
    {
        SDL_FlushAudioStream(pcm->stream);
    }
    return 0;
}

int shared_snd_pcm_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *delayp)
{
    if (pcm && delayp)
    {

        if (pcm->stream)
        {
            int queued_bytes = SDL_GetAudioStreamQueued(pcm->stream);
            int frame_size = pcm->spec.channels * SDL_AUDIO_BYTESIZE(pcm->spec.format);
            if (frame_size > 0)
            {
                *delayp = queued_bytes / frame_size;
            }
            else
            {
                *delayp = 0;
            }
        }
        else
        {
            *delayp = 0;
        }
    }
    return 0;
}

// The frames the device buffer has room for (buffer_size, as configured,
// less what the SDL stream still holds).
static long pcmRoom(snd_pcm_t *pcm, int frame_size)
{
    long queued = SDL_GetAudioStreamQueued(pcm->stream) / frame_size;
    long room = (long)pcm->buffer_size - queued;
    return room > 0 ? room : 0;
}

// As ALSA's: non-blocking, at most the room left, -EAGAIN when there is
// none; blocking, every frame, waiting for room (ALSA returns short only on
// an error: FMOD mixes its next period as soon as writei returns, so a short
// write dropped the rest of each period). The SDL stream would queue
// without limit (FMOD writes whatever it is let to); a device that drains
// nothing for half a second loses its queue rather than hanging the game.
snd_pcm_sframes_t shared_snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, unsigned long size)
{
    if (!pcm || !pcm->stream)
        return -32; // -EPIPE

    int frame_size = pcm->spec.channels * SDL_AUDIO_BYTESIZE(pcm->spec.format);
    if (frame_size <= 0)
        return -22; // EINVAL

    const uint8_t *data = (const uint8_t *)buffer;
    unsigned long written = 0;
    int waited = 0;
    while (written < size)
    {
        long room = pcmRoom(pcm, frame_size);
        if (room == 0)
        {
            if (pcm->nonblock)
                return written ? (snd_pcm_sframes_t)written : -11; // -EAGAIN
            if (++waited > 500)
            {
                SDL_ClearAudioStream(pcm->stream);
                waited = 0;
                continue;
            }
            SDL_Delay(1);
            continue;
        }
        waited = 0;
        unsigned long n = size - written < (unsigned long)room ? size - written : (unsigned long)room;
        if (!SDL_PutAudioStreamData(pcm->stream, data + written * frame_size, (int)(n * frame_size)))
            return written ? (snd_pcm_sframes_t)written : -32;
        written += n;
    }
    return (snd_pcm_sframes_t)written;
}

int shared_snd_pcm_avail_update(snd_pcm_t *pcm)
{
    if (!pcm || !pcm->stream)
        return 4096;

    int frame_size = pcm->spec.channels * SDL_AUDIO_BYTESIZE(pcm->spec.format);
    if (frame_size <= 0)
        return 4096;

    int queued_bytes = SDL_GetAudioStreamQueued(pcm->stream);
    int queued_frames = queued_bytes / frame_size;

    int avail = (int)pcm->buffer_size - queued_frames;
    if (avail < 0)
        avail = 0;

    return avail;
}

int shared_snd_pcm_state(snd_pcm_t *pcm)
{
    if (pcm)
        return pcm->state;
    return SND_PCM_STATE_OPEN;
}

int shared_snd_pcm_format_width(int format)
{
    switch (format)
    {
        case SND_PCM_FORMAT_S8:
            return 8;
        case SND_PCM_FORMAT_U8:
            return 8;
        case SND_PCM_FORMAT_S16_LE:
            return 16;
        case SND_PCM_FORMAT_S16_BE:
            return 16;
        case SND_PCM_FORMAT_FLOAT_LE:
            return 32;
    }
    return 16;
}

const char *shared_snd_strerror(int errnum)
{
    return "ALSA Error";
}

// ------------------------------------------------------------------
// The rest of the ALSA API the g6 games (Angry Birds, The Walking
// Dead) import.

int shared_snd_pcm_hw_params_copy(snd_pcm_hw_params_t *dst, const snd_pcm_hw_params_t *src)
{
    if (dst && src)
        *dst = *src;
    return 0;
}

int shared_snd_pcm_hw_params_get_buffer_size(snd_pcm_hw_params_t *p, snd_pcm_uframes_t *value)
{
    if (p && value)
        *value = p->buffer_size;
    return 0;
}

int shared_snd_pcm_hw_params_get_channels(snd_pcm_hw_params_t *p, unsigned int *value)
{
    if (p && value)
        *value = p->channels;
    return 0;
}

int shared_snd_pcm_hw_params_get_periods(snd_pcm_hw_params_t *p, unsigned int *value, int *value_max)
{
    if (p && value)
        *value = p->periods;
    if (value_max)
        *value_max = 0; // exact value
    return 0;
}

int shared_snd_pcm_hw_params_get_period_size(snd_pcm_hw_params_t *p, snd_pcm_uframes_t *value, snd_pcm_uframes_t *value_max)
{
    if (p && value)
        *value = p->period_size;
    if (value_max)
        *value_max = 0; // exact value
    return 0;
}

int shared_snd_pcm_hw_params_set_periods_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *p, unsigned int *value, int *dir)
{
    (void)pcm;
    (void)dir;
    if (p && value)
        p->periods = *value;
    return 0;
}

unsigned long shared_snd_pcm_hw_params_sizeof(void)
{
    return sizeof(snd_pcm_hw_params_t);
}

unsigned long shared_snd_pcm_sw_params_sizeof(void)
{
    return sizeof(snd_pcm_sw_params_t);
}

int shared_snd_pcm_nonblock(snd_pcm_t *pcm, int nonblock)
{
    if (pcm)
        pcm->nonblock = nonblock != 0;
    return 0;
}

int shared_snd_pcm_recover(snd_pcm_t *pcm, int err, int silent)
{
    (void)pcm;
    (void)err;
    (void)silent;
    // No xruns here (the SDL stream just queues); nothing to recover.
    return 0;
}

// ------------------------------------------------------------------
// FMOD's ALSA output (the Raw Thrills g5 games, libfmod.so.7): the device
// list and the exact rate setters.

const char *shared_snd_asoundlib_version(void)
{
    return "1.1.3";
}

// One device, "default", for both directions (no IOID).
static char alsaDefaultHint[] = "default";

// Memory the game frees: its allocator's (the elfLoader's malloc is
// _aligned_malloc, see MemoryManager::customMalloc).
static void *guestCalloc(size_t size)
{
#ifdef _WIN32
    void *p = _aligned_malloc(size, 16);
#else
    void *p = malloc(size);
#endif
    if (p)
        memset(p, 0, size);
    return p;
}

static void guestFree(void *p)
{
#ifdef _WIN32
    _aligned_free(p);
#else
    free(p);
#endif
}

static char *guestStrdup(const char *s)
{
    char *p = guestCalloc(strlen(s) + 1);
    if (p)
        strcpy(p, s);
    return p;
}

int shared_snd_device_name_hint(int card, const char *iface, void ***hints)
{
    (void)card;
    (void)iface;
    void **list = (void **)guestCalloc(2 * sizeof(void *));
    if (!hints || !list)
    {
        guestFree(list);
        return -12; // -ENOMEM
    }
    list[0] = alsaDefaultHint;
    *hints = list;
    return 0;
}

// Allocated: the caller frees it (its free()).
char *shared_snd_device_name_get_hint(const void *hint, const char *id)
{
    if (hint != alsaDefaultHint || !id)
        return NULL;
    if (!strcmp(id, "NAME"))
        return guestStrdup("default");
    if (!strcmp(id, "DESC"))
        return guestStrdup("Default Audio Device");
    return NULL;
}

int shared_snd_device_name_free_hint(void **hints)
{
    guestFree(hints);
    return 0;
}

int shared_snd_pcm_hw_params_set_rate(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val, int dir)
{
    (void)pcm;
    (void)dir;
    if (params)
        params->rate = val;
    return 0;
}

// Every rate: the SDL stream resamples.
int shared_snd_pcm_hw_params_test_rate(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val, int dir)
{
    (void)pcm;
    (void)params;
    (void)val;
    (void)dir;
    return 0;
}

// No capture (snd_pcm_open refuses it).
snd_pcm_sframes_t shared_snd_pcm_readi(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t size)
{
    (void)pcm;
    (void)buffer;
    (void)size;
    return -5; // -EIO
}

// ------------------------------------------------------------------
// PulseAudio: the g6 games link a Pulse client alongside ALSA (on the
// cabinet Pulse wins). On Windows there is no Pulse, so the calls are
// routed to the same SDL audio stream: contexts and streams are
// reported ready at once, and the samples go straight into the SDL
// queue.

struct _pa_stream
{
    snd_pcm_t *pcm;
};

struct _pa_simple
{
    snd_pcm_t *pcm;
};

static int PaOpenStream(snd_pcm_t **pcm)
{
    return shared_snd_pcm_open(pcm, "pulse", SND_PCM_STREAM_PLAYBACK, 0);
}

int shared_pa_channel_map_init_auto(void *map, unsigned int channels, const char *map_name)
{
    (void)map_name;
    if (map)
    {
        ((unsigned char *)map)[0] = (unsigned char)channels;
        ((unsigned char *)map)[1] = 0;
    }
    return 0;
}

void *shared_pa_mainloop_new(void)
{
    return calloc(1, 8);
}

void *shared_pa_mainloop_get_api(void *mainloop)
{
    (void)mainloop;
    return calloc(1, 8);
}

void shared_pa_mainloop_free(void *mainloop)
{
    free(mainloop);
}

int shared_pa_mainloop_iterate(void *mainloop, int block, int *time)
{
    (void)mainloop;
    (void)time;
    // Nothing to pump: the samples go straight into the SDL queue.
    SDL_Delay(block ? 10 : 1);
    return 0;
}

void *shared_pa_context_new(void *spawncb, const char *name, void *map)
{
    (void)spawncb;
    (void)name;
    (void)map;
    return calloc(1, 8);
}

int shared_pa_context_set_name(void *context, const char *name)
{
    (void)context;
    (void)name;
    return 0;
}

int shared_pa_context_connect(void *context, const char *server, int flags, void *auth)
{
    (void)context;
    (void)server;
    (void)flags;
    (void)auth;
    return 0;
}

int shared_pa_context_get_state(void *context)
{
    (void)context;
    return 4; // PA_CONTEXT_READY
}

int shared_pa_context_disconnect(void *context)
{
    (void)context;
    return 0;
}

int shared_pa_context_unref(void *context)
{
    int r = context ? 1 : 0;
    free(context);
    return r;
}

void *shared_pa_stream_new(void *context, const char *name, void *ss, void *map)
{
    (void)context;
    (void)name;
    (void)ss;
    (void)map;
    snd_pcm_t *pcm;
    if (PaOpenStream(&pcm) != 0)
        return NULL;
    struct _pa_stream *s = (struct _pa_stream *)calloc(1, sizeof(struct _pa_stream));
    s->pcm = pcm;
    return s;
}

// pa_stream_connect_playback(stream, device, buffer_attr, initial_bytes,
// format, error_code): no audio here, the data flows through pa_stream_write.
int shared_pa_stream_connect_playback(void *stream, const char *device, const void *buffer_attr, long long initial_bytes, int format, int *error_code)
{
    (void)stream;
    (void)device;
    (void)buffer_attr;
    (void)initial_bytes;
    (void)format;
    (void)error_code;
    return 0;
}

// pa_stream_write: 0 on success (not the frame count), -1 on error.
int shared_pa_stream_write(void *stream, const void *data, unsigned long size, long long offset, int eol, void *writer)
{
    (void)offset;
    (void)eol;
    (void)writer;
    struct _pa_stream *s = (struct _pa_stream *)stream;
    if (!s || !s->pcm)
        return -1;
    int frame_size = s->pcm->spec.channels * 2; // S16
    if (frame_size <= 0)
        return -1;
    return shared_snd_pcm_writei(s->pcm, data, size / frame_size) >= 0 ? 0 : -1;
}

int shared_pa_stream_drain(void *stream, long long usec, void *userdata)
{
    (void)usec;
    (void)userdata;
    struct _pa_stream *s = (struct _pa_stream *)stream;
    if (s && s->pcm)
        shared_snd_pcm_drain(s->pcm);
    return 0;
}

int shared_pa_stream_get_state(void *stream)
{
    (void)stream;
    return 2; // PA_STREAM_READY
}

int shared_pa_stream_disconnect(void *stream)
{
    (void)stream;
    return 0;
}

unsigned long shared_pa_stream_writable_size(void *stream)
{
    (void)stream;
    return 4096;
}

int shared_pa_stream_unref(void *stream)
{
    struct _pa_stream *s = (struct _pa_stream *)stream;
    int r = s ? 1 : 0;
    if (s && s->pcm)
        shared_snd_pcm_close(s->pcm);
    free(s);
    return r;
}

void *shared_pa_simple_new(void *context, const char *name, int direction, void *device, const char *stream_name, void *ss, void *map, void *attr, long long *usec)
{
    (void)context;
    (void)name;
    (void)direction;
    (void)device;
    (void)stream_name;
    (void)ss;
    (void)map;
    (void)attr;
    (void)usec;
    snd_pcm_t *pcm;
    if (PaOpenStream(&pcm) != 0)
        return NULL;
    struct _pa_simple *s = (struct _pa_simple *)calloc(1, sizeof(struct _pa_simple));
    s->pcm = pcm;
    return s;
}

int shared_pa_simple_free(void *simple)
{
    struct _pa_simple *s = (struct _pa_simple *)simple;
    int r = s ? 1 : 0;
    if (s && s->pcm)
        shared_snd_pcm_close(s->pcm);
    free(s);
    return r;
}

int shared_pa_operation_cancel(void *op)
{
    (void)op;
    return 1;
}

int shared_pa_operation_get_state(void *op)
{
    (void)op;
    // PA_OPERATION_DONE (1; 0 is RUNNING, 2 CANCELLED): the game polls
    // until it sees it, and any other state would keep the wait open.
    return 1;
}

int shared_pa_operation_unref(void *op)
{
    (void)op;
    return 1;
}
