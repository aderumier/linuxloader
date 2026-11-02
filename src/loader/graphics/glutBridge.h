#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    void bridgeGlutInit(int *argcp, char **argv);
    void bridgeGlutMainLoop(void);
    void bridgeGlutMainLoopEvent(void);
    void bridgeGlutSwapBuffers(void);
    int bridgeGlutEnterGameMode(void);
    void bridgeGlutFullScreen(void);
    int bridgeGlutCreateWindow(const char *title);
    int bridgeGlutGet(int type);
    int bridgeGlutExtensionSupported(const char *extension);
    void bridgeGlutSetCursor(int glutCursor);
    void bridgeGlutInitWindowSize(int width, int height);
    void bridgeGlutInitWindowPosition(int x, int y);
    void bridgeGlutDisplayFunc(void (*callback)(void));
    void bridgeGlutReshapeFunc(void (*callback)(int, int));
    void bridgeGlutVisibilityFunc(void (*callback)(int));
    void bridgeGlutIdleFunc(void (*callback)(void));
    void bridgeGlutInitDisplayMode(unsigned int mode);
    void bridgeGlutGameModeString(const char *string);
    void bridgeGlutJoystickFunc(void (*callback)(unsigned int, int, int, int), int pollInterval);
    void bridgeGlutPostRedisplay(void);
    void bridgeGlutKeyboardFunc(void (*callback)(unsigned char, int, int));
    void bridgeGlutKeyboardUpFunc(void (*callback)(unsigned char, int, int));
    int bridgeGlutGetModifiers(void);
    void bridgeGlutIgnoreKeyRepeat(int repeat);
    void bridgeGlutMouseFunc(void (*callback)(int, int, int, int));
    void bridgeGlutMotionFunc(void (*callback)(int, int));
    void bridgeGlutSpecialFunc(void (*callback)(int, int, int));
    void bridgeGlutSpecialUpFunc(void (*callback)(int, int, int));
    void bridgeGlutPassiveMotionFunc(void (*callback)(int, int));
    void bridgeGlutEntryFunc(void (*callback)(int));
    void bridgeGlutLeaveGameMode(void);
    void bridgeGlutSolidTeapot(double size);
    void bridgeGlutWireTeapot(double size);
    void bridgeGlutSolidSphere(double radius, int slices, int stacks);
    void bridgeGlutWireSphere(double radius, int slices, int stacks);
    void bridgeGlutWireCone(double base, double height, int slices, int stacks);
    void bridgeGlutSolidCone(double base, double height, int slices, int stacks);
    void bridgeGlutWireCube(double dSize);
    void bridgeGlutSolidCube(double dSize);
    int bridgeGlutBitmapWidth(void *fontID, int character);
    void bridgeGlutBitmapCharacter(void *fontID, int character);

#ifdef __cplusplus
}
#endif
