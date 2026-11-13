# Linux Loader for Windows: Raw Thrills games

This fork (branch `rawthrills-windows`) runs the PC-based Raw Thrills arcade
games on Windows. `linuxloader.exe` loads the game's Linux executable into its
own process, so no Linux, Wine or virtual machine is needed.

Tested on Windows: Cruis'n Blast (v1.25), Jurassic Park, The Walking Dead, and
Big Buck HD Wild (v08.29 and v08.40).

## Install

Unzip the release anywhere, keep `ll-deps` next to `linuxloader.exe`, and put
each game in its own folder, for example:

```
linuxloader\
  linuxloader.exe
  ll-deps\
  setup-wheel.bat
  linuxloader.ini
  jp.ini
  run-jp.bat
  games\Jurassic Park\
```

A `.bat` per game, with paths relative to the `.bat` itself (`%~dp0`) so the
folder can be moved:

```bat
@echo off
cd /d "%~dp0games\Jurassic Park"
"%~dp0linuxloader.exe" -c "%~dp0jp.ini" -g "%~dp0games\Jurassic Park" > "%~dp0jp-run.log" 2>&1
```

`-c` is the settings file, `-g` the game's folder, and the log goes to
`jp-run.log`: it is the first thing to look at when something does not work.
Esc or Alt+F4 quits. In the `.ini` files, a comment (`;` or `#`) must be on a
line of its own.

## Display

```ini
[Display]
; 0: a window
FULLSCREEN = 1
; the game's resolution (default 640x480)
WIDTH = 1920
HEIGHT = 1080
```

## Light guns

A mouse over the game's window is player 1's gun (left button the trigger,
right button the second button). For **two guns**, read each one on its own
through Windows Raw Input:

```ini
[Input]
; default 0
ENABLE_RAW_GUNS = 1
; optional: which gun is which player, see below
;PLAYER_1_GUN = VID_16C0&PID_0F01
;PLAYER_2_GUN = VID_16C0&PID_0F39

[Display]
; the white border Sinden guns track (default 0)
BORDER_ENABLED = 1
; its thickness, in % of the screen (default 2)
WHITE_BORDER_PERCENTAGE = 2
; a black edge around it (default 0)
BLACK_BORDER_PERCENTAGE = 0
```

- The guns must be in **mouse mode with absolute positioning** (GUN4IR, Sinden).
  For Sinden, `Lightgun.exe` must be running.
- Without `PLAYER_n_GUN`, the guns are the first two mice that report absolute
  positions, sorted by device name, so each gun keeps its player from one start
  to the next. A desktop mouse or touchpad is ignored. A gun counts once it has
  pointed at the screen; until both have, the Windows cursor is player 1's gun.
- To choose which gun is which player, look for the mice listed at startup in
  the log:

  ```
  Desktop input: mouse 1: \\?\HID#VID_16C0&PID_0F01&MI_02&Col02#...
  Desktop input: mouse 2: \\?\HID#VID_16C0&PID_0F39&MI_02&Col02#...
  ```

  and give each player a part of its gun's name: `VID_xxxx&PID_yyyy` when the
  two guns have different ids, the whole name for two identical guns (it holds
  the USB port).

  ```ini
  PLAYER_1_GUN = VID_16C0&PID_0F01
  PLAYER_2_GUN = VID_16C0&PID_0F39
  ```

Two guns are tested with Jurassic Park (two Sindens). Big Buck HD Wild reads
its guns the same way and should work too, untested.

## Wheel

Cruis'n Blast reads a wheel and its pedals with force feedback, and without a
wheel the first gamepad (left stick steers, right trigger gas, left trigger
brake). A wheel with a Logitech PRO Racing Wheel's axes works without setup.

For any other wheel, or to use its buttons, close the game and run
`setup-wheel.bat` once. It asks you to move each control, finds the force's
direction, then asks for each button of a standard pad (Enter skips one). The
result is used by all the games, in `%LOCALAPPDATA%\linuxloader\wheel.ini`:

```ini
[Driving]
Steer = JOY0_AXIS_0
Gas = JOY0_AXIS_3
; _INVERTED reverses an axis
Brake = JOY0_AXIS_4_INVERTED

[Config]
Wheel_Setup = 1
; 1 if the force pulls the wrong way
Wheel_FFB_Inverted = 0
Wheel_SDL_Mapping = <written by setup-wheel.bat>

; A B X Y Start Select L1 R1 L2 R2 L3 R3 Up Down Left Right:
; JOY0_BUTTON_<n>, JOY0_HAT<n>_<UP|DOWN|LEFT|RIGHT> or NONE
[Buttons]
A = JOY0_BUTTON_0
Start = JOY0_BUTTON_9
Up = JOY0_HAT0_UP
L2 = NONE
```

Each game gives the buttons the meaning it gives a gamepad's. Without
`wheel.ini`, the axes come from `controls.ini` in the game's folder.
`Wheel_SDL_Mapping` makes the wheel a gamepad for the Lindbergh games: run
`setup-wheel.bat` again rather than editing only `[Buttons]`.

## More

See [docs/guide-windows.md](docs/guide-windows.md) for the other options and
the Lindbergh games.
