# Namco ES1 (Wangan Midnight Maximum Tune 5DX+) — porting notes

State: **2026-11-27.** Plays (user: 100%) online with Banapassport cards
(the loader's terminal and game server: cards registered, cars made,
results saved and loaded again), the window scaled. Offline with [Network]
`WMMT4_SERVER = 0`. Known problems on Mesa: below (Display).

- Dump: `<games>/Wangan Midnight Maximum Tune 5DX+/` (`WMN5r` at its top,
  `data/`, `data_en/`, `data_ng_lnx/`). Export build **W5P 2.20.02**
  (`W5P100-2-NA-MPR0-F02`), the Pacloader fork's "5DX+ export 2.20.02" table.
- Loader id **`0x6c5079a3`** (`WMMT5DXP_ES1`), file CRC `0x3f5ab62b`.
- `lib/`: OpenSSL 0.9.8 (`libssl`/`libcrypto.so.0.9.8`) and Cg (`libCg.so`,
  `libCgGL.so` from `libs/csneo32/cg`), as WMMT4's.
- Desktop run: `scripts/linux/runwmmt5.sh` (needs `LIBS32` for 32-bit glut).
- Code: `namco/namcoEs1Wmmt5.c`; the display, GL fences, Cg shaders and LAN
  hooks are WMMT4's (`namcoEs1Wangan()` in `namcoEs1Wmmt4.c`). The cabinet
  mounts its packages over `data/`: `dataOverlays` (`data_en`, then
  `data_ng_lnx`) in `namcoEs1Games.c`.

## What it needed

- **Dongle**: 0xd40 image, serial `267620542069` at 0xd00 (5th digit 2 =
  export market, 6th 0 = drive unit). NBLINE points: two records (cost,
  limit, used, big-endian + MPEG-2 CRC): used must be ≤ limit − cost (else
  538 "NBLINE POINT balance error", the boot stops) and fewer than 50 plays
  left shows "Please charge NBLINE points".
- **Network** (fork): content router .254, link/peer checks, monitor steps
  answered. Plus: `getifaddrs` is a synthetic lo + eth0 (a host interface
  without an address crashed the refresh), the refresh gets eth0's name,
  address and the host's MAC (an empty MAC threw `std::out_of_range`), and
  the monitor stays idle until the unit check gives it the dongle serial
  (the fork's step kick let ALL.Net auth start with an empty one).
- **Unit check**: the terminal steps are skipped (drive cabinet alone).
- **Card reader / dispenser**: BngRw and STR400 answered; online, WMMT4's
  card (`save/server/card.ini`) is on the reader when the game waits for a
  touch (its record written into the caller's block as the fork does: the
  game's callback takes a lock the caller holds). The dispenser has no
  cards ("This machine does not sell the new card").
- **Steering board (STR3, /dev/ttyS0)**: not emulated; its protocol is not
  WMMT4's STR2 and the fork never got it past its initial wait. A board that
  does not answer stops the cabinet (E2212); its initialisation
  (`0x80e6020`) answers "no board" (its driver on a failed port crashed
  some boots): STEERING CHECK NG, then the game.
- **Force feedback** (the Windows arcade loader's way for the ES3 5DX+): the
  game hands its forces to its steering controller every frame whether the
  board is there or not (`0x80de440`: slip, spring, friction, collision).
  Hooked and played on the evdev wheel: spring as centering, collision as
  a push away from its side, road joints (collisions 0.209..0.311) and
  tire slip as a vibration: a 60 ms sine (`evdevFfbVibration`, wheels with
  FF_PERIODIC sine; a pad's strong motor). Friction is not played, as on
  Windows. `WMMT5_FFB_TRACE=1` logs the forces.

## Online: Banapassport cards

The cards need the cabinet online and linked to the store's terminal (the
banner "BANAPASSPORT CARDs cannot currently be used" otherwise). All in the
loader ([Network] `WMMT4_SERVER = 0` keeps it offline):

- **Network services**: WMMT4's layer (`namcoEs1Wmmt4Net.c`: names to
  192.168.92.254, numeric and local lookups without `AI_ADDRCONFIG` (a
  machine without a network), curl answered in-process, a TLS server on the game's
  OpenSSL 0.9.8). 5DX+ does no ALL.Net PowerOn: Mucha's board
  authentication (`boardauth.do`, `regiauth.do`) is the network check, and
  its URL_1 is the game server.
- **Terminal** (after the Windows arcade loader's `wmmt5_terminal.rs`):
  packets every 100 ms from 127.0.0.4 to the cabinet's UDP 50765 (link
  group 2 in this build, node 4, a `wm5.protobuf.Message`, CRC-32/MPEG-2),
  the store settings carrying the cabinet's software revision (the global
  `0xbed6364`, 22002). The LAN hook shows them from 192.168.92.15; the
  cabinet then sends its game server requests to the terminal's HTTPS
  proxy, 192.168.92.15:50765, the loader's server. The terminal serial
  question (E2407) stays answered (`0x83c1800`). `WMMT5_FREEPLAY=1`: free
  play in the store settings.
- **Game server** (`namcoEs1Wmmt5Rpc.c`): a port of the Windows loader's
  (`namcoes3srv`: rpc.rs, records.rs, store.rs, 5DX+ field numbers, after
  Bayshore): users by access code, cars, story / versus / ghost results,
  time attack records and rankings, crowns, car search. Kept in
  `save/wm5srv/{users,cars,ta}/<id>` (protobuf). Deleting `save/wm5srv`
  starts afresh. `WMMT4_SERVER_TRACE=1` logs every request and answer.

## Display

WMMT4's (`namcoEs1Wangan()`): the frame drawn at 1360x768 and scaled into
the window at each swap, the window's size read on a connection of the
loader's own. What 5DX+ needed on Mesa (none of it acts on NVIDIA):

- **Back buffer.** The game asks glXChooseFBConfig for any config
  (`GLX_FBCONFIG_ID GLX_DONT_CARE`) and makes its contexts with the first:
  Mesa's is single-buffered, so its contexts drew into the window's front
  buffer, shown by Mesa as they flush (the frame never scaled, in a
  corner). It composes its frame from several contexts (render and loading
  threads, all bound to the window): each is made to draw into the back
  buffer when bound (`glXMakeContextCurrent`). Moving the window's
  double-buffered config first instead crashed more often; only the render
  context on the back buffer left the frame black.
- **Single-sampled.** With the cabinet's 4x multisampling its contexts
  crashed while loading; the visual and configs are asked without samples.
- **NVIDIA-only calls:** `glClearDepthdNV` / `glDepthRangedNV` (Mesa's do
  nothing) go to the standard ones; `glRenderbufferStorageMultisampleCoverageNV`
  to the multisampled storage (the game does not call it).
- `XInitThreads()` before the game's X connection.

`WANGAN_SCALE_TRACE=1` logs the window, the configs and the contexts.

### Known problems (Mesa)

- **The garage's car is see-through** ("Choose a car for the race", after
  the card: the big 3D car shows its far side through its body). Not
  fixed; the new-car screens and races are fine. Reproduces headless with a
  card that has cars.
- **A boot in a few crashes while loading** (heap corruption, a jump to
  `0x50584500` or `free(): ... corrupted`): four threads on the window's
  buffers. With the game's own front-buffer drawing (no scaling) it did not
  happen in 8 boots.

## Controls

As WMMT4 (keyboard: arrows steer/accelerate/brake, space Enter, 5 coin,
1 start, F2 test). In 5DX+'s menus the **accelerator decides** and the
brake goes back (the game's design); Enter answers dialogs and the test menu.

## Save directories

The cabinet's storage script (`wangan4_storage`) mounts a partition per
save directory under `save/` (setting00/01, billing00/01, bookkeep00/01,
errorlog00/01, database00/01, banacoin00/01, contents); the game makes none
of them. The loader makes them at start (`cabinetDirectories`), relative to
the game's directory: the loader's `mkdir` ignores absolute paths under
`/home` (Lindbergh's `/home/disk*`).

## Batocera

Flattened squashfs (WMN5r at its top, no `save/`: written into Batocera's
overlay), in `namcoes3` beside the ES3 5DX+ as
`Wangan Midnight Maximum Tune 5DX+ (NS2).squashfs`, with
`emulator=linuxloader` / `core=linuxloader` for it in batocera.conf. The
generator drives it as WMMT4 (kind `wmmt4`: its buttons, `lib/` first, the
libcurl shim).

## Debugging

`NAMCO_GAME_LOG=1` the game's log; `WMMT5_NET_TRACE=1` the network monitor
and unit check steps; `WMMT5_INFO_TRACE=1` the boot state's flags (unit
check step, terminal link, error); `WMMT4_SHOT=<dir>` frame grabs (shared with WMMT4).
