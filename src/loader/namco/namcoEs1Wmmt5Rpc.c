// Wangan Midnight Maximum Tune 5DX+'s game server: its protobuf RPCs (POST
// <Mucha URL_1>/method/<name>) and resources (GET .../resource/<name>), a
// port of the Windows arcade loader's (crates/namcoes3srv: rpc.rs,
// records.rs, store.rs, its 5DX+ field numbers), itself after Bayshore
// (Project Asakura's server) and its 5DX branches.
//
// The players' data, in save/wm5srv/ of the game's folder, one protobuf
// message per file:
// - users/<id>: 1 user id, 2 card chip id, 3 access code, 4 maxi gold,
//   5 confirmed tutorials (bits), 6 600 HP cards, 7 car ids (repeated, the
//   player's order), 8 shop grade, 9 total maxi gold;
// - cars/<id>: LoadCarResponse's fields but its error (2 the car, 10 its
//   settings, the story / versus / ghost battle counters...), answered as
//   stored, the game's updates merged in;
// - ta/<id>: a car's best on a time attack course: 1 car id, 2 model,
//   3 course, 4 time, 5..11 the sections, 12 tune power, 13 handling.

#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "namcoEs1Wmmt4Pb.h"
#include "namcoEs1Wmmt4Server.h"

#define DIRECTORY "save/wm5srv"

#define ERR_SUCCESS 0
#define ERR_REQUEST 1
#define ERR_NOT_FOUND 2
#define MODE_TIME_ATTACK 2
#define RANKING_VS_STAR 100
#define RANKING_TROPHY 101

// 5DX+'s field numbers (the Windows loader's WM5DXP schema: 5DX's).
#define TA_COURSES 25
#define R_SETTING 10
#define R_ODOMETER 6
#define R_PLAY_COUNT 7
#define R_CUSTOM_COLOR 8
#define R_VS 12
#define R_ST 34
#define R_ITEMS 50
#define R_AURA_AUTO 51
#define R_SCREENSHOTS 53
#define R_RG_STAMP 54
#define R_MAXI_COIN 24
#define C_TITLE 33
#define C_PLAYED_AT 45
#define C_GHOST_LEVEL 49
#define C_COUNTRY 50
#define C_TUNE_POWER 31
#define C_LEVEL 34
#define S_TA 23
#define S_GOLD 18
#define S_ST 22
#define S_VS 24
#define S_RG 25
static const uint32_t recordRequired[] = {3,  4,  5,  6,  7,  8,  9,  12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
                                          23, 24, 25, 27, 28, 29, 30, 34, 35, 36, 37, 38, 39, 40, 41, 51, 53, 56};
static const uint32_t carRequired[] = {9,  10, 11, 12, 13, 14, 16, 17, 18, 20, 21, 22,
                                       23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 34};
static const uint32_t rgMap[][2] = {{8, 22}, {9, 23}, {10, 24}, {11, 4}, {12, 5}, {13, 25}, {14, 26}, {15, 27}, {23, 54}};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// ---------------------------------------------------------------------------
// The store.

static void path(char *buffer, size_t size, const char *kind, unsigned long id)
{
    snprintf(buffer, size, DIRECTORY "/%s/%lu", kind, id);
}

static int load(const char *kind, unsigned long id, PbMsg *msg)
{
    char file[256];
    memset(msg, 0, sizeof(*msg));
    path(file, sizeof(file), kind, id);
    FILE *f = fopen(file, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = n > 0 ? malloc(n) : NULL;
    int ok = n == 0 || (data && fread(data, 1, n, f) == (size_t)n && pbParse(msg, data, n));
    free(data);
    fclose(f);
    if (!ok)
        pbFree(msg);
    return ok;
}

static void save(const char *kind, unsigned long id, const PbMsg *msg)
{
    char file[256], temporary[300];
    size_t n;
    uint8_t *data = pbSerialize(msg, &n);

    mkdir("save", 0755);
    mkdir(DIRECTORY, 0755);
    snprintf(file, sizeof(file), DIRECTORY "/%s", kind);
    mkdir(file, 0755);
    path(file, sizeof(file), kind, id);
    snprintf(temporary, sizeof(temporary), "%s.tmp", file);
    FILE *f = fopen(temporary, "wb");
    if (f)
    {
        int ok = fwrite(data, 1, n, f) == n;
        ok &= fclose(f) == 0;
        if (ok)
            rename(temporary, file);
    }
    free(data);
}

static int byId(const void *a, const void *b)
{
    unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b;
    return x < y ? -1 : x > y;
}

// The ids of a kind, sorted (malloc'd, *n of them).
static unsigned long *ids(const char *kind, size_t *n)
{
    char dir[256];
    unsigned long *list = NULL;
    size_t capacity = 0;
    struct dirent *e;

    *n = 0;
    snprintf(dir, sizeof(dir), DIRECTORY "/%s", kind);
    DIR *d = opendir(dir);
    while (d && (e = readdir(d)))
    {
        char *end;
        unsigned long id = strtoul(e->d_name, &end, 10);
        if (end == e->d_name || *end)
            continue;
        if (*n == capacity)
        {
            capacity = capacity ? capacity * 2 : 16;
            unsigned long *grown = realloc(list, capacity * sizeof(*list));
            if (!grown)
                break;
            list = grown;
        }
        list[(*n)++] = id;
    }
    if (d)
        closedir(d);
    if (*n)
        qsort(list, *n, sizeof(*list), byId);
    return list;
}

static unsigned long newId(const char *kind)
{
    size_t n;
    unsigned long *list = ids(kind, &n), id = n ? list[n - 1] + 1 : 1;
    free(list);
    return id;
}

// The user whose field holds text (3: access code, 2: chip id): its id, 0
// when none.
static unsigned long findUser(uint32_t field, const char *text, PbMsg *user)
{
    size_t n;
    unsigned long *list = ids("users", &n), found = 0;
    char value[128];

    for (size_t i = 0; i < n && !found; i++)
    {
        if (!load("users", list[i], user))
            continue;
        if (!strcmp(pbString(user, field, value, sizeof(value)), text))
            found = list[i];
        else
            pbFree(user);
    }
    free(list);
    return found;
}

// ---------------------------------------------------------------------------
// Messages.

static uint64_t now(void)
{
    return (uint64_t)time(NULL);
}

// A repeated uint field's values, packed or not (malloc'd, *n of them).
static uint64_t *uints(const PbMsg *msg, uint32_t field, size_t *n)
{
    uint64_t *out = NULL;
    size_t capacity = 0;

    *n = 0;
    for (size_t i = 0; i < msg->count; i++)
    {
        const PbField *f = &msg->fields[i];
        if (f->field != field)
            continue;
        const uint8_t *p = f->data, *end = f->data + f->length;
        int packed = f->wire == PB_BYTES;
        do
        {
            uint64_t v = f->value;
            if (packed)
            {
                v = 0;
                for (int shift = 0; p < end && shift < 64; shift += 7)
                {
                    v |= (uint64_t)(*p & 0x7f) << shift;
                    if (!(*p++ & 0x80))
                        break;
                }
            }
            if (*n == capacity)
            {
                capacity = capacity ? capacity * 2 : 8;
                uint64_t *grown = realloc(out, capacity * sizeof(*out));
                if (!grown)
                    return out;
                out = grown;
            }
            out[(*n)++] = v;
        } while (packed && p < end);
    }
    return out;
}

// The sub-message field of msg with update's fields replacing its own.
static void mergeSub(PbMsg *msg, uint32_t field, const PbMsg *update)
{
    PbMsg sub = {0};
    pbSub(msg, field, &sub);
    pbMerge(&sub, update);
    pbSetMsg(msg, field, &sub);
    pbFree(&sub);
}

// The fields of from (src) set as dst of to.
static void copyFields(PbMsg *to, const PbMsg *from, const uint32_t (*map)[2], size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        const PbField *f = pbGet(from, map[i][0]);
        if (!f)
            continue;
        PbField g = *f;
        g.field = map[i][1];
        pbRemove(to, map[i][1]);
        pbAddField(to, &g);
    }
}

// The user's car order: car first, the others after (without it).
static void carFirst(PbMsg *user, unsigned long car)
{
    size_t n;
    uint64_t *order = uints(user, 7, &n);
    pbRemove(user, 7);
    pbAddUint(user, 7, car);
    for (size_t i = 0; i < n; i++)
        if (order[i] != car)
            pbAddUint(user, 7, order[i]);
    free(order);
}

static void answer(Wmmt4Response *response, const PbMsg *msg)
{
    response->status = 200;
    response->contentType = "application/x-protobuf; revision=1290";
    response->body = pbSerialize(msg, &response->length);
}

static void feature(PbMsg *msg, uint32_t field)
{
    PbMsg f = {0};
    pbAddUint(&f, 1, 100);
    pbAddUint(&f, 2, 2014);
    pbAddUint(&f, 3, 5);
    pbAddUint(&f, 4, 0);
    pbAddUint(&f, 5, 0);
    pbAddMsg(msg, field, &f);
    pbFree(&f);
}

static void transferNotice(PbMsg *msg, uint32_t field)
{
    PbMsg t = {0};
    pbAddUint(&t, 1, 0);
    pbAddUint(&t, 2, 0);
    pbAddUint(&t, 3, 0);
    pbAddMsg(msg, field, &t);
    pbFree(&t);
}

// ---------------------------------------------------------------------------
// Users.

// The user of a request's access code (field), made for a new card when
// create: its id (0: none), the record in user.
static unsigned long requestUser(const PbMsg *req, uint32_t field, uint32_t chipField, int create, PbMsg *user)
{
    char code[64], chip[128];
    memset(user, 0, sizeof(*user));
    if (!*pbString(req, field, code, sizeof(code)))
        return 0;
    unsigned long id = findUser(3, code, user);
    if (id || !create)
        return id;
    id = newId("users");
    pbAddUint(user, 1, id);
    pbString(req, chipField, chip, sizeof(chip));
    pbAddBytes(user, 2, chip, strlen(chip));
    pbAddBytes(user, 3, code, strlen(code));
    for (uint32_t f = 4; f <= 9; f++)
        if (f != 7)
            pbAddUint(user, f, 0);
    save("users", id, user);
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT5 server: new user %lu (card %s)\n", id, code);
    return id;
}

// The user's maxi gold, 600 HP cards, tutorials and shop grade from a request
// (fields, 0 for none).
static void updateUser(PbMsg *user, const PbMsg *req, uint32_t gold, uint32_t hp600, uint32_t tutorials, uint32_t grade)
{
    if (gold && pbHas(req, gold))
    {
        uint64_t v = pbUint(req, gold, 0), total = pbUint(user, 9, 0);
        pbSetUint(user, 4, v);
        pbSetUint(user, 9, v > total ? v : total);
    }
    if (hp600 && pbHas(req, hp600))
        pbSetUint(user, 6, pbUint(req, hp600, 0));
    if (tutorials)
    {
        size_t n;
        uint64_t *list = uints(req, tutorials, &n), bits = pbUint(user, 5, 0);
        for (size_t i = 0; i < n; i++)
            bits |= list[i];
        free(list);
        pbSetUint(user, 5, bits);
    }
    if (grade && pbHas(req, grade))
        pbSetUint(user, 8, pbUint(req, grade, 0));
}

static void registerSystemInfo(const PbMsg *req, PbMsg *res)
{
    char place[32];
    pbAddUint(res, 1, ERR_SUCCESS);
    pbAddUint(res, 2, pbUint(req, 9, 1));
    pbString(req, 10, place, sizeof(place));
    pbSetString(res, 3, *place ? place : "JPN0123");
    pbAddUint(res, 6, INT_MAX);
    pbAddUint(res, 7, INT_MAX);
    pbAddUint(res, 8, 0);
    pbAddUint(res, 9, 0);
    feature(res, 10);
}

static void loadUser(const PbMsg *req, PbMsg *res)
{
    PbMsg user;
    char code[64];
    unsigned long id = requestUser(req, 2, 1, 1, &user);

    if (!id)
    {
        static const uint32_t zero[] = {7, 8, 10, 13, 14, 15, 21, 22};
        pbAddUint(res, 1, ERR_REQUEST);
        for (size_t i = 0; i < COUNT(zero); i++)
            pbAddUint(res, zero[i], 0);
        return;
    }
    size_t max = (size_t)pbUint(req, 8, SIZE_MAX), n, shown = 0;
    uint64_t *cars = uints(&user, 7, &n);
    pbAddUint(res, 1, ERR_SUCCESS);
    pbSetString(res, 3, pbString(&user, 3, code, sizeof(code)));
    pbAddUint(res, 4, 1);
    pbAddUint(res, 6, id);
    pbAddUint(res, 7, pbUint(&user, 4, 0));
    pbAddUint(res, 8, pbUint(&user, 9, 0));
    pbAddUint(res, 10, n);
    for (size_t i = 0; i < n && shown < max; i++)
    {
        PbMsg record;
        if (!load("cars", cars[i], &record))
            continue;
        const PbField *car = pbGet(&record, 2);
        pbAddBytes(res, 11, car ? car->data : NULL, car ? car->length : 0);
        pbFree(&record);
        shown++;
    }
    for (size_t i = 0; i < shown; i++)
    {
        // no opponent ghost, no rename, not deleted, no event
        PbMsg state = {0};
        pbAddUint(&state, 1, 0);
        pbAddUint(&state, 3, 0);
        pbAddUint(&state, 4, 0);
        pbAddUint(&state, 5, 0);
        pbAddMsg(res, 12, &state);
        pbFree(&state);
    }
    pbAddUint(res, 13, n ? 1 : 0);
    pbAddUint(res, 14, pbUint(&user, 6, 0));
    pbAddUint(res, 15, pbUint(&user, 5, 0));
    pbAddUint(res, 21, 0);
    pbAddUint(res, 22, 0);
    free(cars);
    pbFree(&user);
}

static void createUser(const PbMsg *req, PbMsg *res)
{
    PbMsg user;
    char code[64];
    unsigned long id = requestUser(req, 3, 2, 1, &user);

    if (!id)
    {
        pbAddUint(res, 1, ERR_NOT_FOUND);
        pbAddUint(res, 5, 0);
        return;
    }
    pbAddUint(res, 1, ERR_SUCCESS);
    pbSetString(res, 2, pbString(&user, 3, code, sizeof(code)));
    pbAddUint(res, 5, id);
    pbFree(&user);
}

static void askAccessCode(const PbMsg *req, PbMsg *res)
{
    PbMsg user;
    char chip[128], code[64] = "";
    if (findUser(2, pbString(req, 1, chip, sizeof(chip)), &user))
    {
        pbString(&user, 3, code, sizeof(code));
        pbFree(&user);
    }
    pbAddUint(res, 1, ERR_SUCCESS);
    pbSetString(res, 2, code);
}

static void receiveMaxiGold(const PbMsg *req, PbMsg *res)
{
    PbMsg user;
    uint64_t gold = 0;
    if (load("users", pbUint(req, 1, 0), &user))
    {
        gold = pbUint(&user, 4, 0);
        pbFree(&user);
    }
    pbAddUint(res, 1, ERR_SUCCESS);
    pbAddUint(res, 2, gold);
    pbAddUint(res, 3, gold);
}

static void saveTerminalResult(const PbMsg *req)
{
    PbMsg user;
    unsigned long id = pbUint(req, 1, 0);
    if (!id || !load("users", id, &user))
        return;
    size_t n;
    uint64_t *order = uints(req, 3, &n);
    if (n)
    {
        pbRemove(&user, 7);
        for (size_t i = 0; i < n; i++)
            pbAddUint(&user, 7, order[i]);
    }
    free(order);
    updateUser(&user, req, 0, 0, 5, 0);
    save("users", id, &user);
    pbFree(&user);
}

// ---------------------------------------------------------------------------
// Cars.

static void createCar(const PbMsg *req, PbMsg *res)
{
    PbMsg user = {0}, car = {0}, given = {0}, setting = {0}, record = {0};
    unsigned long userId = pbUint(req, 1, 0);
    char text[64];

    if (userId ? !load("users", userId, &user) : !(userId = requestUser(req, 4, 3, 1, &user)))
    {
        pbAddUint(res, 1, ERR_NOT_FOUND);
        pbAddUint(res, 5, 0);
        pbAddUint(res, 6, 0);
        return;
    }
    unsigned long carId = newId("cars");
    for (size_t i = 0; i < COUNT(carRequired); i++)
        pbAddUint(&car, carRequired[i], 0);
    pbSetString(&car, C_TITLE, "");
    pbSub(req, 8, &given);
    pbMerge(&car, &given);
    pbFree(&given);
    uint64_t region = pbUint(&car, 3, 1), level = pbUint(&car, C_GHOST_LEVEL, 1);
    pbSetUint(&car, 1, carId);
    pbSetUint(&car, 2, userId);
    pbSetUint(&car, 3, region ? region : 1);
    pbSetUint(&car, C_PLAYED_AT, pbUint(req, 11, now()));
    pbSetUint(&car, C_GHOST_LEVEL, level ? level : 1);
    if (!*pbString(&car, C_COUNTRY, text, sizeof(text)))
        pbSetString(&car, C_COUNTRY, "JPN");
    // view, transmission, no retire, meter 0, navigation map, volume 2, bgm 0,
    // nameplate 0, terminal background 0
    pbAddUint(&setting, 1, carId);
    pbAddUint(&setting, 2, 1);
    pbAddUint(&setting, 3, pbUint(req, 9, 0));
    pbAddUint(&setting, 4, 0);
    pbAddUint(&setting, 5, 0);
    pbAddUint(&setting, 6, 1);
    pbAddUint(&setting, 7, 2);
    pbAddUint(&setting, 8, 0);
    pbAddUint(&setting, 9, 0);
    pbAddUint(&setting, 10, 0);
    pbAddUint(&setting, 13, 0);
    pbSetMsg(&record, 2, &car);
    for (size_t i = 0; i < COUNT(recordRequired); i++)
        pbAddUint(&record, recordRequired[i], 0);
    pbSetMsg(&record, R_SETTING, &setting);
    pbSetUint(&record, R_RG_STAMP, 1);
    save("cars", carId, &record);
    carFirst(&user, carId);
    save("users", userId, &user);
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT5 server: user %lu: new car %lu\n", userId, carId);
    pbAddUint(res, 1, ERR_SUCCESS);
    pbSetString(res, 2, pbString(&user, 3, text, sizeof(text)));
    pbAddUint(res, 5, userId);
    pbAddUint(res, 6, carId);
    pbAddUint(res, 16, 1); // the ghost battle stamp
    pbFree(&user);
    pbFree(&car);
    pbFree(&setting);
    pbFree(&record);
}

static void loadCar(const PbMsg *req, PbMsg *res)
{
    PbMsg record;
    if (!load("cars", pbUint(req, 1, 0), &record))
    {
        pbAddUint(res, 1, ERR_NOT_FOUND);
        return;
    }
    pbAddUint(res, 1, ERR_SUCCESS);
    for (size_t i = 0; i < record.count; i++)
        pbAddField(res, &record.fields[i]);
    pbFree(&record);
}

// The user owning a car record: its id, 0 when none.
static unsigned long carUser(const PbMsg *record, PbMsg *user)
{
    PbMsg car = {0};
    pbSub(record, 2, &car);
    unsigned long id = pbUint(&car, 2, 0);
    pbFree(&car);
    memset(user, 0, sizeof(*user));
    return id && load("users", id, user) ? id : 0;
}

// The request's items (field) added to the car's owned items.
static void addItems(PbMsg *record, const PbMsg *req, uint32_t field)
{
    for (size_t i = 0; i < req->count; i++)
        if (req->fields[i].field == field)
        {
            PbField f = req->fields[i];
            f.field = R_ITEMS;
            pbAddField(record, &f);
        }
}

static void updateCar(const PbMsg *req, PbMsg *res)
{
    PbMsg record, user, sub = {0};
    unsigned long carId = pbUint(req, 1, 0);

    if (!carId || !load("cars", carId, &record))
    {
        pbAddUint(res, 1, carId ? ERR_NOT_FOUND : ERR_REQUEST);
        return;
    }
    if (pbSub(req, 2, &sub))
        mergeSub(&record, 2, &sub);
    pbFree(&sub);
    if (pbSub(req, 5, &sub))
        mergeSub(&record, R_SETTING, &sub);
    pbFree(&sub);
    addItems(&record, req, 4);
    if (pbHas(req, 10))
        pbSetUint(&record, R_AURA_AUTO, pbUint(req, 10, 0));
    if (pbHas(req, 12))
        pbSetUint(&record, R_RG_STAMP, pbUint(req, 12, 0));
    unsigned long userId = carUser(&record, &user);
    if (userId)
    {
        updateUser(&user, req, 8, 3, 0, 0);
        if (pbUint(req, 9, 0) == 1)
        {
            // deleted: out of the user's cars
            size_t n;
            uint64_t *order = uints(&user, 7, &n);
            pbRemove(&user, 7);
            for (size_t i = 0; i < n; i++)
                if (order[i] != carId)
                    pbAddUint(&user, 7, order[i]);
            free(order);
        }
        save("users", userId, &user);
        pbFree(&user);
    }
    save("cars", carId, &record);
    pbFree(&record);
    pbAddUint(res, 1, ERR_SUCCESS);
}

static void renameCar(const PbMsg *req, PbMsg *res)
{
    PbMsg record, name = {0};
    char text[64];
    unsigned long carId = pbUint(req, 1, 0);

    if (!carId || !pbHas(req, 2))
    {
        pbAddUint(res, 1, ERR_REQUEST);
        return;
    }
    if (!load("cars", carId, &record))
    {
        pbAddUint(res, 1, ERR_NOT_FOUND);
        return;
    }
    pbSetString(&name, 4, pbString(req, 2, text, sizeof(text)));
    mergeSub(&record, 2, &name);
    save("cars", carId, &record);
    pbFree(&name);
    pbFree(&record);
    pbAddUint(res, 1, ERR_SUCCESS);
}

static void saveScreenshot(const PbMsg *req)
{
    PbMsg record;
    unsigned long carId = pbUint(req, 1, 0);
    if (!carId || !load("cars", carId, &record))
        return;
    pbSetUint(&record, R_SCREENSHOTS, pbUint(&record, R_SCREENSHOTS, 0) + 1);
    save("cars", carId, &record);
    pbFree(&record);
}

// ---------------------------------------------------------------------------
// Time attack records.

typedef struct
{
    unsigned long id;
    PbMsg msg;
} Record;

static int byTime(const void *a, const void *b)
{
    uint64_t x = pbUint(&((const Record *)a)->msg, 4, UINT64_MAX), y = pbUint(&((const Record *)b)->msg, 4, UINT64_MAX);
    return x < y ? -1 : x > y;
}

// The records of a course (every course when course < 0), fastest first.
static Record *taRecords(long course, size_t *n)
{
    size_t count;
    unsigned long *list = ids("ta", &count);
    Record *records = calloc(count ? count : 1, sizeof(*records));

    *n = 0;
    for (size_t i = 0; records && i < count; i++)
    {
        Record *r = &records[*n];
        if (!load("ta", list[i], &r->msg))
            continue;
        if (course >= 0 && pbUint(&r->msg, 3, UINT64_MAX) != (uint64_t)course)
        {
            pbFree(&r->msg);
            continue;
        }
        r->id = list[i];
        (*n)++;
    }
    free(list);
    if (*n)
        qsort(records, *n, sizeof(*records), byTime);
    return records;
}

static void freeRecords(Record *records, size_t n)
{
    for (size_t i = 0; i < n; i++)
        pbFree(&records[i].msg);
    free(records);
}

static void saveTimeAttack(unsigned long carId, const PbMsg *record, const PbMsg *ta)
{
    if (!pbHas(ta, 1) || !pbHas(ta, 2))
        return;
    uint64_t time = pbUint(ta, 1, 0), course = pbUint(ta, 2, 0);
    size_t n;
    Record *records = taRecords((long)course, &n);
    unsigned long id = 0;
    for (size_t i = 0; i < n; i++)
        if (pbUint(&records[i].msg, 1, 0) == carId)
        {
            if (pbUint(&records[i].msg, 4, UINT64_MAX) <= time)
            {
                freeRecords(records, n);
                return;
            }
            id = records[i].id;
        }
    freeRecords(records, n);
    if (!id)
        id = newId("ta");
    PbMsg car = {0}, best = {0};
    pbSub(record, 2, &car);
    pbAddUint(&best, 1, carId);
    pbAddUint(&best, 2, pbUint(&car, 6, 0));
    pbAddUint(&best, 3, course);
    pbAddUint(&best, 4, time);
    for (uint32_t s = 0; s < 7; s++)
        pbAddUint(&best, 5 + s, pbUint(ta, 3 + s, 0));
    pbAddUint(&best, 12, pbUint(&car, C_TUNE_POWER, 0));
    pbAddUint(&best, 13, pbUint(&car, C_TUNE_POWER + 1, 0));
    save("ta", id, &best);
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT5 server: car %lu: time attack course %llu best %llu\n", carId,
                (unsigned long long)course, (unsigned long long)time);
    pbFree(&car);
    pbFree(&best);
}

static void loadTimeAttackRecord(const PbMsg *req, PbMsg *res)
{
    size_t n, shown = 0;
    Record *records = taRecords((long)pbUint(req, 3, 0), &n);
    int hasModel = pbHas(req, 2), hasCar = pbHas(req, 1);
    uint64_t model = pbUint(req, 2, 0), car = pbUint(req, 1, 0);

    pbAddUint(res, 1, ERR_SUCCESS);
    for (size_t i = 0; i < n && i < 100; i++)
        pbAddUint(res, 2, pbUint(&records[i].msg, 4, 0));
    for (size_t i = 0; i < n && shown < 100; i++)
        if (hasModel && pbUint(&records[i].msg, 2, 0) == model)
        {
            pbAddUint(res, 3, pbUint(&records[i].msg, 4, 0));
            shown++;
        }
    for (size_t i = 0; hasCar && i < n; i++)
        if (pbUint(&records[i].msg, 1, 0) == car)
        {
            pbAddUint(res, 4, pbUint(&records[i].msg, 4, 0));
            for (uint32_t s = 0; s < 7; s++)
                pbAddUint(res, 5 + s, pbUint(&records[i].msg, 5 + s, 0));
            break;
        }
    freeRecords(records, n);
}

static void loadGameHistory(const PbMsg *req, PbMsg *res)
{
    int hasCar = pbHas(req, 1);
    uint64_t carId = pbUint(req, 1, 0);
    PbMsg record = {0};
    size_t n;
    Record *mine = taRecords(-1, &n);

    if (hasCar)
        load("cars", carId, &record);
    pbAddUint(res, 1, ERR_SUCCESS);
    for (size_t i = 0; hasCar && i < n; i++)
    {
        if (pbUint(&mine[i].msg, 1, 0) != carId)
            continue;
        size_t m, wholeRank = 0, modelRank = 0, models = 0;
        uint64_t model = pbUint(&mine[i].msg, 2, 0);
        Record *course = taRecords((long)pbUint(&mine[i].msg, 3, 0), &m);
        for (size_t j = 0; j < m; j++)
        {
            int sameModel = pbUint(&course[j].msg, 2, 0) == model;
            models += sameModel;
            if (pbUint(&course[j].msg, 1, 0) == carId)
            {
                wholeRank = j + 1;
                modelRank = models;
            }
        }
        PbMsg entry = {0};
        pbAddUint(&entry, 1, pbUint(&mine[i].msg, 3, 0));
        pbAddUint(&entry, 2, pbUint(&mine[i].msg, 4, 0));
        pbAddUint(&entry, 3, pbUint(&mine[i].msg, 12, 0));
        pbAddUint(&entry, 4, pbUint(&mine[i].msg, 13, 0));
        pbAddUint(&entry, 5, wholeRank);
        pbAddUint(&entry, 6, m);
        pbAddUint(&entry, 7, modelRank);
        pbAddUint(&entry, 8, models);
        pbAddMsg(res, 2, &entry);
        pbFree(&entry);
        freeRecords(course, m);
    }
    freeRecords(mine, n);
    // the ghost battle play and win counts follow the versus counters
    pbAddUint(res, 3, now());
    pbAddUint(res, 5, pbUint(&record, R_VS + 10, 0));
    pbAddUint(res, 6, pbUint(&record, R_VS + 11, 0));
    pbAddUint(res, 7, 0);
    pbFree(&record);
}

// ---------------------------------------------------------------------------
// Game results.

static uint64_t ghostLevel(uint64_t tune)
{
    static const uint64_t steps[] = {5, 10, 15, 20, 26, 28, 30, 31, 33};
    for (size_t i = 0; i < COUNT(steps); i++)
        if (tune <= steps[i])
            return i + 1;
    return 10;
}

static void saveGameResult(const PbMsg *req, PbMsg *res)
{
    PbMsg record, car = {0}, sub = {0}, user;
    unsigned long carId = pbUint(req, 1, 0);

    if (!carId || !load("cars", carId, &record))
    {
        pbAddUint(res, 1, carId ? ERR_NOT_FOUND : ERR_REQUEST);
        return;
    }
    int finished = pbUint(req, 5, 0) != 1 && pbUint(req, 6, 0) != 1;
    pbSub(req, 7, &car);
    if (pbHas(req, 3))
        pbSetUint(&car, C_PLAYED_AT, pbUint(req, 3, 0));
    pbSetUint(&car, C_GHOST_LEVEL, ghostLevel(pbUint(&car, C_TUNE_POWER, 0) + pbUint(&car, C_TUNE_POWER + 1, 0)));
    if (pbHas(&car, 3) && pbUint(&car, 3, 0) == 0)
        pbSetUint(&car, 3, carId % 47 + 1);
    mergeSub(&record, 2, &car);
    pbFree(&car);
    if (pbUint(req, 2, 0) == MODE_TIME_ATTACK && finished && pbSub(req, S_TA, &sub))
        saveTimeAttack(carId, &record, &sub);
    pbFree(&sub);
    if (pbSub(req, 8, &sub))
        mergeSub(&record, R_SETTING, &sub);
    pbFree(&sub);
    static const uint32_t counters[][2] = {{4, R_PLAY_COUNT}, {9, R_ODOMETER}, {11, R_CUSTOM_COLOR}};
    copyFields(&record, req, counters, COUNT(counters));
    addItems(&record, req, 13);
    // story: play count, tuning points, then clear bits.. consecutive wins,
    // their maximum
    if (finished && pbSub(req, S_ST, &sub))
    {
        static const uint32_t st[][2] = {{1, R_ST},     {2, 3},          {3, R_ST + 1}, {4, R_ST + 2},
                                         {5, R_ST + 3}, {6, R_ST + 4},   {7, R_ST + 5}, {8, R_ST + 6}};
        copyFields(&record, &sub, st, COUNT(st));
        if (pbUint(&record, R_ST + 6, 0) > pbUint(&record, R_ST + 7, 0))
            pbSetUint(&record, R_ST + 7, pbUint(&record, R_ST + 6, 0));
    }
    pbFree(&sub);
    // versus: play count, bursts, stars (their maximum), the medals
    if (pbUint(req, 5, 0) != 1 && pbSub(req, S_VS, &sub))
    {
        static const uint32_t vs[][2] = {{6, R_VS},      {7, R_VS + 1},  {8, R_VS + 2},  {9, R_VS + 4}, {10, R_VS + 5},
                                         {11, R_VS + 6}, {12, R_VS + 7}, {13, R_VS + 8}, {14, R_VS + 9}};
        copyFields(&record, &sub, vs, COUNT(vs));
        if (pbUint(&record, R_VS + 2, 0) > pbUint(&record, R_VS + 3, 0))
            pbSetUint(&record, R_VS + 3, pbUint(&record, R_VS + 2, 0));
    }
    pbFree(&sub);
    if (pbSub(req, S_RG, &sub))
        copyFields(&record, &sub, rgMap, COUNT(rgMap));
    pbFree(&sub);
    unsigned long userId = carUser(&record, &user);
    if (userId)
    {
        updateUser(&user, req, S_GOLD, 10, 12, 0);
        carFirst(&user, carId);
        save("users", userId, &user);
        pbFree(&user);
    }
    save("cars", carId, &record);
    pbFree(&record);
    pbAddUint(res, 1, ERR_SUCCESS);
}

// ---------------------------------------------------------------------------
// Resources.

// A Ranking.Entry of a car.
static void rankingEntry(PbMsg *list, unsigned long carId, const PbMsg *car, uint64_t result, uint64_t power,
                         uint64_t handling)
{
    PbMsg e = {0};
    char text[64];
    uint64_t level = pbUint(car, C_LEVEL, 0), region = pbUint(car, 3, 1);
    pbAddUint(&e, 1, level);
    pbAddUint(&e, 2, result);
    pbAddUint(&e, 3, carId);
    pbSetString(&e, 4, pbString(car, 4, text, sizeof(text)));
    pbAddUint(&e, 5, region ? region : 1);
    pbAddUint(&e, 6, pbUint(car, 6, 0));
    pbAddUint(&e, 7, pbUint(car, 7, 0));
    pbAddUint(&e, 8, pbUint(car, 8, 0));
    pbAddUint(&e, 9, power);
    pbAddUint(&e, 10, handling);
    pbSetString(&e, 11, pbString(car, C_TITLE, text, sizeof(text)));
    pbAddUint(&e, 12, level);
    pbAddMsg(list, 2, &e);
    pbFree(&e);
}

static int carRecord(unsigned long id, PbMsg *car)
{
    PbMsg record;
    memset(car, 0, sizeof(*car));
    if (!load("cars", id, &record))
        return 0;
    int ok = pbSub(&record, 2, car);
    pbFree(&record);
    return ok;
}

typedef struct
{
    unsigned long id;
    uint64_t value;
} Best;

static int byValue(const void *a, const void *b)
{
    uint64_t x = ((const Best *)a)->value, y = ((const Best *)b)->value;
    return x > y ? -1 : x < y;
}

static void ranking(PbMsg *res)
{
    for (long course = 0; course < TA_COURSES; course++)
    {
        PbMsg list = {0};
        size_t n;
        Record *records = taRecords(course, &n);
        pbAddUint(&list, 1, course);
        for (size_t i = 0; i < n && i < 10; i++)
        {
            PbMsg car;
            unsigned long carId = pbUint(&records[i].msg, 1, 0);
            if (carRecord(carId, &car))
                rankingEntry(&list, carId, &car, pbUint(&records[i].msg, 4, 0), pbUint(&records[i].msg, 12, 0),
                             pbUint(&records[i].msg, 13, 0));
            pbFree(&car);
        }
        freeRecords(records, n);
        pbAddMsg(res, 1, &list);
        pbFree(&list);
    }
    static const uint32_t kinds[][2] = {{RANKING_VS_STAR, R_VS + 2}, {RANKING_TROPHY, R_MAXI_COIN}};
    size_t n;
    unsigned long *cars = ids("cars", &n);
    Best *best = calloc(n ? n : 1, sizeof(*best));
    for (size_t k = 0; best && k < COUNT(kinds); k++)
    {
        size_t m = 0;
        PbMsg list = {0};
        for (size_t i = 0; i < n; i++)
        {
            PbMsg record;
            if (!load("cars", cars[i], &record))
                continue;
            uint64_t v = pbUint(&record, kinds[k][1], 0);
            if (v)
                best[m++] = (Best){cars[i], v};
            pbFree(&record);
        }
        qsort(best, m, sizeof(*best), byValue);
        pbAddUint(&list, 1, kinds[k][0]);
        for (size_t i = 0; i < m && i < 20; i++)
        {
            PbMsg car;
            if (carRecord(best[i].id, &car))
                rankingEntry(&list, best[i].id, &car, best[i].value, pbUint(&car, C_TUNE_POWER, 0),
                             pbUint(&car, C_TUNE_POWER + 1, 0));
            pbFree(&car);
        }
        pbAddMsg(res, 1, &list);
        pbFree(&list);
    }
    free(best);
    free(cars);
}

// A %XX / + encoded query value of key, "" when absent; 1 when present.
static int queryParam(const char *query, const char *key, char *out, size_t size)
{
    size_t k = strlen(key);
    const char *p = query;
    *out = 0;
    while (p && *p)
    {
        if (!strncmp(p, key, k) && p[k] == '=')
        {
            size_t n = 0;
            for (p += k + 1; *p && *p != '&' && n + 1 < size; p++)
            {
                unsigned c;
                if (*p == '%' && p[1] && p[2] && sscanf(p + 1, "%2x", &c) == 1)
                {
                    out[n++] = (char)c;
                    p += 2;
                }
                else
                    out[n++] = *p == '+' ? ' ' : *p;
            }
            out[n] = 0;
            return 1;
        }
        p = strchr(p, '&');
        if (p)
            p++;
    }
    return 0;
}

static void carSummary(const char *query, PbMsg *res)
{
    char text[128], name[128];
    int hasName = queryParam(query, "name", name, sizeof(name)) && *name;
    int byPlace = queryParam(query, "last_played_place_id", text, sizeof(text));
    size_t limit = queryParam(query, "limit", text, sizeof(text)) ? (size_t)atoi(text) : 10, n, found = 0;
    unsigned long *cars = ids("cars", &n);
    PbMsg list = {0};

    for (size_t i = 0; i < n && found < limit; i++)
    {
        PbMsg car;
        if (!carRecord(cars[i], &car))
            continue;
        pbString(&car, 4, text, sizeof(text));
        size_t t = strlen(text), m = strlen(name);
        int hit = byPlace || (hasName && t >= m && (!strncmp(text, name, m) || !strcmp(text + t - m, name)));
        if (hit)
        {
            if (pbUint(&car, 3, 0) == 0)
                pbSetUint(&car, 3, cars[i] % 47 + 1);
            pbAddMsg(&list, 2, &car);
            found++;
        }
        pbFree(&car);
    }
    free(cars);
    pbAddUint(res, 1, found);
    for (size_t i = 0; i < list.count; i++)
        pbAddField(res, &list.fields[i]);
    pbFree(&list);
}

static void resource(const char *name, const char *query, PbMsg *res)
{
    if (!strcmp(name, "place_list"))
    {
        PbMsg place = {0};
        pbSetString(&place, 1, "JPN0123");
        pbSetString(&place, 2, "Arcade");
        pbAddUint(&place, 3, 1);
        pbSetString(&place, 4, "JPN");
        pbAddMsg(res, 1, &place);
        pbFree(&place);
    }
    else if (!strcmp(name, "ranking"))
        ranking(res);
    else if (!strcmp(name, "crown_list"))
    {
        // no crown holder: a placeholder crown per run area, 0..13 and 18
        for (unsigned area = 0; area <= 14; area++)
        {
            unsigned a = area == 14 ? 18 : area;
            PbMsg crown = {0};
            pbAddUint(&crown, 1, 999999999u - a);
            pbAddUint(&crown, 2, a);
            pbAddUint(&crown, 3, 0);
            pbAddMsg(res, 1, &crown);
            pbFree(&crown);
        }
    }
    else if (!strcmp(name, "car_summary"))
        carSummary(query, res);
    else if (!strcmp(name, "file_list"))
        pbAddUint(res, 2, 3600); // no file, the next check in an hour
    else
        fprintf(stderr, "WMMT5 server: resource %s?%s: not implemented\n", name, query);
}

// ---------------------------------------------------------------------------

static void method(const char *name, const PbMsg *req, PbMsg *res)
{
    static const struct
    {
        const char *name;
        uint32_t fields[6][2]; // fixed answers: field, value (ended by field 0)
    } fixed[] = {
        {"register_system_stats", {{1, 0}}},
        {"update_user_lock", {{1, 0}}},
        {"load_shop_information", {{1, 0}, {2, 1}, {4, 5}, {5, 1}}},
        {"load_ghost_battle_info", {{1, 0}, {7, 0}}},
        {"load_ghost_competition_info", {{1, 0}, {3, 1}}},
        {"update_user_session", {{1, 0}}},
        {"submit_client_log", {{1, 0}}},
        {"save_bookmarks", {{1, 0}}},
        {"load_bookmarks", {{1, 0}}},
        {"load_unreceived_user_items", {{1, 0}}},
        {"load_car_campaign_info", {{1, 0}, {2, 1}, {3, 0}, {4, 0}, {5, 0}}},
        {"start_transfer", {{1, 0}, {2, 0}}},
        {"load_scratch_information", {{1, 0}, {3, 0}, {4, 0}}},
        {"save_scratch_sheet", {{1, 0}, {3, 0}, {4, 0}}},
        {"grant_car_right", {{1, 0}}},
        {"accept_car_campaign", {{1, 0}}},
        {"save_car_campaign_info", {{1, 0}}},
        {"save_charge", {{1, 0}}},
        {"save_client_report", {{1, 0}}},
    };

    for (size_t i = 0; i < COUNT(fixed); i++)
        if (!strcmp(name, fixed[i].name))
        {
            for (size_t f = 0; f < 6 && fixed[i].fields[f][0]; f++)
                pbAddUint(res, fixed[i].fields[f][0], fixed[i].fields[f][1]);
            return;
        }
    if (!strcmp(name, "register_system_info"))
        registerSystemInfo(req, res);
    else if (!strcmp(name, "ping"))
    {
        pbAddUint(res, 1, ERR_SUCCESS);
        pbAddUint(res, 2, pbUint(req, 1, 1));
    }
    else if (!strcmp(name, "load_user"))
        loadUser(req, res);
    else if (!strcmp(name, "create_user"))
        createUser(req, res);
    else if (!strcmp(name, "load_drive_information"))
    {
        pbAddUint(res, 1, ERR_SUCCESS);
        transferNotice(res, 4);
    }
    else if (!strcmp(name, "load_terminal_information"))
    {
        // maxi gold receivable, nothing transferred, no prize waiting, the
        // free scratch card used
        pbAddUint(res, 1, ERR_SUCCESS);
        pbAddUint(res, 9, 1);
        transferNotice(res, 14);
        pbAddUint(res, 16, 0);
        pbAddUint(res, 17, 0);
        pbAddUint(res, 18, 1);
    }
    else if (!strcmp(name, "load_game_history"))
        loadGameHistory(req, res);
    else if (!strcmp(name, "load_time_attack_record"))
        loadTimeAttackRecord(req, res);
    else if (!strcmp(name, "receive_maxi_gold"))
        receiveMaxiGold(req, res);
    else if (!strcmp(name, "save_terminal_result"))
    {
        saveTerminalResult(req);
        pbAddUint(res, 1, ERR_SUCCESS);
    }
    else if (!strcmp(name, "rename_car"))
        renameCar(req, res);
    else if (!strcmp(name, "ask_access_code"))
        askAccessCode(req, res);
    else if (!strcmp(name, "save_screenshot"))
    {
        saveScreenshot(req);
        pbAddUint(res, 1, ERR_SUCCESS);
    }
    else if (!strcmp(name, "create_car"))
        createCar(req, res);
    else if (!strcmp(name, "load_car"))
        loadCar(req, res);
    else if (!strcmp(name, "update_car"))
        updateCar(req, res);
    else if (!strcmp(name, "save_game_result"))
        saveGameResult(req, res);
    else
    {
        fprintf(stderr, "WMMT5 server: %s: not implemented\n", name);
        pbAddUint(res, 1, ERR_SUCCESS);
    }
}

// The request's name after the marker ("/method/", "/resource/"), its query
// after '?'.
static int requestName(const char *path, const char *marker, char *name, size_t size, const char **query)
{
    const char *p = strstr(path, marker);
    if (!p)
        return 0;
    p += strlen(marker);
    size_t n = strcspn(p, "?");
    snprintf(name, size, "%.*s", (int)n, p);
    *query = p[n] == '?' ? p + n + 1 : "";
    return 1;
}

// The fields of a message on stderr (strings and sub-messages shown as
// bytes, their length).
static void dump(const char *what, const PbMsg *msg)
{
    fprintf(stderr, "WMMT5 server: %s:", what);
    for (size_t i = 0; i < msg->count; i++)
    {
        const PbField *f = &msg->fields[i];
        if (f->wire == PB_BYTES)
        {
            fprintf(stderr, " %u:\"", f->field);
            for (size_t j = 0; j < f->length && j < 48; j++)
                fputc(f->data[j] >= 32 && f->data[j] < 127 ? f->data[j] : '.', stderr);
            fprintf(stderr, "\"(%zu)", f->length);
        }
        else
            fprintf(stderr, " %u:%llu", f->field, (unsigned long long)f->value);
    }
    fputc('\n', stderr);
}

void wmmt5RpcHandle(const char *path, const uint8_t *body, size_t length, Wmmt4Response *response)
{
    char name[96];
    const char *query;
    PbMsg req = {0}, res = {0};

    pthread_mutex_lock(&lock);
    if (requestName(path, "/method/", name, sizeof(name), &query))
    {
        pbParse(&req, body, length);
        method(name, &req, &res);
        if (wmmt4ServerTrace)
        {
            dump(name, &req);
            dump("  answer", &res);
        }
        answer(response, &res);
    }
    else if (requestName(path, "/resource/", name, sizeof(name), &query))
    {
        resource(name, query, &res);
        answer(response, &res);
    }
    else
        response->status = 404;
    if (wmmt4ServerTrace)
        fprintf(stderr, "WMMT5 server: %s (%zu bytes) -> %d, %zu bytes\n", path, length, response->status,
                response->length);
    pbFree(&req);
    pbFree(&res);
    pthread_mutex_unlock(&lock);
}
