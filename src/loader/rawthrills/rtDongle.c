// Aladdin HASP HL dongle, emulated at the HASP API level: the game's own
// Dongle* layer runs unchanged on top of it.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "rawthrills.h"

#define HASP_STATUS_OK 0
#define HASP_FEATURE_NOT_FOUND 1
#define HASP_SESSION_INFO "<haspid>287339817</haspid>"

// Dongle memory offsets the games read.
#define DONGLE_CAB_TYPE 0x1d
#define DONGLE_FLAG 0x1e
#define DONGLE_SERIAL 0x3c

#define CAB_TYPE 4
#define CAB_SERIAL 0xc0ffee

static uint32_t haspFeature;
static uint32_t haspMemoryFileId;

static int haspLogin(uint32_t featureId, const void *vendorCode, uint32_t *handle)
{
    (void)vendorCode;
    (void)handle;
    return featureId == haspFeature ? HASP_STATUS_OK : HASP_FEATURE_NOT_FOUND;
}

static int haspOk(void)
{
    return HASP_STATUS_OK;
}

// Dongle memory is all zeros except the cabinet type, a flag and the
// cabinet serial number.
static int haspRead(uint32_t handle, uint32_t fileId, uint32_t offset, uint32_t length, void *buffer)
{
    (void)handle;
    if (fileId != haspMemoryFileId)
        return HASP_STATUS_OK;
    if (length == 1)
    {
        uint8_t v = offset == DONGLE_CAB_TYPE ? CAB_TYPE : offset == DONGLE_FLAG ? 1 : 0;
        memcpy(buffer, &v, 1);
    }
    else if (length == 4)
    {
        uint32_t v = offset == DONGLE_SERIAL ? CAB_SERIAL : 0;
        memcpy(buffer, &v, 4);
    }
    else
    {
        memset(buffer, 0, length);
    }
    return HASP_STATUS_OK;
}

static int haspGetSessionInfo(uint32_t handle, const char *format, char **info)
{
    (void)handle;
    (void)format;
    *info = strdup(HASP_SESSION_INFO);
    return HASP_STATUS_OK;
}

static int haspFree(void *info)
{
    free(info);
    return HASP_STATUS_OK;
}

// The dumps ship with decrypted data, so DongleEncrypt/DongleDecrypt only
// need to report success (status field at +0x10 of the dongle object).
static int dongleCrypt(uint8_t *dongle)
{
    *(uint32_t *)(dongle + 0x10) = 0;
    return 0;
}

void rtInstallDongle(const RtGame *game)
{
    haspFeature = game->haspFeature;
    haspMemoryFileId = game->haspMemoryFileId;
    rtDetour("hasp_login", haspLogin);
    rtDetour("hasp_logout", haspOk);
    rtDetour("hasp_read", haspRead);
    rtDetour("hasp_write", haspOk);
    rtDetour("hasp_get_sessioninfo", haspGetSessionInfo);
    rtDetour("hasp_free", haspFree);
    rtDetour("DongleEncrypt", dongleCrypt);
    rtDetour("DongleDecrypt", dongleCrypt);
    rtDetour("DongleWriteLoop", haspOk);
    rtDetour("DongleNoise", haspOk);
    rtDetour("DongleWriteHLFeature", haspOk);
    if (game->haspReadPatch)
        rtDetourAddress(game->haspReadPatch, haspRead);
    if (game->haspWritePatch)
        rtDetourAddress(game->haspWritePatch, haspOk);
    if (game->haspSessionInfoPatch)
        rtDetourAddress(game->haspSessionInfoPatch, haspGetSessionInfo);
}
