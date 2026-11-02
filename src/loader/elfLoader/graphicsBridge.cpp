#include "graphicsBridge.hpp"
#include "../graphics/x11Bridge.h"
#include "../graphics/glxBridge.h"
#include "../graphics/glutBridge.h"
#include "../graphics/shaderPatches.h"
#include "symbolResolver.hpp"
#include "../log/log.h"

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))
#define MAP_WITH_ORIG(name, func, origPtr) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func), reinterpret_cast<void**>(origPtr))

extern "C" {
    extern void* real_cgCreateProgram;
    extern void* real_cgGetProgramString;
}

namespace GraphicsBridge
{

    void initBridges()
    {
        log_info("Initializing Graphics Bridges (OpenGL/GLX/X11)...");

        // ============================================================
        // GLUT / GLU
        // ============================================================
        MAP("glutInit", bridgeGlutInit);
        MAP("glutInitDisplayMode", bridgeGlutInitDisplayMode);
        MAP("glutInitWindowSize", bridgeGlutInitWindowSize);
        MAP("glutInitWindowPosition", bridgeGlutInitWindowPosition);
        MAP("glutEnterGameMode", bridgeGlutEnterGameMode);
        MAP("glutFullScreen", bridgeGlutFullScreen);
        MAP("glutLeaveGameMode", bridgeGlutLeaveGameMode);
        MAP("glutCreateWindow", bridgeGlutCreateWindow);
        MAP("glutMainLoop", bridgeGlutMainLoop);
        MAP("glutDisplayFunc", bridgeGlutDisplayFunc);
        MAP("glutIdleFunc", bridgeGlutIdleFunc);
        MAP("glutPostRedisplay", bridgeGlutPostRedisplay);
        MAP("glutSwapBuffers", bridgeGlutSwapBuffers);
        MAP("glutGet", bridgeGlutGet);
        MAP("glutSetCursor", bridgeGlutSetCursor);
        MAP("glutGameModeString", bridgeGlutGameModeString);
        MAP("glutBitmapCharacter", bridgeGlutBitmapCharacter);
        MAP("glutBitmapWidth", bridgeGlutBitmapWidth);
        MAP("glutMainLoopEvent", bridgeGlutMainLoopEvent);
        MAP("glutJoystickFunc", bridgeGlutJoystickFunc);
        MAP("glutPostRedisplay", bridgeGlutPostRedisplay);
        MAP("glutGet", bridgeGlutGet);
        MAP("glutSetCursor", bridgeGlutSetCursor);
        MAP("glutGameModeString", bridgeGlutGameModeString);
        MAP("glutBitmapCharacter", bridgeGlutBitmapCharacter);
        MAP("glutBitmapWidth", bridgeGlutBitmapWidth);
        MAP("glutKeyboardFunc", bridgeGlutKeyboardFunc);
        MAP("glutKeyboardUpFunc", bridgeGlutKeyboardUpFunc);
        MAP("glutSpecialUpFunc", bridgeGlutSpecialUpFunc);
        MAP("glutMotionFunc", bridgeGlutMotionFunc);
        MAP("glutMouseFunc", bridgeGlutMouseFunc);
        MAP("glutPassiveMotionFunc", bridgeGlutPassiveMotionFunc);
        MAP("glutEntryFunc", bridgeGlutEntryFunc);
        MAP("glutJoystickFunc", bridgeGlutJoystickFunc);
        MAP("glutSolidTeapot", bridgeGlutSolidTeapot);
        MAP("glutWireTeapot", bridgeGlutWireTeapot);
        MAP("glutSpecialFunc", bridgeGlutSpecialFunc);
        MAP("glutReshapeFunc", bridgeGlutReshapeFunc);
        MAP("glutSolidSphere", bridgeGlutSolidSphere);
        MAP("glutWireSphere", bridgeGlutWireSphere);
        MAP("glutWireCone", bridgeGlutWireCone);
        MAP("glutSolidCone", bridgeGlutSolidCone);
        MAP("glutWireCube", bridgeGlutWireCube);
        MAP("glutSolidCube", bridgeGlutSolidCube);
        MAP("glutVisibilityFunc", bridgeGlutVisibilityFunc);
        MAP("glutExtensionSupported", bridgeGlutExtensionSupported);
        // MAP("glutGetModifiers", bridgeGlutGetModifiers);

        MAP("gluPerspective", bridgegluPerspective);
        MAP("gluLookAt", bridgegluLookAt);
        MAP("gluOrtho2D", bridgegluOrtho2D);
        MAP("gluErrorString", bridgegluErrorString);
        MAP("gluProject", bridgegluProject);
        MAP("gluUnProject", bridgegluUnProject);

        // XF86VidMode
        MAP("XF86VidModeQueryExtension", bridgeXF86VidModeQueryExtension);
        MAP("XF86VidModeGetAllModeLines", bridgeXF86VidModeGetAllModeLines);
        MAP("XF86VidModeGetModeLine", bridgeXF86VidModeGetModeLine);
        MAP("XF86VidModeSwitchToMode", bridgeXF86VidModeSwitchToMode);
        MAP("XF86VidModeSetViewPort", bridgeXF86VidModeSetViewPort);
        MAP("XF86VidModeGetViewPort", bridgeXF86VidModeGetViewPort);
        MAP("XF86VidModeLockModeSwitch", bridgeXF86VidModeLockModeSwitch);
        MAP("XF86VidModeQueryVersion", bridgeXF86VidModeQueryVersion);

        // X11 Misc
        MAP("XOpenDisplay", bridgeXOpenDisplay);
        MAP("XCloseDisplay", bridgeXCloseDisplay);
        MAP("XInitThreads", bridgeXInitThreads);
        MAP("XLockDisplay", bridgeXLockDisplay);
        MAP("XUnlockDisplay", bridgeXUnlockDisplay);
        MAP("XSync", bridgeXSync);
        MAP("XFlush", bridgeXFlush);
        MAP("_XFlush", bridgeXFlush);
        MAP("_XReply", bridgeXReply);
        MAP("XCreateWindow", bridgeXCreateWindow);
        MAP("XDestroyWindow", bridgeXDestroyWindow);
        MAP("XConfigureWindow", bridgeXConfigureWindow);
        MAP("XMapWindow", bridgeXMapWindow);
        MAP("XGetWindowAttributes", bridgeXGetWindowAttributes);
        MAP("XChangeWindowAttributes", bridgeXChangeWindowAttributes);
        MAP("XGetGeometry", bridgeXGetGeometry);
        MAP("XMapRaised", bridgeXMapRaised);
        MAP("XextFindDisplay", bridgeXextFindDisplay);
        MAP("XMoveWindow", bridgeXMoveWindow);
        MAP("XResizeWindow", bridgeXResizeWindow);
        MAP("XPending", bridgeXPending);
        MAP("XNextEvent", bridgeXNextEvent);
        MAP("XTranslateCoordinates", bridgeXTranslateCoordinates);
        MAP("XQueryPointer", bridgeXQueryPointer);
        MAP("XWarpPointer", bridgeXWarpPointer);
        MAP("XGrabPointer", bridgeXGrabPointer);
        MAP("XUngrabPointer", bridgeXUngrabPointer);
        MAP("XLookupString", bridgeXLookupString);
        MAP("XStoreName", bridgeXStoreName);
        MAP("XCreateColormap", bridgeXCreateColormap);
        MAP("XFreeColormap", bridgeXFreeColormap);
        MAP("XSetTransientForHint", bridgeXSetTransientForHint);

        MAP("XDisplayWidth", bridgeXDisplayWidth);
        MAP("XDisplayHeight", bridgeXDisplayHeight);
        MAP("XDisplayWidthMM", bridgeXDisplayWidthMM);
        MAP("XDisplayHeightMM", bridgeXDisplayHeightMM);
        MAP("XSetWMNormalHints", bridgeXSetWMNormalHints);
        MAP("XInternAtom", (Atom (*)(Display *, const char *, bool))bridgeXInternAtom);
        MAP("XSetStandardProperties", bridgeXSetStandardProperties);
        MAP("XRRGetScreenResources", bridgeXRRGetScreenResources);
        MAP("XRRGetCrtcInfo", bridgeXRRGetCrtcInfo);

        MAP("XParseColor", bridgeXParseColor);
        MAP("XFree", bridgeXFree);

        MAP("XSetErrorHandler", bridgeXSetErrorHandler);
        MAP("XGetErrorText", bridgeXGetErrorText);

        MAP("XCreatePixmapCursor", bridgeXCreatePixmapCursor);
        MAP("XCreatePixmapFromBitmapData", bridgeXCreatePixmapFromBitmapData);
        MAP("XFreePixmap", bridgeXFreePixmap);
        MAP("XFreeCursor", bridgeXFreeCursor);
        MAP("XDefineCursor", bridgeXDefineCursor);
        MAP("XUndefineCursor", bridgeXUndefineCursor);

        MAP("XSetInputFocus", bridgeXSetInputFocus);
        MAP("XSetWMProtocols", bridgeXSetWMProtocols);
        MAP("XSetWMProperties", bridgeXSetWMProperties);
        MAP("XStringListToTextProperty", bridgeXStringListToTextProperty);
        MAP("XChangeProperty", bridgeXChangeProperty);
        MAP("XSetCloseDownMode", bridgeXSetCloseDownMode);
        MAP("XAutoRepeatOn", bridgeXAutoRepeatOn);
        MAP("XAutoRepeatOff", bridgeXAutoRepeatOff);
        MAP("XCreateBitmapFromData", bridgeXCreateBitmapFromData);

        MAP("XGrabKeyboard", bridgeXGrabKeyboard);
        MAP("XUngrabKeyboard", bridgeXUngrabKeyboard);
        MAP("XKeysymToKeycode", bridgeXKeysymToKeycode);
        MAP("XSendEvent", bridgeXSendEvent);

        MAP("XAllocWMHints", bridgeXAllocWMHints);

        // g6 (Angry Birds / The Walking Dead) + Pac-Man's fuller X11 client.
        // Two of these (XCheckTypedEvent, XSelectInput) were already implemented,
        // just never registered.
        MAP("XCheckTypedEvent", bridgeXCheckTypedEvent);
        MAP("XSelectInput", bridgeXSelectInput);
        MAP("XDefaultScreen", bridgeXDefaultScreen);
        MAP("XDisplayName", bridgeXDisplayName);
        MAP("XQueryExtension", bridgeXQueryExtension);
        MAP("XMissingExtension", bridgeXMissingExtension);
        MAP("XEventsQueued", bridgeXEventsQueued);
        MAP("XPeekEvent", bridgeXPeekEvent);
        MAP("XMaskEvent", bridgeXMaskEvent);
        MAP("XCreateSimpleWindow", bridgeXCreateSimpleWindow);
        MAP("XRaiseWindow", bridgeXRaiseWindow);
        MAP("XIconifyWindow", bridgeXIconifyWindow);
        MAP("XMoveResizeWindow", bridgeXMoveResizeWindow);
        MAP("XReparentWindow", bridgeXReparentWindow);
        MAP("XClearWindow", bridgeXClearWindow);
        MAP("XInstallColormap", bridgeXInstallColormap);
        MAP("XSetWindowBackground", bridgeXSetWindowBackground);
        MAP("XSetWindowBackgroundPixmap", bridgeXSetWindowBackgroundPixmap);
        MAP("XSetWindowColormap", bridgeXSetWindowColormap);
        MAP("XResetScreenSaver", bridgeXResetScreenSaver);
        MAP("XChangePointerControl", bridgeXChangePointerControl);
        MAP("XGetPointerControl", bridgeXGetPointerControl);
        MAP("XGetVisualInfo", bridgeXGetVisualInfo);
        MAP("XMatchVisualInfo", bridgeXMatchVisualInfo);
        MAP("XVisualIDFromVisual", bridgeXVisualIDFromVisual);
        MAP("XCreateGC", bridgeXCreateGC);
        MAP("XFreeGC", bridgeXFreeGC);
        MAP("XCreatePixmap", bridgeXCreatePixmap);
        MAP("XCreateImage", bridgeXCreateImage);
        MAP("XPutImage", bridgeXPutImage);
        MAP("XListPixmapFormats", bridgeXListPixmapFormats);
        MAP("XShmQueryExtension", bridgeXShmQueryExtension);
        MAP("XShmAttach", bridgeXShmAttach);
        MAP("XShmDetach", bridgeXShmDetach);
        MAP("XShmCreateImage", bridgeXShmCreateImage);
        MAP("XShmPutImage", bridgeXShmPutImage);
        MAP("XKeycodeToKeysym", bridgeXKeycodeToKeysym);
        MAP("XQueryKeymap", bridgeXQueryKeymap);
        MAP("XRefreshKeyboardMapping", bridgeXRefreshKeyboardMapping);
        MAP("XGetModifierMapping", bridgeXGetModifierMapping);
        MAP("XFreeModifiermap", bridgeXFreeModifiermap);
        MAP("XAllocColor", bridgeXAllocColor);
        MAP("XFreeColors", bridgeXFreeColors);
        MAP("XQueryColors", bridgeXQueryColors);
        MAP("XStoreColors", bridgeXStoreColors);
        MAP("XAllocClassHint", bridgeXAllocClassHint);
        MAP("XAllocSizeHints", bridgeXAllocSizeHints);
        MAP("XSetClassHint", bridgeXSetClassHint);
        MAP("XSetWMHints", bridgeXSetWMHints);
        MAP("XGetWMHints", bridgeXGetWMHints);
        MAP("XSetTextProperty", bridgeXSetTextProperty);
        MAP("XDeleteProperty", bridgeXDeleteProperty);
        MAP("Xutf8TextListToTextProperty", bridgeXutf8TextListToTextProperty);
        MAP("Xutf8LookupString", bridgeXutf8LookupString);
        MAP("XSetIOErrorHandler", bridgeXSetIOErrorHandler);
        MAP("XGetErrorDatabaseText", bridgeXGetErrorDatabaseText);
        MAP("XextAddDisplay", bridgeXextAddDisplay);
        MAP("XextCreateExtension", bridgeXextCreateExtension);
        MAP("XextDestroyExtension", bridgeXextDestroyExtension);
        MAP("XextRemoveDisplay", bridgeXextRemoveDisplay);
        MAP("XSetExtensionErrorHandler", bridgeXSetExtensionErrorHandler);
        MAP("XESetEventToWire", bridgeXESetEventToWire);
        MAP("XESetWireToEvent", bridgeXESetWireToEvent);
        MAP("_XEatData", bridge_XEatData);
        MAP("_XGetRequest", bridge_XGetRequest);
        MAP("_XSend", bridge_XSend);
        MAP("_XRead", bridge_XRead);
        MAP("_XReadPad", bridge_XReadPad);
        MAP("_XSetLastRequestRead", bridge_XSetLastRequestRead);
        MAP("_XFlushGCCache", bridge_XFlushGCCache);
        MAP("XOpenIM", bridgeXOpenIM);
        MAP("XCloseIM", bridgeXCloseIM);
        MAP("XCreateIC", bridgeXCreateIC);
        MAP("XDestroyIC", bridgeXDestroyIC);
        MAP("XGetICValues", bridgeXGetICValues);
        MAP("XSetICFocus", bridgeXSetICFocus);
        MAP("XUnsetICFocus", bridgeXUnsetICFocus);
        MAP("XDisplayOfIM", bridgeXDisplayOfIM);
        MAP("XSetLocaleModifiers", bridgeXSetLocaleModifiers);
        MAP("XFilterEvent", bridgeXFilterEvent);
        MAP("XRRQueryVersion", bridgeXRRQueryVersion);
        MAP("XRRGetScreenInfo", bridgeXRRGetScreenInfo);
        MAP("XRRFreeScreenConfigInfo", bridgeXRRFreeScreenConfigInfo);
        MAP("XRRSetScreenConfig", bridgeXRRSetScreenConfig);
        MAP("XRRConfigCurrentConfiguration", bridgeXRRConfigCurrentConfiguration);
        MAP("XRRConfigSizes", bridgeXRRConfigSizes);

        // ============================================================
        // GLX Functions
        // ============================================================
        MAP("glXChooseVisual", bridgeGlxChooseVisual);
        MAP("glXCreateContext", bridgeGlxCreateContext);
        MAP("glXMakeCurrent", bridgeGlxMakeCurrent);
        MAP("glXCreateNewContext", bridgeGlxCreateNewContext);
        MAP("glXDestroyPbuffer", bridgeGlxDestroyPbuffer);
        MAP("glXCreatePbuffer", bridgeGlxCreatePbuffer);
        MAP("glXSwapBuffers", bridgeGlxSwapBuffers);
        MAP("glXDestroyContext", bridgeGlxDestroyContext);
        MAP("glXGetCurrentDisplay", bridgeGlxGetCurrentDisplay);
        MAP("glXGetCurrentDrawable", bridgeGlxGetCurrentDrawable);
        MAP("glXGetCurrentContext", bridgeGlxGetCurrentContext);
        MAP("glXChooseFBConfig", bridgeGlxChooseFBConfig);
        MAP("glXGetVisualFromFBConfig", bridgeGlxGetVisualFromFBConfig);
        MAP("glXGetProcAddressARB", bridgeGlxGetProcAddress);
        MAP("glXGetProcAddress", bridgeGlxGetProcAddress);
        MAP("glXQueryExtension", bridgeGlxQueryExtension);
        MAP("glXSwapIntervalSGI", bridgeGlxSwapInterval);
        MAP("glXQueryExtension", bridgeGlxQueryExtension);
        MAP("glXQueryExtensionsString", bridgeGlxQueryExtensionsString);
        MAP("glXQueryServerString", bridgeGlxQueryServerString);
        MAP("glXQueryVersion", bridgeGlxQueryVersion);
        MAP("glXGetClientString", bridgeGlxGetClientString);
        MAP("glXIsDirect", bridgeGlxIsDirect);
        MAP("glXGetConfig", bridgeGlxGetConfig);

        // GLX SGI
        MAP("glXSwapIntervalSGI", bridgeGlxSwapIntervalSGI);
        MAP("glXGetVideoSyncSGI", bridgeGlxGetVideoSyncSGI);
        MAP("glXGetRefreshRateSGI", bridgeGlxGetRefreshRateSGI);

        // GLX SGIX / FBConfig Extensions
        MAP("glXChooseFBConfigSGIX", bridgeGlxChooseFBConfigSGIX);
        MAP("glXGetFBConfigAttribSGIX", bridgeGlxGetFBConfigAttribSGIX);
        MAP("glXCreateContextWithConfigSGIX", bridgeGlxCreateContextWithConfigSGIX);
        MAP("glXCreateGLXPbufferSGIX", bridgeGlxCreateGLXPbufferSGIX);
        MAP("glXDestroyGLXPbufferSGIX", bridgeGlxDestroyGLXPbufferSGIX);

        // CG
        MAP_WITH_ORIG("cgCreateProgram", bridgeCgCreateProgram, &real_cgCreateProgram);
        MAP_WITH_ORIG("cgGetProgramString", bridgeCgGetProgramString, &real_cgGetProgramString);
    }
} // namespace GraphicsBridge
