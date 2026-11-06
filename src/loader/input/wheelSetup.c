// Finds a wheel's axes and force direction from the user moving it, and keeps
// them for all the games (see wheelSetup.h).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "wheelSetup.h"
#include "../config/iniParser.h"
#include "../log/log.h"
#include "../mainShared.h"

#define WHEEL_MAX_AXES 16
// How far an axis must move to be the one asked for: a stiff brake pedal may
// give little.
#define WHEEL_MIN_TRAVEL 3000
#define WHEEL_TRIES 3
#define PULSE_LEVEL 8000
#define PULSE_MS 400
#define PULSE_MIN_TRAVEL 300

// The user's wheel.ini, in the directory of the loader's other files: path,
// and in dir, if given, its directory.
static const char *wheelIniPath(char *path, size_t size, char *dir, size_t dirSize)
{
    char base[512];
#ifdef _WIN32
    const char *local = getenv("LOCALAPPDATA");
    snprintf(base, sizeof(base), "%s\\linuxloader", local && local[0] ? local : ".");
    snprintf(path, size, "%s\\wheel.ini", base);
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(base, sizeof(base), "%s/linuxloader", xdg);
    else
        snprintf(base, sizeof(base), "%s/.config/linuxloader", home ? home : ".");
    snprintf(path, size, "%s/wheel.ini", base);
#endif
    if (dir)
        snprintf(dir, dirSize, "%s", base);
    return path;
}

// A [Driving] action's key: with or without its player.
static const char *drivingKey(const IniConfig *ini, const char *action, char *key, size_t size)
{
    snprintf(key, size, "P1_%s", action);
    if (!iniGetValue(ini, "Driving", key))
        snprintf(key, size, "%s", action);
    return key;
}

// The first JOY0_AXIS_<n>[_INVERTED] of a binding list.
static void parseAxis(const IniConfig *ini, const char *action, int *axis, int *inverted)
{
    char key[32], copy[256];
    const char *value = iniGetValue(ini, "Driving", drivingKey(ini, action, key, sizeof(key)));

    if (!value)
        return;
    snprintf(copy, sizeof(copy), "%s", value);
    for (char *token = strtok(copy, ","); token; token = strtok(NULL, ","))
    {
        int device, n;
        while (*token == ' ')
            token++;
        if (sscanf(token, "JOY%d_AXIS_%d", &device, &n) == 2 && device == 0 && !strchr(token, '+'))
        {
            *axis = n;
            *inverted = strstr(token, "_INVERTED") != NULL;
            return;
        }
    }
}

const WheelMap *wheelMap(void)
{
    static WheelMap map = {.steerAxis = 0, .gasAxis = 3, .brakeAxis = 4};
    static int loaded;

    if (!loaded)
    {
        char global[600];
        const char *path = wheelIniPath(global, sizeof(global), NULL, 0);
        struct stat st;
        IniConfig *ini;

        // --setup-wheel's, whatever the game's folder holds: a default
        // controls.ini binds axes that are not this wheel's.
        if (stat(path, &st) != 0)
            path = controlsPath && controlsPath[0] ? controlsPath : "controls.ini";
        ini = stat(path, &st) == 0 ? iniLoad(path) : NULL;

        loaded = 1;
        if (ini)
        {
            const char *value;
            parseAxis(ini, "Steer", &map.steerAxis, &map.steerInverted);
            parseAxis(ini, "Gas", &map.gasAxis, &map.gasInverted);
            parseAxis(ini, "Brake", &map.brakeAxis, &map.brakeInverted);
            map.ffbInverted = (value = iniGetValue(ini, "Config", "Wheel_FFB_Inverted")) && atoi(value);
            map.setUp = (value = iniGetValue(ini, "Config", "Wheel_Setup")) && atoi(value);
            iniFree(ini);
        }
        else
            path = "defaults";
        log_info("Wheel: steering axis %d%s, gas %d%s, brake %d%s, force%s (%s)", map.steerAxis,
                 map.steerInverted ? " inverted" : "", map.gasAxis, map.gasInverted ? " inverted" : "", map.brakeAxis,
                 map.brakeInverted ? " inverted" : "", map.ffbInverted ? " inverted" : "", path);
    }
    return &map;
}

int wheelIsJoystick(SDL_JoystickID id)
{
    return SDL_GetJoystickTypeForID(id) == SDL_JOYSTICK_TYPE_WHEEL || (wheelMap()->setUp && !SDL_IsGamepad(id));
}

// ---------------------------------------------------------------------------
// --setup-wheel

static void waitEnter(void)
{
    char line[16];
    printf("  then press Enter... ");
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin))
        exit(1);
}

static void readAxes(SDL_Joystick *joystick, int count, int *axes)
{
    // A few updates: the last report may be on its way.
    for (int i = 0; i < 5; i++)
    {
        SDL_Delay(20);
        SDL_UpdateJoysticks();
    }
    for (int i = 0; i < count; i++)
        axes[i] = SDL_GetJoystickAxis(joystick, i);
}

// The axis the user moves, among those not taken yet. -1 if none did.
static int askAxis(SDL_Joystick *joystick, int count, const char *what, unsigned int *taken, int *inverted)
{
    int rest[WHEEL_MAX_AXES], moved[WHEEL_MAX_AXES];

    for (int attempt = 0; attempt < WHEEL_TRIES; attempt++)
    {
        int best = -1, bestTravel = WHEEL_MIN_TRAVEL - 1;

        readAxes(joystick, count, rest);
        printf("\n%s and hold,\n", what);
        waitEnter();
        readAxes(joystick, count, moved);
        for (int i = 0; i < count; i++)
        {
            int travel = abs(moved[i] - rest[i]);
            if (!(*taken & (1u << i)) && travel > bestTravel)
            {
                best = i;
                bestTravel = travel;
            }
        }
        if (best >= 0)
        {
            *taken |= 1u << best;
            *inverted = moved[best] < rest[best];
            printf("  axis %d (%d to %d)\n  Now release it,\n", best, rest[best], moved[best]);
            waitEnter();
            return best;
        }
        printf("  No new axis moved. Release it and try again,\n");
        waitEnter();
    }
    return -1;
}

// The steering's answer to a weak force, to tell which way a positive level
// pulls: 1 for the right (inverted), 0 for the left, -1 if it did not move.
static int measureForce(SDL_Joystick *joystick, int axis, int inverted)
{
    SDL_Haptic *haptic = SDL_OpenHapticFromJoystick(joystick);
    SDL_HapticEffect effect;
    int id, start, travel, result = -1;

    if (!haptic || !(SDL_GetHapticFeatures(haptic) & SDL_HAPTIC_CONSTANT))
    {
        printf("  No constant force available: %s\n", SDL_GetError());
        if (haptic)
            SDL_CloseHaptic(haptic);
        return -1;
    }
    memset(&effect, 0, sizeof(effect));
    effect.type = SDL_HAPTIC_CONSTANT;
    effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    effect.constant.direction.dir[0] = 1;
    effect.constant.length = PULSE_MS;
    effect.constant.level = PULSE_LEVEL;
    SDL_UpdateJoysticks();
    start = SDL_GetJoystickAxis(joystick, axis);
    if ((id = SDL_CreateHapticEffect(haptic, &effect)) >= 0 && SDL_RunHapticEffect(haptic, id, 1))
    {
        SDL_Delay(PULSE_MS);
        SDL_UpdateJoysticks();
        travel = SDL_GetJoystickAxis(joystick, axis) - start;
        SDL_StopHapticEffect(haptic, id);
        if (inverted)
            travel = -travel;
        if (abs(travel) >= PULSE_MIN_TRAVEL)
            result = travel > 0;
    }
    else
        printf("  The force did not start: %s\n", SDL_GetError());
    SDL_CloseHaptic(haptic);
    return result;
}

static void setAxis(IniConfig *ini, const char *action, int axis, int inverted)
{
    char value[64];

    snprintf(value, sizeof(value), "JOY0_AXIS_%d%s", axis, inverted ? "_INVERTED" : "");
    iniSetValue(ini, "Driving", action, value);
}

int wheelSetupRun(void)
{
    SDL_JoystickID *ids, wheelId = 0;
    SDL_Joystick *joystick;
    IniConfig *ini;
    struct stat st;
    char path[600], dir[512];
    int count = 0, axes, wake[WHEEL_MAX_AXES], steer, gas, brake, steerInverted, gasInverted, brakeInverted, force;
    unsigned int taken = 0;
    int moved = 0;

    wheelIniPath(path, sizeof(path), dir, sizeof(dir));
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC))
    {
        printf("Could not initialize SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Delay(300);
    SDL_UpdateJoysticks();
    ids = SDL_GetJoysticks(&count);
    for (int i = 0; ids && i < count && !wheelId; i++)
        if (SDL_GetJoystickTypeForID(ids[i]) == SDL_JOYSTICK_TYPE_WHEEL)
            wheelId = ids[i];
    for (int i = 0; ids && i < count && !wheelId; i++)
        if (!SDL_IsGamepad(ids[i]))
            wheelId = ids[i];
    SDL_free(ids);
    if (!wheelId || !(joystick = SDL_OpenJoystick(wheelId)))
    {
        printf("No wheel found: connect it, and close any game using it.\n");
        return 1;
    }
    axes = SDL_GetNumJoystickAxes(joystick);
    if (axes > WHEEL_MAX_AXES)
        axes = WHEEL_MAX_AXES;
    printf("Wheel: %s (%d axes)\n", SDL_GetJoystickName(joystick), axes);

    // A wheel only reports on changes, and its axes read 0 until then: its
    // first report moves them all.
    printf("\nTurn the wheel a little to wake it up...\n");
    for (int i = 0; i < 3000 && !moved; i++)
    {
        SDL_Delay(20);
        SDL_UpdateJoysticks();
        for (int a = 0; a < axes; a++)
            moved |= (wake[a] = SDL_GetJoystickAxis(joystick, a)) != 0;
    }
    if (!moved)
    {
        printf("It sent nothing for a minute: is it powered, and is its driver running?\n");
        return 1;
    }
    printf("\nCenter the wheel and take your feet off the pedals,\n");
    waitEnter();

    if ((steer = askAxis(joystick, axes, "Turn the wheel well to the LEFT", &taken, &steerInverted)) < 0 ||
        (gas = askAxis(joystick, axes, "Press the GAS pedal", &taken, &gasInverted)) < 0 ||
        (brake = askAxis(joystick, axes, "Press the BRAKE pedal", &taken, &brakeInverted)) < 0)
    {
        printf("\nNo axis found, nothing written. If your pedals share one axis, turn on separate axes in the\n"
               "wheel's driver.\n");
        return 1;
    }
    // Left is the lower end of a steering axis that is not inverted: askAxis
    // gave "inverted" for one that goes down, which is the right way here.
    steerInverted = !steerInverted;

    printf("\nThe wheel will now move by itself, weakly: LET GO of it,\n");
    waitEnter();
    force = measureForce(joystick, steer, steerInverted);
    if (force < 0)
        printf("  It did not move: force direction left as it was.\n");
    else
        printf("  The force is %s.\n", force ? "inverted" : "the expected way");

    // An earlier setup's force direction stays if this one could not measure it.
    ini = stat(path, &st) == 0 ? iniLoad(path) : NULL;
    if (!ini && !(ini = (IniConfig *)calloc(1, sizeof(IniConfig))))
        return 1;
    SDL_CreateDirectory(dir);
    setAxis(ini, "Steer", steer, steerInverted);
    setAxis(ini, "Gas", gas, gasInverted);
    setAxis(ini, "Brake", brake, brakeInverted);
    iniSetValue(ini, "Config", "Wheel_Setup", "1");
    if (force >= 0)
        iniSetValue(ini, "Config", "Wheel_FFB_Inverted", force ? "1" : "0");
    if (iniSave(ini, path) < 0)
    {
        printf("Could not write %s\n", path);
        iniFree(ini);
        return 1;
    }
    iniFree(ini);
    printf("\nSaved to %s: steering axis %d%s, gas %d%s, brake %d%s.\n", path, steer, steerInverted ? " inverted" : "",
           gas, gasInverted ? " inverted" : "", brake, brakeInverted ? " inverted" : "");
    return 0;
}
