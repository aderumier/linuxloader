# Linux Loader for Windows: Raw Thrills games

This fork (branch `rawthrills-windows`) runs the PC-based Raw Thrills arcade
games on Windows. `linuxloader.exe` loads the game's Linux executable into its
own process, so no Linux, Wine or virtual machine is needed.

Tested on Windows: Cruis'n Blast (v1.25), Jurassic Park, The Walking Dead,
Big Buck HD Wild (v08.29 and v08.40) and Big Buck Safari, and the 64-bit
Centipede Chaos, Halo: Fireteam Raven and Minecraft Dungeons Arcade, run by
`linuxloader64.exe`. The
Gaelco games (Tokyo Cop, Ring Riders, Gaelco Championship Tuning Race) run
too; Ring Riders' and Tuning Race's dumps are the cabinet's whole disk, the
game in `home\joc2001\<game>\salidas`.

## Install

Unzip the release anywhere, keep `ll-deps` next to `linuxloader.exe` (and
`ll-deps64` next to `linuxloader64.exe`), and put each game in a `games`
folder next to it, under the name its `.bat` uses:

```
linuxloader\
  linuxloader.exe
  ll-deps\
  linuxloader64.exe
  ll-deps64\
  run-centipede.bat games\Centipede Chaos\
  run-halo.bat      games\Halo Fireteam Raven\
  run-minecraft.bat games\Minecraft Dungeons Arcade\
  run-mcoc.bat      games\Marvel Contest of Champions\
  setup-wheel.bat
  run-cb.bat        games\Cruis'n Blast\
  run-jp.bat        games\Jurassic Park\
  run-twd.bat       games\The Walking Dead\
  run-bbhd.bat      games\Big Buck HD Wild\
  run-bbhd840.bat   games\Big Buck HD Wild 8.40\
  run-bbs.bat       games\Big Buck Safari\
  run-tc.bat        games\Tokyo Cop\
  run-rr.bat        games\Ring Riders\ (the whole disk)
  run-gctr.bat      games\Gaelco Championship Tuning Race\ (the whole disk)
  examples\linuxloader.ini, jp.ini
```

Copy `examples\linuxloader.ini` (full screen, 1920x1080) and
`examples\jp.ini` (the same, plus two light guns and the Sinden border) next to
`linuxloader.exe` once: they are kept out of the way so that unzipping a new
release does not overwrite your settings. `run-jp.bat` uses `jp.ini`, the
other `.bat` files `linuxloader.ini`. Without its settings file, a game starts
with a `linuxloader.ini` in its own folder if there is one, else with the
defaults (640x480 in a window).

The `.bat` files take their paths from their own folder (`%~dp0`), so the
folder can be moved:

```bat
@echo off
cd /d "%~dp0games\Jurassic Park"
"%~dp0linuxloader.exe" -c "%~dp0jp.ini" -g "%~dp0games\Jurassic Park" > "%~dp0jp-run.log" 2>&1
```

`-c` is the settings file, `-g` the game's folder, and the output goes to
`jp-run.log`. The loader's log is off by default; when something does not work,
turn it on in the settings file and look there first:

```
[System]
DEBUG_MSGS=1
```

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
  the log (`DEBUG_MSGS=1`):

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

## Centipede Chaos

`linuxloader64.exe` runs the 64-bit g7 games; Centipede Chaos (v01.11) is the
one done so far. Its folder is the cabinet's `pm` folder: `g7\centipede\game2`
(the dump of `game`) and the dongle's recorded answers in `g7\dump`.
`run-centipede.bat` uses `linuxloader.ini`: with `FULLSCREEN = 1` the game
renders at `WIDTH` x `HEIGHT` (at most the screen's, the screen's by default),
scaled to the screen; with `FULLSCREEN = 0` it is a 1280x720 window.

Keys: 5, 6, 7 coins, 1, 2, 3 start; player 1 the arrows and Space (or left
Ctrl), player 2 D, G, R, F and A, player 3 J, L, I, K and right Ctrl; F2 held a
second opens the test menu, F1 is service, Page Up/Down the volume. XInput
pads are players 1 to 3: the left stick or the D-pad, A/B/X/Y the button,
Start, Back a coin, the right stick's click the test button, the left's
service. Esc or Alt+F4 quits.

## Halo: Fireteam Raven

Run by `linuxloader64.exe` too (v03.11, TeknoParrot's dump). Its folder holds
`game` (the dump), `game.orig` (the original, which the file check hashes),
`lib`, `data`, `eFileMaps` and the dongle's recorded answers in `dump`.
`run-halo.bat` uses `linuxloader.ini`: `FULLSCREEN = 1` is full screen at
`WIDTH` x `HEIGHT` (the screen's by default), `FULLSCREEN = 0` a window.
The cabinet is the 4 player one; the environment variable
`HALO_CABINET_TYPE=4` (or 8) makes it a 2 player one.

The mouse is player 1's gun: left button the trigger, right reload, middle
the action button. Keys: 5, 6, 7, 8 coins, 1 to 4 start (also I and O for
players 1 and 2); F2 test, F1 service, Up/Down the volume. XInput pads are
players 1 to 4, each aiming its gun with the sticks (or the D-pad): right
trigger or A fires, left trigger, X or LB reload, B, Y or RB the action,
Start, Back a coin, the right stick's click test, the left's service. Player
1's pad takes the gun when its stick moves, the mouse when it moves.

## Minecraft Dungeons Arcade

Run by `linuxloader64.exe` (v05.00, TeknoParrot's dump `gamedump.exec`). Its
folder is the game's one with `lib`, `data`, `pmuser` and `dump`: the dongle's
recorded answers, `dongle_memory.bin` (the cards it may dispense) and the card
art in `dump\binder_art`. The folder must be writable (the game keeps its log
and settings in `pmuser`). `run-minecraft.bat` uses `linuxloader.ini`:
`FULLSCREEN = 1` renders at `WIDTH` x `HEIGHT` (the screen's by default),
`[Sound] VOLUME` the level in percent (40 by default: the game is mixed loud).

Four players, MAME's keys: player 1 the arrows and left Ctrl, left Alt, Space,
player 2 R, F, D, G and A, S, Q, player 3 I, K, J, L and right Ctrl, right
Shift, Enter, player 4 the keypad's 8, 2, 4, 6 and 0, `.`, `+`; 1 to 4 start,
5 or 6 coins for players 1 and 2, 7 or 8 for 3 and 4; F2 test, F1 or 9
service. XInput pads are players 1 to 4: the left stick or the D-pad, A, B
and X (or Y) the three buttons, Start.

The cabinet scans paper cards with webcams; here F3 to F6 (or Select+Start on
a pad) open a card binder for player 1 to 4: arrows pick, Space takes a card
into the hand, Q/E change the kind, Enter scans the hand, Backspace closes (on
a pad: A, LB/RB, Start, B). Esc or Alt+F4 quits the game.

## Marvel Contest of Champions

A Unity game: its Linux dump runs on Unity's own Windows player of the same
version (2021.3.23f1), from the same folder, which still runs on Linux. The
folder needs, beside the Linux files:

- `mcoc2.exe` and `mcocwin.dll`, from this release's `mcoc` folder;
- `UnityPlayer.dll` and `MonoBleedingEdge\`, Unity's Windows player, and
  `mcoc2_Data\Managed_win32\`, the Windows builds of Mono's libraries the game
  ships. They are Unity's, not in this release: `tools/mcoc-add-windows.sh`
  (on Linux) downloads them from Unity and adds them, with the two files
  above, to a game folder.

`run-mcoc.bat` uses `linuxloader.ini` (`[Display]` `WIDTH`, `HEIGHT`,
`FULLSCREEN`); the log is `mcocwin.log` in the game's folder, Unity's in
`rt_data\unity.log`. The game draws with OpenGL.

Keys, MAME's: player 1 the arrows and left Ctrl, left Alt, Space, left Shift
(light, medium, heavy, special attack), player 2 R, F, D, G and A, S, Q, W;
1 and 2 start, 5 and 6 coins, F2 test, 9 service, Page Up/Down the volume.
XInput pads 1 and 2: the left stick or the D-pad, X light, Y medium, B heavy,
A special, Start, Back a coin; pad 1's shoulders the volume. No webcam yet:
on the scan screen, the stick left/right picks a champion and down opens the
card binder. Esc or Alt+F4 quits.

## More

See [docs/guide-windows.md](docs/guide-windows.md) for the other options and
the Lindbergh games.
