#ifndef WHEEL_SETUP_H
#define WHEEL_SETUP_H

#include <SDL3/SDL.h>

// A wheel read directly with SDL's joystick functions, by the games that have
// no SDL input layer (Raw Thrills on Windows). --setup-wheel keeps its axes
// and force direction in the user's wheel.ini, for all the games; without one,
// the game folder's controls.ini gives them. In both: the first
// JOY0_AXIS_<n>[_INVERTED] of [Driving]'s Steer, Gas and Brake, and [Config]'s
// Wheel_FFB_Inverted.
//
// Its buttons are a standard pad's, whatever the game, which gives them the
// meaning it gives a gamepad's: [Buttons] in wheel.ini, a JOY0_BUTTON_<n> or
// JOY0_HAT<n>_<UP, DOWN, LEFT or RIGHT> for each (no default ones). [Config]'s
// Wheel_SDL_Mapping says the same to SDL, for the games with the SDL input
// layer: the wheel is a gamepad for them.
enum
{
    WHEEL_INPUTS = 16
};

typedef struct
{
    int hat;   // Set: index is a hat, and mask its direction (SDL_HAT_...).
    int index; // -1: none.
    int mask;
} WheelButton;

typedef struct
{
    int steerAxis, gasAxis, brakeAxis;
    int steerInverted, gasInverted, brakeInverted;
    int ffbInverted;
    int setUp; // By --setup-wheel: it may be a joystick SDL does not call a wheel.
    WheelButton buttons[WHEEL_INPUTS];
    char sdlMapping[768];
} WheelMap;

// Once, from wheel.ini, else controls.ini in the current directory (or
// --controls): call it before the game changes directory. Without either, a
// Logitech PRO Racing Wheel's.
const WheelMap *wheelMap(void);

// Whether this joystick is the wheel.
int wheelIsJoystick(SDL_JoystickID id);

// Whether the wheel holds a pad's button.
int wheelGamepadButtonHeld(SDL_Joystick *wheel, SDL_GamepadButton button);

// Tells SDL the wheel is a gamepad, for the SDL input layer.
void wheelAddGamepadMapping(void);

// --setup-wheel: asks to move each control, and writes what it finds to
// wheel.ini. 0 when done.
int wheelSetupRun(void);

#endif // WHEEL_SETUP_H
