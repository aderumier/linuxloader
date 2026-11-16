// OSS /dev/dsp on waveOut (see ll64.h), for SDL 2's dsp audio driver: the
// format the ioctls set, written blocking as a sound card takes it.
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include "ll64.h"

#define DSP_FD 300000
#define BUFFERS 4

enum
{
    SNDCTL_DSP_RESET = 0x5000, SNDCTL_DSP_SYNC = 0x5001, SNDCTL_DSP_SPEED = 0xc0045002,
    SNDCTL_DSP_SETFMT = 0xc0045005, SNDCTL_DSP_CHANNELS = 0xc0045006, SNDCTL_DSP_SETFRAGMENT = 0xc004500a,
    SNDCTL_DSP_GETFMTS = 0x8004500b, SNDCTL_DSP_GETOSPACE = 0x8010500c, SNDCTL_DSP_STEREO = 0xc0045003,
    AFMT_U8 = 0x8, AFMT_S16_LE = 0x10,
};

static struct
{
    int open, format, channels, rate, fragSize;
    HWAVEOUT out;
    HANDLE done;
    WAVEHDR hdr[BUFFERS];
    uint8_t *data[BUFFERS];
    size_t fill;   // bytes in the buffer being filled
    int current;
} dsp;

int lxDspIsPath(const char *path)
{
    return path && (!strcmp(path, "/dev/dsp") || !strcmp(path, "/dev/sound/dsp") || !strcmp(path, "/dev/audio"));
}

int lxDspIsFd(int fd) { return fd == DSP_FD; }

int lxDspOpen(void)
{
    if (dsp.open)
    {
        lxSetErrno(16);   // EBUSY
        return -1;
    }
    memset(&dsp, 0, sizeof(dsp));
    dsp.open = 1;
    dsp.format = AFMT_U8;
    dsp.channels = 1;
    dsp.rate = 8000;
    dsp.fragSize = 4096;
    return DSP_FD;
}

static void stop(void)
{
    if (!dsp.out)
        return;
    waveOutReset(dsp.out);
    for (int i = 0; i < BUFFERS; i++)
    {
        if (dsp.hdr[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(dsp.out, &dsp.hdr[i], sizeof(WAVEHDR));
        free(dsp.data[i]);
        dsp.data[i] = NULL;
    }
    waveOutClose(dsp.out);
    CloseHandle(dsp.done);
    dsp.out = NULL;
}

int lxDspClose(void)
{
    stop();
    dsp.open = 0;
    return 0;
}

static int start(void)
{
    WAVEFORMATEX wf = {WAVE_FORMAT_PCM};
    MMRESULT r;
    wf.nChannels = (WORD)dsp.channels;
    wf.nSamplesPerSec = (DWORD)dsp.rate;
    wf.wBitsPerSample = dsp.format == AFMT_S16_LE ? 16 : 8;
    wf.nBlockAlign = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    dsp.done = CreateEventW(NULL, FALSE, FALSE, NULL);
    r = waveOutOpen(&dsp.out, WAVE_MAPPER, &wf, (DWORD_PTR)dsp.done, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR)
    {
        lxLog("dsp: waveOutOpen %d Hz %d channels %d bits failed (%u)\n", dsp.rate, dsp.channels, wf.wBitsPerSample, r);
        dsp.out = NULL;
        return 0;
    }
    for (int i = 0; i < BUFFERS; i++)
    {
        dsp.data[i] = malloc((size_t)dsp.fragSize);
        dsp.hdr[i].lpData = (LPSTR)dsp.data[i];
        dsp.hdr[i].dwBufferLength = (DWORD)dsp.fragSize;
        dsp.hdr[i].dwFlags = WHDR_DONE;
    }
    dsp.current = 0;
    dsp.fill = 0;
    lxLog("dsp: %d Hz, %d channels, %d bits, fragments of %d bytes\n", dsp.rate, dsp.channels, wf.wBitsPerSample,
          dsp.fragSize);
    return 1;
}

int64_t lxDspWrite(const void *buf, size_t n)
{
    const uint8_t *p = buf;
    size_t left = n;
    static uint64_t total, lastLog;
    total += n;
    if (GetTickCount64() - lastLog > 5000)
    {
        lastLog = GetTickCount64();
        lxTraceLog("dsp: %llu bytes written\n", (unsigned long long)total);
    }
    if (!dsp.out && !start())
        return (int64_t)n;   // no output: dropped
    while (left)
    {
        WAVEHDR *h = &dsp.hdr[dsp.current];
        size_t k;
        // The buffer must be back from the card: the write blocks as a
        // card's does.
        while (!(h->dwFlags & WHDR_DONE))
            WaitForSingleObject(dsp.done, 100);
        if (h->dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(dsp.out, h, sizeof(WAVEHDR));
        k = (size_t)dsp.fragSize - dsp.fill;
        if (k > left)
            k = left;
        memcpy(dsp.data[dsp.current] + dsp.fill, p, k);
        dsp.fill += k;
        p += k;
        left -= k;
        if (dsp.fill == (size_t)dsp.fragSize)
        {
            h->dwBufferLength = (DWORD)dsp.fill;
            h->dwFlags = 0;
            waveOutPrepareHeader(dsp.out, h, sizeof(WAVEHDR));
            waveOutWrite(dsp.out, h, sizeof(WAVEHDR));
            dsp.current = (dsp.current + 1) % BUFFERS;
            dsp.fill = 0;
        }
    }
    return (int64_t)n;
}

int lxDspIoctl(unsigned req, void *arg)
{
    int *v = arg;
    switch (req)
    {
    case SNDCTL_DSP_GETFMTS:
        *v = AFMT_U8 | AFMT_S16_LE;
        return 0;
    case SNDCTL_DSP_SETFMT:
        if (*v != AFMT_U8)
            *v = AFMT_S16_LE;
        dsp.format = *v;
        return 0;
    case SNDCTL_DSP_CHANNELS:
        if (*v < 1)
            *v = 1;
        if (*v > 2)
            *v = 2;
        dsp.channels = *v;
        return 0;
    case SNDCTL_DSP_STEREO:
        dsp.channels = *v ? 2 : 1;
        return 0;
    case SNDCTL_DSP_SPEED:
        dsp.rate = *v;
        return 0;
    case SNDCTL_DSP_SETFRAGMENT:
    {
        int shift = *v & 0xffff;
        if (shift < 8)
            shift = 8;
        if (shift > 16)
            shift = 16;
        dsp.fragSize = 1 << shift;
        return 0;
    }
    case SNDCTL_DSP_GETOSPACE:
    {
        int free = 0;
        for (int i = 0; i < BUFFERS; i++)
            free += (dsp.hdr[i].dwFlags & WHDR_DONE) || !dsp.out;
        v[0] = free;         // fragments
        v[1] = BUFFERS;      // fragstotal
        v[2] = dsp.fragSize; // fragsize
        v[3] = free * dsp.fragSize;
        return 0;
    }
    case SNDCTL_DSP_RESET:
    case SNDCTL_DSP_SYNC:
        return 0;
    }
    lxLog("dsp: ioctl %#x not handled\n", req);
    lxSetErrno(22);
    return -1;
}
