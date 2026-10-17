# p2hl1

Half-Life 1 movement for Portal 2, as a server plugin.

The walk and air movement is a port of `PM_PlayerMove` from the Half-Life SDK
(`pm_shared.c`): 800 gravity, 320 max speed, 10 accelerate / airaccelerate,
the 30 u/s air speed cap, edge friction, HL1 jump, duck and ducktap, stair
stepping and ground checks. Portal 2 still handles collision, portals, water,
tractor beams and wall gel. Catapult and other launch flights use the game's gravity
so you still land on the target.

## Install

1. Build `hl1mov.dll` (see below) or grab it from a release.
2. Copy it into `Portal 2/portal2/`.
3. In the console: `plugin_load hl1mov`

Works alongside SAR.

## Cvars

| cvar | default | |
|---|---|---|
| `hl1_movement` | 1 | turn HL1 movement on or off |
| `hl1_fps` | 100 | HL1 frame rate to simulate (splits each tick, 0 = off) |
| `hl1_jumpbuffer` | 2 | ticks a jump press is remembered before landing, 0 = strict HL1 |
| `hl1_edgefriction` | 2 | friction multiplier near ledges |
| `hl1_bhopcap` | 0 | 1.7x max speed bunnyhop cap from later HL1 patches |
| `hl1_maxspeed` | 320 | player max speed |
| `hl1_inputspeed` | 400 | movement key speed, 0 keeps Portal 2's |
| `hl1_debug` | 0 | print movement stats once a second |

## Build

Needs Visual Studio with the x86 C++ tools, CMake and Ninja. Portal 2 is 32-bit.

```
build.bat
```

Output: `build/hl1mov.dll`. Edit the `vcvars32.bat` path in `build.bat` if your
Visual Studio is installed somewhere else.
