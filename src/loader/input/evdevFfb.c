#ifdef __linux__
// Force feedback on one evdev device (see evdevFfb.h).

#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "evdevFfb.h"
#include "../log/log.h"

typedef struct
{
    struct ff_effect effect;
    int level; // last level sent, -1: none
} Effect;

static const char *ffbWho = "FFB";
static int ffbFd = -1;
static int ffbConstant; // else rumble
static int ffbHasSpring, ffbHasFriction, ffbHasDamper, ffbHasAutocenter;
static Effect constantEffect, springEffect, damperEffect;
static int autocenterLevel = -1;

#define test_bit(bit, array) ((array[(bit) / (8 * sizeof(long))] >> ((bit) % (8 * sizeof(long)))) & 1)

static void initEffect(Effect *e, uint16_t type)
{
    memset(e, 0, sizeof(*e));
    e->effect.type = type;
    e->effect.id = -1;
    e->level = -1;
}

static void sendEvent(uint16_t code, int32_t value)
{
    struct input_event ev = {.type = EV_FF, .code = code, .value = value};
    if (write(ffbFd, &ev, sizeof(ev)) < 0)
        log_error("%s: force feedback event %u failed", ffbWho, code);
}

static void setAutocenter(int level)
{
    if (ffbFd < 0 || !ffbHasAutocenter || level == autocenterLevel)
        return;
    sendEvent(FF_AUTOCENTER, level);
    autocenterLevel = level;
}

// Opens a device for force feedback if it has any: 1 if it does.
static int ffbOpenPath(const char *path)
{
    unsigned long features[(FF_CNT + 8 * sizeof(long) - 1) / (8 * sizeof(long))] = {0};
    int fd = open(path, O_RDWR | O_CLOEXEC);

    if (fd < 0)
        return 0;
    if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(features)), features) < 0 ||
        (!test_bit(FF_CONSTANT, features) && !test_bit(FF_RUMBLE, features)))
    {
        close(fd);
        return 0;
    }
    ffbFd = fd;
    ffbConstant = test_bit(FF_CONSTANT, features);
    ffbHasSpring = ffbConstant && test_bit(FF_SPRING, features);
    ffbHasFriction = ffbConstant && test_bit(FF_FRICTION, features);
    ffbHasDamper = ffbConstant && test_bit(FF_DAMPER, features);
    ffbHasAutocenter = ffbConstant && test_bit(FF_AUTOCENTER, features);

    if (test_bit(FF_GAIN, features))
        sendEvent(FF_GAIN, 0xffff);
    // The game centers the wheel itself.
    setAutocenter(0);

    if (ffbConstant)
    {
        initEffect(&constantEffect, FF_CONSTANT);
        constantEffect.effect.direction = 0x4000; // positive levels pull to the left
    }
    else
        initEffect(&constantEffect, FF_RUMBLE);
    initEffect(&springEffect, FF_SPRING);
    initEffect(&damperEffect, ffbHasDamper ? FF_DAMPER : FF_FRICTION);
    printf("%s: force feedback (%s%s%s) on %s\n", ffbWho, ffbConstant ? "wheel" : "rumble",
           ffbHasSpring ? ", spring" : "", ffbHasDamper ? ", damper" : ffbHasFriction ? ", friction" : "", path);
    return 1;
}

int evdevFfbOpenPath(const char *who, const char *path)
{
    ffbWho = who;
    return ffbFd >= 0 || ffbOpenPath(path);
}

int evdevFfbWheel(void)
{
    return ffbFd >= 0 && ffbConstant;
}

// Uploaded once, then updated in place; plays until replaced.
static void upload(Effect *e, int level)
{
    int first = e->effect.id < 0;

    if (ioctl(ffbFd, EVIOCSFF, &e->effect) < 0)
    {
        log_error("%s: force feedback upload failed", ffbWho);
        close(ffbFd);
        ffbFd = -1;
        return;
    }
    if (first)
        sendEvent((uint16_t)e->effect.id, 1);
    e->level = level;
}

static float clamp(float v, float lo, float hi)
{
    if (!(v >= lo)) // NaN too
        return v > hi ? hi : lo;
    return v > hi ? hi : v;
}

void evdevFfbConstant(float force)
{
    if (ffbFd < 0)
        return;
    int level = (int)lrintf(clamp(force, -1.0f, 1.0f) * 0x7fff);
    if (level == constantEffect.level)
        return;

    if (ffbConstant)
        constantEffect.effect.u.constant.level = (int16_t)level;
    else
    {
        // A pad: the weak (high frequency) motor, as hard as the wheel is pushed.
        constantEffect.effect.u.rumble.strong_magnitude = 0;
        constantEffect.effect.u.rumble.weak_magnitude = (uint16_t)(abs(level) * 2);
    }
    upload(&constantEffect, level);
}

// A condition (spring, damper) centered, as hard both ways.
static void condition(Effect *e, int level)
{
    for (int axis = 0; axis < 2; axis++)
    {
        struct ff_condition_effect *c = &e->effect.u.condition[axis];
        c->right_coeff = c->left_coeff = (int16_t)level;
        c->right_saturation = c->left_saturation = 0xffff;
        c->deadband = 0;
        c->center = 0;
    }
    upload(e, level);
}

void evdevFfbSpring(float strength)
{
    if (ffbFd < 0 || !ffbConstant)
        return;
    int level = (int)lrintf(clamp(strength, 0.0f, 1.0f) * 0x7fff);
    if (ffbHasSpring)
    {
        if (level != springEffect.level)
            condition(&springEffect, level);
    }
    else
        setAutocenter(level * 2);
}

void evdevFfbDamper(float strength)
{
    if (ffbFd < 0 || !(ffbHasDamper || ffbHasFriction))
        return;
    int level = (int)lrintf(clamp(strength, 0.0f, 1.0f) * 0x7fff);
    if (level != damperEffect.level)
        condition(&damperEffect, level);
}
#endif

#ifndef __linux__
// No evdev on Windows: the constant force goes to the first wheel's SDL
// haptics (the loader's own SDL 3, which the games' SDL 1.2 does not shadow
// here). The other effects stay no-ops, no game of this port asks for them.

#include <SDL3/SDL.h>

#include "evdevFfb.h"
#include "../log/log.h"

// A force changing by less is not worth a USB transfer on the game's thread.
#define FFB_MIN_CHANGE 164
#define FFB_MIN_INTERVAL_MS 10
// The wheel keeps a running force after the process is gone (Esc is an
// ExitProcess, and a crash asks nothing either): it only lasts this long unless
// renewed, so that it dies with the game.
#define FFB_LENGTH_MS 500
#define FFB_RENEW_MS 100

static SDL_Haptic *haptic;
static SDL_HapticEffect effect;
static int effectId = -1;
static Uint64 lastUpdate, lastRun;

int evdevFfbOpen(const char *who)
{
    int count = 0;
    SDL_JoystickID *ids;

    if (haptic)
        return 1;
    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC))
    {
        log_warn("%s: no SDL haptics: %s", who, SDL_GetError());
        return 0;
    }
    ids = SDL_GetJoysticks(&count);
    for (int i = 0; ids && i < count && !haptic; i++)
    {
        SDL_Joystick *joystick;

        if (SDL_IsGamepad(ids[i]) || !(joystick = SDL_OpenJoystick(ids[i])))
            continue;
        if (SDL_IsJoystickHaptic(joystick) && (haptic = SDL_OpenHapticFromJoystick(joystick)) &&
            !(SDL_GetHapticFeatures(haptic) & SDL_HAPTIC_CONSTANT))
        {
            SDL_CloseHaptic(haptic);
            haptic = NULL;
        }
        if (haptic)
            log_info("%s: force feedback on %s", who, SDL_GetJoystickName(joystick));
        else
            SDL_CloseJoystick(joystick);
    }
    SDL_free(ids);
    return haptic != NULL;
}

int evdevFfbOpenPath(const char *who, const char *path)
{
    (void)path;
    return evdevFfbOpen(who);
}

int evdevFfbWheel(void)
{
    return haptic != NULL;
}

void evdevFfbConstant(float force)
{
    Uint64 now;
    int level;

    if (!haptic)
        return;
    force = force > 1.f ? 1.f : force < -1.f ? -1.f : force;
    // A positive level pulls the wheel to the left (measured on a Logitech
    // PRO Racing Wheel), as this function's force.
    level = (int)(force * 32767);
    now = SDL_GetTicks();
    if (effectId < 0)
    {
        effect.type = SDL_HAPTIC_CONSTANT;
        effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
        effect.constant.direction.dir[0] = 1;
        effect.constant.length = FFB_LENGTH_MS;
        effect.constant.level = (Sint16)level;
        effectId = SDL_CreateHapticEffect(haptic, &effect);
        if (effectId < 0 || !SDL_RunHapticEffect(haptic, effectId, 1))
        {
            log_warn("Force feedback: no constant force: %s", SDL_GetError());
            SDL_CloseHaptic(haptic);
            haptic = NULL;
        }
        lastUpdate = lastRun = now;
        return;
    }
    if (level != effect.constant.level && now - lastUpdate >= FFB_MIN_INTERVAL_MS &&
        (level == 0 || SDL_abs(level - effect.constant.level) >= FFB_MIN_CHANGE))
    {
        effect.constant.level = (Sint16)level;
        SDL_UpdateHapticEffect(haptic, effectId, &effect);
        lastUpdate = now;
    }
    if (effect.constant.level != 0 && now - lastRun >= FFB_RENEW_MS)
    {
        SDL_RunHapticEffect(haptic, effectId, 1);
        lastRun = now;
    }
}

void evdevFfbSpring(float strength)
{
    (void)strength;
}

void evdevFfbDamper(float strength)
{
    (void)strength;
}
#endif
