#ifndef WEBCAM_H
#define WEBCAM_H

// Webcams (/dev/videoN) of the Raw Thrills and Namco ES1 games: colour
// cameras only, held open (see webcam.c).
int webcamIsPath(const char *path);
int webcamOpen(const char *path, int flags, int mode, int (*realOpen)(const char *, int, ...));
// A colour camera is there (for the games that can do without one).
int webcamHasCamera(void);

#ifdef _WIN32
// Windows has no /dev/video: webcamWin.c answers the games' V4L2 calls from SDL's
// cameras.
#ifdef __cplusplus
extern "C"
{
#endif
int webcamIsFd(int fd);
int webcamIoctl(int fd, unsigned long request, void *arg);
void webcamClose(int fd);
void *webcamMmap(int fd, unsigned int offset);
int webcamIsMapped(const void *addr);
#ifdef __cplusplus
}
#endif
#endif

#endif // WEBCAM_H
