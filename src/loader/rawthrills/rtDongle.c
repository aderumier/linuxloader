// Aladdin HASP HL dongle, emulated at the HASP API level: the game's own
// Dongle* layer runs unchanged on top of it.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rawthrills.h"
#include "../log/log.h"

#define HASP_STATUS_OK 0
#define HASP_FEATURE_NOT_FOUND 1
#define HASP_HASP_NOT_FOUND 7
#define HASP_TOO_SHORT 8

#define ANSWER_KEY_SIZE 16
#define MEMORY_DUMP "hhl_mem.dmp"
#define MEMORY_SIZE 128
#define HASP_SESSION_INFO "<haspid>287339817</haspid>"

// Dongle memory offsets the games read.
#define DONGLE_CAB_TYPE 0x1d
#define DONGLE_FLAG 0x1e
#define DONGLE_SERIAL 0x3c

#define CAB_TYPE 4
#define CAB_SERIAL 0xc0ffee

static uint32_t haspFeature;
static uint32_t haspMemoryFileId;
static char answersDir[PATH_MAX];
static uint8_t memory[MEMORY_SIZE];
static int memoryLoaded;

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
    if (memoryLoaded)
    {
        // A recorded dongle memory: read as is, zeros past its end.
        memset(buffer, 0, length);
        if (offset < MEMORY_SIZE)
            memcpy(buffer, memory + offset, length < MEMORY_SIZE - offset ? length : MEMORY_SIZE - offset);
    }
    else if (length == 1)
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

// hasp_encrypt/hasp_decrypt: the dongle's AES key never leaves it, so the
// results come from recorded answers, looked up by the first bytes of the
// input (the data is a hash or a secret, unique in those bytes).
static int haspCrypt(uint32_t handle, uint8_t *buffer, uint32_t length)
{
    char path[PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/", answersDir);
    FILE *f;

    (void)handle;
    if (length < ANSWER_KEY_SIZE)
        return HASP_TOO_SHORT;
    for (int i = 0; i < ANSWER_KEY_SIZE && n + 2 < (int)sizeof(path); i++)
        n += snprintf(path + n, sizeof(path) - n, "%02x", buffer[i]);
    if (!(f = fopen(path, "rb")))
    {
        log_warn("Raw Thrills: no recorded dongle answer %s", path);
        return HASP_HASP_NOT_FOUND;
    }
    uint8_t *answer = malloc(length);
    size_t got = answer ? fread(answer, 1, length, f) : 0;
    int extra = fgetc(f) != EOF;
    fclose(f);
    if (got != length || extra)
    {
        log_warn("Raw Thrills: recorded dongle answer %s is not %u bytes", path, length);
        free(answer);
        return HASP_HASP_NOT_FOUND;
    }
    memcpy(buffer, answer, length);
    free(answer);
    return HASP_STATUS_OK;
}

static void loadAnswers(const char *dir)
{
    char path[PATH_MAX];
    FILE *f;

    snprintf(answersDir, sizeof(answersDir), "%s/%s", rtGameDir(), dir);
    snprintf(path, sizeof(path), "%s/" MEMORY_DUMP, answersDir);
    if ((f = fopen(path, "rb")))
    {
        memoryLoaded = fread(memory, 1, sizeof(memory), f) == sizeof(memory);
        fclose(f);
    }
    if (!memoryLoaded)
        log_warn("Raw Thrills: cannot read the dongle memory %s", path);
    rtDetour("hasp_encrypt", haspCrypt);
    rtDetour("hasp_decrypt", haspCrypt);
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
    if (game->haspAnswers)
        loadAnswers(game->haspAnswers);
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
