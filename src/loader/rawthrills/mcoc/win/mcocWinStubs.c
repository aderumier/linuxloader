// Marvel Contest of Champions on Windows: the plugins with nothing behind
// them yet.
//
// - WebCamRT: no camera (the game is told so, Config.NoWebCam: mcocFrame);
//   the players pick with the stick or the card binder. Its answers: 0 is
//   success, or yes.
// - WeChatCURL: China's online login, unused here: every call fails.
// - The HASP library: the game calls hasp_login once, to have its
//   delegates marshalled, and reads nothing back (see unityRt.c).
#include <stdint.h>
#include <string.h>
#include "../mcoc.h"

#define WEBCAM_OK 0
#define WEBCAM_FAIL (-1)
#define EXPORT __declspec(dllexport)

int mcocWebcamCount(void) { return 0; }
void mcocWebcamAssign(void) {}

EXPORT int WebCamRT_SetLogCallback(void *cb) { (void)cb; return WEBCAM_OK; }
EXPORT int WebCamRT_Create(void) { return WEBCAM_OK; }
EXPORT int WebCamRT_Destroy(void) { return WEBCAM_OK; }
EXPORT int WebCamRT_Enum_UpdateDevices(void) { return WEBCAM_OK; }
EXPORT int WebCamRT_Enum_NewDeviceCount(void) { return 0; }
EXPORT int WebCamRT_Enum_NewDevicePath(int index, char *out, int max)
{
    (void)index;
    if (out && max > 0)
        *out = 0;
    return WEBCAM_FAIL;
}
EXPORT int WebCamRT_CameraNew(const char *path) { (void)path; return WEBCAM_FAIL; }
EXPORT int WebCamRT_CameraDelete(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_DeepProbeDevice(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_DeepProbeDeviceResults(int id, char *out, int *size)
{
    (void)id;
    (void)out;
    if (size)
        *size = 0;
    return WEBCAM_FAIL;
}
EXPORT int WebCamRT_OpenConfig_Probe(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_OpenConfig_GetCount(int id) { (void)id; return 0; }
EXPORT int WebCamRT_OpenConfig_Get(int id, int index, int *format, int *width, int *height, int *fps)
{
    (void)id; (void)index;
    *format = *width = *height = *fps = 0;
    return WEBCAM_FAIL;
}
EXPORT int WebCamRT_OpenConfig_SetWidth(int id, int v) { (void)id; (void)v; return WEBCAM_FAIL; }
EXPORT int WebCamRT_OpenConfig_SetHeight(int id, int v) { (void)id; (void)v; return WEBCAM_FAIL; }
EXPORT int WebCamRT_OpenConfig_SetFPS(int id, int v) { (void)id; (void)v; return WEBCAM_FAIL; }
EXPORT int WebCamRT_OpenConfig_SetFormatSource(int id, int v) { (void)id; (void)v; return WEBCAM_FAIL; }
EXPORT int WebCamRT_OpenConfig_SetGreyscale(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_CameraOpen(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_CameraClose(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_CaptureBegin(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_CaptureEnd(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_IsCapturing(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_IsFrameReady(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_IsCapturingAndFrameReady(int id) { (void)id; return WEBCAM_FAIL; }
EXPORT int WebCamRT_IsDisconnected(int id) { (void)id; return 0; }
EXPORT int WebCamRT_GrabFrame(int id, void *buf, unsigned size) { (void)id; (void)buf; (void)size; return WEBCAM_FAIL; }
EXPORT int WebCamRT_GetProp_DeviceName(int id, char *out, int max)
{
    (void)id;
    if (out && max > 0)
        *out = 0;
    return WEBCAM_FAIL;
}
EXPORT int WebCamRT_GetProp_Width(int id) { (void)id; return 0; }
EXPORT int WebCamRT_GetProp_Height(int id) { (void)id; return 0; }
EXPORT int WebCamRT_GetProp_DestFormat(int id) { (void)id; return 0; }
EXPORT int WebCamRT_GetProp_DestBytesPerPixel(int id) { (void)id; return 0; }
EXPORT int WebCamRT_GetSettingSupported(int id, int setting) { (void)id; (void)setting; return WEBCAM_FAIL; }
EXPORT int WebCamRT_GetSettingName(int id, int setting, char *out, int max)
{
    (void)id; (void)setting;
    if (out && max > 0)
        *out = 0;
    return WEBCAM_FAIL;
}
EXPORT int WebCamRT_GetSettingStep(int id, int setting, float *v) { (void)id; (void)setting; *v = 0.f; return WEBCAM_FAIL; }
EXPORT int WebCamRT_GetSettingValue01(int id, int setting, float *v) { (void)id; (void)setting; *v = 0.f; return WEBCAM_FAIL; }
EXPORT int WebCamRT_GetSettingDefalut01(int id, int setting, float *v) { (void)id; (void)setting; *v = 0.f; return WEBCAM_FAIL; }
EXPORT int WebCamRT_SetSettingValue01(int id, int setting, float v) { (void)id; (void)setting; (void)v; return WEBCAM_FAIL; }
EXPORT int WebCamRT_GetRealFPS(int id, float *v) { (void)id; *v = 0.f; return WEBCAM_FAIL; }
EXPORT int WebCamRT_GetErrorFlags(int id) { (void)id; return 0; }
EXPORT int WebCamRT_GetCameraFlags(int id) { (void)id; return 0; }
EXPORT int WebCamRT_SetCameraFlags(int id, int flags) { (void)id; (void)flags; return WEBCAM_OK; }
EXPORT int WebCamRT_GetDeviceInfo(int id, int *vid, int *pid, int *bcd)
{
    (void)id;
    *vid = *pid = *bcd = 0;
    return WEBCAM_FAIL;
}
EXPORT int WebCamRT_GetMaxPath(void) { return 256; }
EXPORT int WebCamRT_GetMaxName(void) { return 64; }
EXPORT int WebCamRT_SetTimeout(float v) { (void)v; return WEBCAM_OK; }
EXPORT int WebCamRT_GetTimeout(float *v) { *v = 1.f; return WEBCAM_OK; }

// WeChatCURL: nothing (0, NULL: a failure or no answer).
#define WECHAT(name) EXPORT intptr_t WeChatCURL_##name(void) { return 0; }
WECHAT(CheckForLogin) WECHAT(Create) WECHAT(Destroy) WECHAT(DownloadImage) WECHAT(GetAccessToken)
WECHAT(GetLastError) WECHAT(GetLoginQRDownloadURL) WECHAT(GetResponseString) WECHAT(LoginAsGuest)
WECHAT(NotifyRoleQuit) WECHAT(PushLogMessagesToUnity) WECHAT(SendBattleResults) WECHAT(SendCardScan)
WECHAT(SetLog) WECHAT(SetMachineCode) WECHAT(SetProdServer) WECHAT(SetTestServer)
WECHAT(UploadMachineRemainPoints)

EXPORT int hasp_login(int feature, const char *vendor, int *handle)
{
    (void)feature; (void)vendor;
    if (handle)
        *handle = 1;
    return 0;
}
