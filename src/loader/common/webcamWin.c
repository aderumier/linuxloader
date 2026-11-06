// The webcams on Windows, which has no /dev/video: the games' V4L2 calls (open,
// the streaming ioctls, mmap), answered from SDL's cameras. As Cruis'n Blast's
// player photos ask for them: a YUYV picture, read non-blocking. The controls
// (brightness...) are a webcam's that has none: the games go on without.

#ifdef _WIN32

#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include <SDL3/SDL.h>

#include "webcam.h"
#include "../log/log.h"
#include "../rawthrills/rawthrills.h"

#define VIDEO_PREFIX "/dev/video"
#define MAX_HANDLES 16
#define MAX_BUFFERS 8
#define BUFFER_ALIGN 4096
#define CAMERA_RATE 30

// The i386 V4L2 requests and structures.
#define VIDIOC_QUERYCAP 0x80685600u
#define VIDIOC_G_FMT 0xc0cc5604u
#define VIDIOC_S_FMT 0xc0cc5605u
#define VIDIOC_TRY_FMT 0xc0cc5640u
#define VIDIOC_REQBUFS 0xc0145608u
#define VIDIOC_QUERYBUF 0xc0445609u
#define VIDIOC_QBUF 0xc044560fu
#define VIDIOC_DQBUF 0xc0445611u
#define VIDIOC_STREAMON 0x40045612u
#define VIDIOC_STREAMOFF 0x40045613u

#define CAP_VIDEO_CAPTURE 0x00000001u
#define CAP_STREAMING 0x04000000u
#define CAP_DEVICE_CAPS 0x80000000u
#define BUF_TYPE_VIDEO_CAPTURE 1u
#define MEMORY_MMAP 1u
#define FIELD_NONE 1u
#define PIX_FMT_YUYV 0x56595559u
#define COLORSPACE_SRGB 8u
#define BUF_FLAG_MAPPED 0x1u
#define BUF_FLAG_QUEUED 0x2u
#define BUF_FLAG_DONE 0x4u

typedef struct
{
    char driver[16], card[32], busInfo[32];
    uint32_t version, capabilities, deviceCaps, reserved[3];
} V4l2Capability;

typedef struct
{
    uint32_t type, width, height, pixelFormat, field, bytesPerLine, sizeImage, colorspace;
    uint8_t rest[204 - 32];
} V4l2Format;

typedef struct
{
    uint32_t count, type, memory, reserved[2];
} V4l2RequestBuffers;

typedef struct
{
    uint32_t index, type, bytesUsed, flags, field;
    int32_t seconds, microseconds;
    uint32_t timecode[4];
    uint32_t sequence, memory, offset, length, reserved2, reserved;
} V4l2Buffer;

_Static_assert(sizeof(V4l2Capability) == 104 && sizeof(V4l2Format) == 204 && sizeof(V4l2RequestBuffers) == 20 &&
                   sizeof(V4l2Buffer) == 68,
               "the i386 V4L2 structures");

typedef struct
{
    int fd; // 0: free.
    SDL_CameraID camera;
    uint32_t width, height, bytesPerLine, sizeImage;
    uint32_t bufferCount, sequence;
    uint8_t *buffers[MAX_BUFFERS];
    int queued[MAX_BUFFERS];
    SDL_Camera *stream;
} Handle;

static Handle handles[MAX_HANDLES];
static CRITICAL_SECTION lock;
static INIT_ONCE lockOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK initLock(PINIT_ONCE once, PVOID param, PVOID *context)
{
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&lock);
    return TRUE;
}

static void enter(void)
{
    InitOnceExecuteOnce(&lockOnce, initLock, NULL, NULL);
    EnterCriticalSection(&lock);
}

static void leave(void)
{
    LeaveCriticalSection(&lock);
}

// The n-th of SDL's cameras: 0 if none. The games ask thousands of times a
// second if a camera is there: the list is read once a second.
static SDL_CameraID cameraAt(int n)
{
    static SDL_CameraID *ids;
    static int count;
    static Uint64 lastRead;
    static int started;
    Uint64 now = SDL_GetTicks();

    if (!started)
    {
        started = SDL_InitSubSystem(SDL_INIT_CAMERA) ? 1 : -1;
        if (started < 0)
            log_warn("Webcam: no SDL cameras: %s", SDL_GetError());
    }
    if (started < 0)
        return 0;
    if (!lastRead || now - lastRead >= 1000)
    {
        SDL_free(ids);
        ids = SDL_GetCameras(&count);
        lastRead = now;
    }
    return ids && n >= 0 && n < count ? ids[n] : 0;
}

int webcamIsPath(const char *path)
{
    const char *n = path + sizeof(VIDEO_PREFIX) - 1;

    if (!isRawThrillsGame() || strncmp(path, VIDEO_PREFIX, sizeof(VIDEO_PREFIX) - 1) || !*n)
        return 0;
    for (; *n; n++)
        if (*n < '0' || *n > '9')
            return 0;
    return 1;
}

int webcamOpen(const char *path, int flags, int mode, int (*realOpen)(const char *, int, ...))
{
    int fd = -1;

    (void)flags;
    (void)mode;
    enter();
    SDL_CameraID camera = cameraAt(atoi(path + sizeof(VIDEO_PREFIX) - 1));
    if (!camera)
        errno = ENOENT;
    else
    {
        for (int i = 0; i < MAX_HANDLES; i++)
            if (!handles[i].fd)
            {
                fd = realOpen("nul", O_RDWR);
                if (fd >= 0)
                {
                    memset(&handles[i], 0, sizeof(handles[i]));
                    handles[i].fd = fd;
                    handles[i].camera = camera;
                }
                break;
            }
        if (fd < 0 && errno != ENOENT)
            errno = EMFILE;
    }
    leave();
    return fd;
}

static Handle *handleOf(int fd)
{
    for (int i = 0; i < MAX_HANDLES; i++)
        if (fd >= 0 && handles[i].fd == fd)
            return &handles[i];
    return NULL;
}

int webcamIsFd(int fd)
{
    enter();
    int is = handleOf(fd) != NULL;
    leave();
    return is;
}

static void stopStream(Handle *h)
{
    if (h->stream)
    {
        SDL_CloseCamera(h->stream);
        h->stream = NULL;
        log_info("Webcam: stream stopped");
    }
    memset(h->queued, 0, sizeof(h->queued));
}

static void freeBuffers(Handle *h)
{
    for (uint32_t i = 0; i < MAX_BUFFERS; i++)
        if (h->buffers[i])
        {
            VirtualFree(h->buffers[i], 0, MEM_RELEASE);
            h->buffers[i] = NULL;
        }
    h->bufferCount = 0;
}

void webcamClose(int fd)
{
    enter();
    Handle *h = handleOf(fd);
    if (h)
    {
        stopStream(h);
        freeBuffers(h);
        h->fd = 0;
    }
    leave();
}

static uint32_t bufferStride(const Handle *h)
{
    return (h->sizeImage + BUFFER_ALIGN - 1) & ~(uint32_t)(BUFFER_ALIGN - 1);
}

void *webcamMmap(int fd, unsigned int offset)
{
    void *mapped = NULL;

    enter();
    Handle *h = handleOf(fd);
    if (h && h->sizeImage && offset / bufferStride(h) < h->bufferCount)
        mapped = h->buffers[offset / bufferStride(h)];
    leave();
    return mapped;
}

int webcamIsMapped(const void *addr)
{
    int mapped = 0;

    enter();
    for (int i = 0; i < MAX_HANDLES && !mapped; i++)
        for (int b = 0; handles[i].fd && b < MAX_BUFFERS; b++)
            mapped |= addr && handles[i].buffers[b] == addr;
    leave();
    return mapped;
}

static void setFormat(Handle *h, V4l2Format *format, int store)
{
    // Even widths: YUYV pairs its pixels.
    uint32_t width = format->width ? format->width & ~1u : 640, height = format->height ? format->height : 480;

    format->width = width;
    format->height = height;
    format->pixelFormat = PIX_FMT_YUYV;
    format->field = FIELD_NONE;
    format->bytesPerLine = width * 2;
    format->sizeImage = width * height * 2;
    format->colorspace = COLORSPACE_SRGB;
    if (store)
    {
        h->width = width;
        h->height = height;
        h->bytesPerLine = format->bytesPerLine;
        h->sizeImage = format->sizeImage;
    }
}

// A frame in the buffer, line by line: SDL has it at the size and format asked,
// unless the camera could not be converted.
static int copyFrame(Handle *h, SDL_Surface *frame, uint8_t *dst)
{
    SDL_Surface *converted = NULL;

    if (frame->format != SDL_PIXELFORMAT_YUY2)
    {
        converted = SDL_ConvertSurface(frame, SDL_PIXELFORMAT_YUY2);
        if (!converted)
            return 0;
        frame = converted;
    }
    uint32_t bytes = (uint32_t)frame->w * 2 < h->bytesPerLine ? (uint32_t)frame->w * 2 : h->bytesPerLine;
    uint32_t lines = (uint32_t)frame->h < h->height ? (uint32_t)frame->h : h->height;
    for (uint32_t y = 0; y < lines; y++)
        memcpy(dst + y * h->bytesPerLine, (const uint8_t *)frame->pixels + y * (uint32_t)frame->pitch, bytes);
    if (converted)
        SDL_DestroySurface(converted);
    return 1;
}

static int fail(int error)
{
    errno = error;
    return -1;
}

static int ioctlLocked(Handle *h, unsigned long request, void *arg)
{
    switch ((uint32_t)request)
    {
    case VIDIOC_QUERYCAP:
    {
        V4l2Capability *cap = arg;
        const char *name = SDL_GetCameraName(h->camera);
        memset(cap, 0, sizeof(*cap));
        snprintf(cap->driver, sizeof(cap->driver), "uvcvideo");
        snprintf(cap->card, sizeof(cap->card), "%s", name ? name : "Webcam");
        snprintf(cap->busInfo, sizeof(cap->busInfo), "usb");
        cap->version = 0x00050000;
        cap->deviceCaps = CAP_VIDEO_CAPTURE | CAP_STREAMING;
        cap->capabilities = cap->deviceCaps | CAP_DEVICE_CAPS;
        return 0;
    }
    case VIDIOC_G_FMT:
    case VIDIOC_S_FMT:
    case VIDIOC_TRY_FMT:
    {
        V4l2Format *format = arg;
        if (format->type != BUF_TYPE_VIDEO_CAPTURE)
            return fail(EINVAL);
        if ((uint32_t)request == VIDIOC_G_FMT)
        {
            format->width = h->width;
            format->height = h->height;
        }
        setFormat(h, format, (uint32_t)request == VIDIOC_S_FMT);
        return 0;
    }
    case VIDIOC_REQBUFS:
    {
        V4l2RequestBuffers *req = arg;
        if (req->type != BUF_TYPE_VIDEO_CAPTURE || req->memory != MEMORY_MMAP || !h->sizeImage)
            return fail(EINVAL);
        stopStream(h);
        freeBuffers(h);
        if (req->count > MAX_BUFFERS)
            req->count = MAX_BUFFERS;
        for (uint32_t i = 0; i < req->count; i++)
        {
            h->buffers[i] = VirtualAlloc(NULL, bufferStride(h), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!h->buffers[i])
            {
                freeBuffers(h);
                return fail(ENOMEM);
            }
        }
        h->bufferCount = req->count;
        return 0;
    }
    case VIDIOC_QUERYBUF:
    {
        V4l2Buffer *buffer = arg;
        if (buffer->type != BUF_TYPE_VIDEO_CAPTURE || buffer->index >= h->bufferCount)
            return fail(EINVAL);
        buffer->memory = MEMORY_MMAP;
        buffer->offset = buffer->index * bufferStride(h);
        buffer->length = h->sizeImage;
        buffer->field = FIELD_NONE;
        buffer->bytesUsed = 0;
        buffer->flags = BUF_FLAG_MAPPED | (h->queued[buffer->index] ? BUF_FLAG_QUEUED : 0);
        return 0;
    }
    case VIDIOC_QBUF:
    {
        V4l2Buffer *buffer = arg;
        if (buffer->type != BUF_TYPE_VIDEO_CAPTURE || buffer->index >= h->bufferCount)
            return fail(EINVAL);
        h->queued[buffer->index] = 1;
        buffer->flags = BUF_FLAG_MAPPED | BUF_FLAG_QUEUED;
        return 0;
    }
    case VIDIOC_STREAMON:
    {
        SDL_CameraSpec spec;
        if (h->stream)
            return 0;
        if (!h->sizeImage || !h->bufferCount)
            return fail(EINVAL);
        memset(&spec, 0, sizeof(spec));
        spec.format = SDL_PIXELFORMAT_YUY2;
        spec.colorspace = SDL_COLORSPACE_UNKNOWN;
        spec.width = (int)h->width;
        spec.height = (int)h->height;
        spec.framerate_numerator = CAMERA_RATE;
        spec.framerate_denominator = 1;
        h->stream = SDL_OpenCamera(h->camera, &spec);
        if (!h->stream)
        {
            log_warn("Webcam: cannot open %s: %s", SDL_GetCameraName(h->camera), SDL_GetError());
            return fail(EIO);
        }
        h->sequence = 0;
        log_info("Webcam: streaming %s at %ux%u", SDL_GetCameraName(h->camera), h->width, h->height);
        return 0;
    }
    case VIDIOC_STREAMOFF:
        stopStream(h);
        return 0;
    case VIDIOC_DQBUF:
    {
        V4l2Buffer *buffer = arg;
        Uint64 timestamp = 0;
        uint32_t index = 0;
        if (!h->stream)
            return fail(EINVAL);
        while (index < h->bufferCount && !h->queued[index])
            index++;
        if (index == h->bufferCount)
            return fail(EAGAIN);
        SDL_Surface *frame = SDL_AcquireCameraFrame(h->stream, &timestamp);
        if (!frame)
            return fail(EAGAIN);
        int copied = copyFrame(h, frame, h->buffers[index]);
        SDL_ReleaseCameraFrame(h->stream, frame);
        if (!copied)
            return fail(EAGAIN);
        h->queued[index] = 0;
        memset(buffer, 0, sizeof(*buffer));
        buffer->index = index;
        buffer->type = BUF_TYPE_VIDEO_CAPTURE;
        buffer->bytesUsed = h->sizeImage;
        buffer->flags = BUF_FLAG_MAPPED | BUF_FLAG_DONE;
        buffer->field = FIELD_NONE;
        buffer->seconds = (int32_t)(timestamp / 1000000000u);
        buffer->microseconds = (int32_t)(timestamp % 1000000000u / 1000u);
        buffer->sequence = h->sequence++;
        buffer->memory = MEMORY_MMAP;
        buffer->offset = index * bufferStride(h);
        buffer->length = h->sizeImage;
        return 0;
    }
    default:
        return fail(EINVAL);
    }
}

int webcamIoctl(int fd, unsigned long request, void *arg)
{
    enter();
    Handle *h = handleOf(fd);
    int result = h ? ioctlLocked(h, request, arg) : fail(EBADF);
    leave();
    return result;
}

#endif // _WIN32
