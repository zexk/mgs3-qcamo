# MGS3 camouflage reload protocol

Verified live against PC Master Collection executable timestamp `0x6980B92F`.
All addresses below are module-relative RVAs.

## Persistent state

- `0xACDE98`: pointer to player stats
- stats `+0x67E`: equipped uniform byte
- stats `+0x67F`: equipped face-paint byte
- `0x1E16CD0`: encoded player-controller handle
- `0x10EDC0`: central message dispatcher

`scripts/probe.py` finds the stats slot by signature, reads current equipment,
and locates the inventory table read-only. Uniform ownership starts at item 40,
uses 80-byte entries, and treats capacity `>= 1` as owned.

## Working quick-change sequence

Uniform changes require both uniform and face-paint reloads. Changing only the
equipped byte updates labels and camouflage index but leaves Snake's mesh
unchanged. Running only the uniform asset phase works once, then leaves the
composite model unsafe for another change.

Milestone 1 reproduces this native sequence on the gameplay thread:

1. Write selected uniform to stats `+0x67E`.
2. Dispatch `0x1A0001` to begin uniform change.
3. Resolve uniform asset ID with `0x9BFA0` and type `0x602F5702`.
4. Queue its single resource slot through `0xE1660` and `0xE17C0`.
5. While `0xE1970` reports busy, pump task `0x106` through `0x725CD0`.
6. Finalize through `0x304050`; resolve slot `0x0D413AA8` with `0xE1680`.
7. Dispatch `0x1A0002` with returned asset handle.
8. Save current face paint, clear stats `+0x67F`, and dispatch `0x1A000F`.
9. Load face asset type `0x609B53C5` through same queue/pump/finalize path.
10. Restore face byte, refresh equipment through `0x2FDB00`, then apply face
    resource `0x6903A157` through `0x2F8D0` and `0xC3600`.
11. After short settle period, dispatch `0x1A0014`.

Calls to the dispatcher are bracketed by `0x1143F0`, which selects the
allocator heap the message handler will allocate from. Native brackets with
`(0)` and `(1)`; copy only the `(0)`, and restore the value that was there.
See "Allocator heap index" below.

Native Survival Viewer traces showed same uniform request object, queue, and
asset handle reused across consecutive changes. Face reload after every uniform
change was required for repeated gameplay changes.

## Quick menu shell

Phase 2 draws through a D3D11 Present hook and sends confirmed uniform IDs to
existing gameplay-thread dispatcher hook. Renderer thread only reads equipment
and inventory state. It never runs asset reload protocol.

Keyboard controls: F7 opens or closes; Up/Down select; Enter changes uniform;
Escape closes. Menu finds item table with same signature as `scripts/probe.py`
and shows owned uniforms. If signature is unavailable, Olive Drab and Tiger
Stripe remain as fallback test rows.

Single atomic gate covers queued request, native reload, and the settle
period. Enter closes menu only after gate accepts selection. This prevents input
from being accepted during short gap before gameplay thread starts reload.

## Survival Viewer context

`0x1E14AE0` points to live Viewer context only while screen exists.
`0x3008C0` and `0x300E50` are its uniform and face state machines.
Milestone 1 does not construct or tick this UI object; it calls minimal asset
and player protocol underneath it.

## Failed paths retained as constraints

- Raw stats write: labels change, mesh does not.
- Stats write plus `0x1A0014`: same result.
- Asynchronous uniform queue outside Viewer: crashes because task pump/finalize
  phase is missing.
- Uniform reload without face reload: first change works, second crashes.
- Preloading two uniforms into resource pool: pool exposes one active slot.

## Camouflage swatch assets

Master Collection ships every texture loose under `textures/flatlist/_win` in
CTXR form. Header is `TXTR`, big-endian version at `0x04`, big-endian width and
height at `0x08` and `0x0A`, and a big-endian byte count at `0x80` followed by
the top mip level as raw BGRA. Remaining mip levels use a different framing and
are not needed.

The Survival Viewer camouflage icons are seamless 128x128 tiles in the same
directory, named by asset id with no readable name anywhere on disk. Filtering
the hash-named textures to 128x128 with a fully opaque alpha channel (0x80 is
opaque, not 0xFF) narrows them to 49, and the codec portraits in that set are
the `*fb5*` ids. Uniform ids follow the `UNIFORM/...` string block at file
offset `0x8D0BC8` in `METAL GEAR SOLID3.exe`, which matches the Survival Viewer
order; each icon was matched to its uniform by comparing the tile against the
body texture that uniform loads. The table lives in `src/camo_swatch.cpp`.

Uniform bodies all share the source name `sna_def_olive.bmp`, so the flatlist
also holds one hashed body copy per camouflage. That mapping is not guessable
from the hash either, but each camouflage slot lists its own copy in
`sp/slot/camoufla-<slot>/bp_assets.txt`, whose first field is the flatlist path.
Slot names use the original internal spelling: `normal`, `rain_stroke`, `garco`,
`desert`, `animal`. Six uniforms are not BDU camouflage and carry their own body
texture: naked, sneaking suit, scientist, officer, maintenance, tuxedo. Slots
`cell`, `grenade`, and `mummy` exist on disk but have no uniform id. The `banana`
slot ships blank, and so does its icon.

## HUD font

`Misc/Layoutfont/_win/layoutfont.ctxr` is the bitmap atlas the game draws its
own HUD text with: 960x200, 32 columns by **5** rows of 30x40 cells, glyph shape
in alpha only and `0x80` for opaque. Cell zero is ASCII `0x20`, so the first
three rows cover printable ASCII and the last two hold accented Latin. The row
count is easy to get wrong by eye; the ink bands sit at y 2, 42, 82, 121 and
161, a pitch of 40. Baselines line up when each glyph is drawn across its whole
cell, so only the horizontal ink bounds need measuring for proportional spacing.

HUD colours sampled from the Survival Viewer and the equipment HUD: panel
`0A0A07`, frame and unselected row `434335`, selected row `A8A88C`, header text
`95957B`, bright HUD text `A6A68F`, dim `6E6E5E`.

## PC control map

The game's own keyboard prompt art doubles as ground truth for what each pad
button maps to: `textures/flatlist/ovr_stm/ctrltype_kbd/_win` replaces cross
with `Enter`, triangle with `E`, square with `Q`, and R1 with a right-click
mouse icon, and the `type_a` / `type_b` subdirectories give R2 as `2` / `R` and
L2 as `1` / `Q`. The full map is in the online manual, page 04.

Layout A takes W A S D, Left Ctrl, Left Shift, Space, E, F, H, M, N, O, U, I J
K L, 1, 2, 9, 0, Tab, Esc and both mouse buttons. Layout B takes the same
movement keys plus C, E, N, O, Q, R, V, 1, 2, Tab, Esc, the wheel click and
both mouse buttons. `G` and the arrow keys are free in both. `Esc` is Codec Mode, so it is not
usable as a close key. Once the menu freezes the game it can also take keys the
game owns, which is why W/S navigate alongside the arrows.

## Menu semi-pause

Weapon and item wheel holds both change `GV_PauseLevel` at RVA `0x1D78F6C`
from `0` to `4`, then restore it to `0` on release. `GV_ExecActor` at RVA
`0x10EE10` tests each actor's pause mask against this global at `0x10F0FF`.
Intersecting actors stop; wheel UI and audio actors continue.

Quick menu atomically sets and clears bit `4` on its open/close edges. Other
pause-level bits remain untouched. This reproduces wheel behavior without
stopping threads, scheduler fibers, rendering, or audio.

## Menu gating

The menu opens only during playable gameplay. Three read-only signals,
checked fail-closed in `src/gameplay_gate.h`:

- Area code at stats `+0x24`: bbtracker's 7-char stage string, where `s*` and
  `v*` are gameplay (`s001a`, `v000a`) and anything else (`title`) is out.
  This alone excludes title screens and other non-stage states.
- Survival Viewer context at RVA `0x1E14AE0`: live pointer only while the
  Viewer screen exists; non-null blocks.
- `GV_PauseLevel`: no bits outside the wheel bit may be set. On the opening
  edge the wheel bit itself must be clear, so a queued change never stacks
  onto a game wheel or another pauser; while open, the menu's own wheel bit
  is tolerated.

Enforcement is layered: the render thread refuses the opening edge and force-
closes under an open menu, the gameplay thread re-checks before applying a
queued change, and a 10 ms watchdog drops the pause if Present stalls across
a load. `F6` goes through the same queue gate.

Cutscene caveat: scripted sequences inside an `s`/`v` stage keep the same area
code, so this narrows the window but does not yet prove Snake is
controllable. A demo-playback flag, if one turns up, belongs in the gate.

## Pad input

The pad reaches this process only through Steam Input. Neither binary imports
an input API at all: the executable takes `GetAsyncKeyState` and `GetKeyState`
from USER32 and nothing else, and both it and `Engine.dll` pull `SteamInput006`
through `SteamInternal_FindOrCreateUserInterface`. Ruled out along the way, each
by observation rather than by imports:

- XInput reports `ERROR_DEVICE_NOT_CONNECTED` on all four slots. `xinput1_4.dll`
  in the process is loaded by this mod, not by the game.
- The winmm joystick API enumerates nothing. `winmm.dll` being resident is the
  ASI loader, not the game.
- DirectInput 8 enumerates nothing under `DI8DEVCLASS_GAMECTRL`, even though the
  game loads `dinput8.dll` on demand. Enumerating `DI8DEVCLASS_ALL` instead
  finds only "Wine Mouse", which is a trap worth avoiding.

Steam Input does see it: one controller, type 13, which is a DualSense.

The game drives Steam Input through the C++ interface, so hooking the flat
`SteamAPI_ISteamInput_*` exports catches nothing. Those exports are still useful
as documentation: each is a thunk of the form `mov rax,[rcx]; jmp [rax+disp]`,
which gives the vtable slots without guessing at an SDK layout. `RunFrame` is
0x18, `GetConnectedControllers` 0x30, `GetActionSetHandle` 0x48,
`GetDigitalActionHandle` 0x80, `GetDigitalActionData` 0x88 and
`GetDigitalActionOrigins` 0x90, so slots 3, 6, 9, 16, 17 and 18.
`GetDigitalActionData` returns its two bytes through a hidden pointer, which the
flat wrapper makes plain with a `lea rdx,[rsp+0x30]` ahead of the call.

Patching slots 9, 16 and 17 on the live interface showed the game polling
sixteen digital actions every frame, handles 1 to 16. `GetStringForDigitalActionName`
names them in Xbox terms: 1 `Y Button`, 2 `B Button`, 3 `X Button`, 4 `A Button`,
5 to 8 `Arrow Up/Right/Down/Left`, 9 and 10 the stick buttons, 11 `L1 Button`,
12 `R1 Button`, 13 `L2 Button`, 14 `R2 Button`, 15 `Back Button`, 16 `Start
Button`. So triangle is 1, cross is 4 and L1 is 11.

Those display names are not what `GetDigitalActionHandle` takes; it wants the
identifier from the game's action manifest, which is not on disk and is not in
the binary's strings. Rather than hardcode the numbers, `open_pad` walks handles
1 to 64, asks each for its name and keeps the ones it recognises. No hooks are
left installed.

Sticks are analog actions whose names are not among the digital sixteen, so the
menu navigates on the D-pad only.

## Face paint assets

Face paint ids follow the `FACE/...` string block that runs straight on from the
uniform names in `METAL GEAR SOLID3.exe`, at file offset `0x8D0CF0`. The game's
own camouflage records name all twenty-three, in equipped-face order: no paint,
woodland, black, water, mountain, splitter, snow, kabuki, zombie, oyama, mask,
green, brown, infinity, then the nationals soviet union, united kingdom, france,
germany, italy, spain, sweden, japan, usa. One per `sp/slot/facepaint-*` slot.

The Survival Viewer has its own face paint thumbnails, so the face textures are
not the thing to draw. They are 128x64 tiles in `textures/flatlist/_win`, named
by asset id like the uniform icons but at half the height, and the game keeps
the whole list of them at RVA `0x8ED380`: twenty-three consecutive dwords in
equipped-face order.

| face paint | icon | | face paint | icon |
| --- | --- | --- | --- | --- |
| no paint | `000a365b` | | brown | `00ac362b` |
| woodland | `0042367f` | | infinity | `004c3656` |
| black | `00e9362a` | | soviet union | `0011366c` |
| water | `0092367d` | | united kingdom | `008dab1c` |
| mountain | `00113632` | | france | `00a3363b` |
| splitter | `006a366f` | | germany | `0010363e` |
| snow | `002d366f` | | italy | `00df3647` |
| kabuki | `0080364d` | | spain | `005f366f` |
| zombie | `0000368b` | | sweden | `00433670` |
| oyama | `008b3660` | | japan | `006c364b` |
| mask | `00c93657` | | usa | `00bf3677` |
| green | `00ac363f` | | | |

Twelve of these had been identified by eye first, and nine of the twelve landed
on their own index in this table, which is what confirms the ordering. The other
three say what artwork matching costs: `00c93657` was read as oyama and is
really mask, and the id read as zombie, `00ac966f`, is not a face icon at all.
It belongs to the second block of uniform icons described below.

There is a uniform icon table too, at RVA `0x8EDCD0`, followed at `0x8EDD60` by
a second block of the same uniforms at 128x64. Neither matches the uniform icon
list in `camo_swatch.cpp` index for index -- ten of thirty-three differ, and the
order looks like Viewer display order rather than equipped-uniform order. The
list drawn from artwork is correct on screen, so it was left alone, but that
table is where to look if a uniform swatch ever comes out wrong.

The per-slot manifests are still useful for the face textures themselves. Each
`facepaint-*` slot lists exactly one, mostly `0003a157.img_<hash>.ctxr` where
`0003a157` is the id of `sna_face_def`; `none` takes plain `sna_face_def.bmp.ctxr`,
`brown` the unhashed `0003a157.img.ctxr`, and `mask` has no face texture at all
because it is the Raiden mask, a model swap.

## Task pump

`0x725CD0`, used as the asset wait, is a task-context yield rather than a task
pump. `0x22190` finds the current context by scanning the table at `0x103BC60`
with stride `0x90` for the pointer held in `0x1044C60`, returning its index.
`0x725CD0` compares that index against 1: on a match it runs the scheduler and
ignores its argument, otherwise it converts the argument to a duration and
yields. So `0x106` is a duration, not a category mask, and on the gameplay
thread it is discarded.

So `while (busy()) pump(0x106)` re-enters the scheduler from inside a message
dispatch. The game itself does exactly this in its area loader at `0x9BF40`, so
the pattern is sound; dropping it crashes instead, because nothing else
advances the request outside the Viewer. The Viewer does not pump only because
it ticks its change one state per frame, which is why its `0x1A0001` and
`0x1A0002` sit ~250ms apart.

## Allocator heap index

`0x1143F0` is a single instruction: `mov [0x1D7A550], ecx`. It is not a lock and
not a loading guard. `0x1D7A550` is the allocator's default heap index: the
allocator at `0x113F90` does `cmovs edi, [0x1D7A550]` whenever a caller asks for
heap `-1`, and indexes the heap descriptor table at `0x1E2C4A0` with stride
`0x28`. `0x114300` initialises one descriptor; `0x114280` and `0x114420` free
into the same table.

The Survival Viewer brackets each of its dispatches as `set(0)` ... `set(1)`
because the Viewer screen runs with the ambient heap already at 1; its teardown
at `0x303169` writes 0 back on the way out. During gameplay the ambient heap is
0, so copying that literal pair leaves the index at 1 forever. Every later
allocation then lands in the wrong arena and the next area transition never
finishes: the area loader at `0x9BDA0` sits in its own pump loop at `0x9BF40`
(`0x725CD0(0x106)` until `0xE1970` clears) while the rest of the game keeps
rendering at 60 FPS. That is the semi-pause. Save and restore the real ambient
value instead of hardcoding 1.

The bisect that isolated it: writing the stats byte alone transitioned fine,
while every variant that dispatched anything -- `0x1A0001` alone, the two
dispatches with a stale handle, or the full protocol -- wedged. The common
factor was the dispatch bracket, not the asset work. Pumping was never
implicated: the area loader pumps the same way.

Three readings were wrong along the way and are recorded so they are not
repeated. A single program-counter sample showed `RtlAcquireSRWLockExclusive`
and was read as a deadlock; the game was in fact running normally at 60 FPS the
whole time, so the thread, lock and task-context investigation was chasing
ordinary behaviour. The asset pools and request modes were suspected twice and
are byte-identical between a native change and ours.

## Area asset loader

`0x9BDA0` loads a list of asset indices for a stage. Its pool argument is a
small cache of 16-byte entries starting at `pool+0x10`: dword asset id at `+0`,
queue pointer at `+8`. It first scans the cache for ids already resident, then
for each remaining id takes a free entry, calls `mode(queue, 2)`, `id(queue,
asset)`, pumps until `0xE1970` clears, and calls `0xE16B0(queue, 2)`.
`0x304050` is a wrapper over that last call which brackets it with the heap
index and restores from `0x1E15760`.

## Viewer uniform-change state machine

`0x3008C0` is the Viewer's uniform change, a jump-table state machine with the
index at `[this+0x26D8]` and the table at `0x300BC8`. It runs one step per
frame and returns without advancing while the asset system is busy, which is
why a native change takes about 2.5 seconds of wall clock.

- state 2 (`0x3008FE`): write `[this+0x218]` to stats `+0x67E`, refresh
  equipment through `0x2FDB00`
- state 3 (`0x30093D`): dispatch `0x1A0001` to the player
- state 7 (`0x300A10`): resolve the asset id, `request(type)`, `mode(queue, 2)`,
  `id(queue, asset)`
- state 8 (`0x300A83`): wait for `0xE1970`, `0x304050(entry, 2)`,
  `finish(queue, 0x0D413AA8)`, dispatch `0x140025` to `[this+0x220]`, dispatch
  `0x1A0002` with the handle, then branch on bit 11 of the uniform flags: set
  clears the face byte and dispatches `0x1A000F`, clear leaves the face alone

`0x302FB0` and the chain through `0x303070`, `0x3030B0`, `0x3030F0` and
`0x3031B8` is the Viewer *screen* closing, not change completion: it restores
the pause and UI globals, zeroes `0x1E15760`, and sends `0x1A0014`. The
`mode(queue, 1)` and `0x114280` it performs at `0x3030CF` are on the Viewer's
own queue, `[[this+0x50]+0x10]`, never on a request queue. Passing ours to
`0x114280` writes a free-list link over the queue's name string and kills the
game shortly after.

## Asset request modes

The native change and ours pass identical arguments to the asset calls:
`request(type)`, `mode(queue, 2)`, `id(queue, asset)`, `finalize(request, 2)`,
`finish(queue, slot)`. The mode 2 constants inherited from the proof of concept
are correct.

Request mode is owned by the game, not by a change. Hooking `0xE1660` shows the
game putting the uniform and face queues into mode 2 from `0x9BF23` seconds
before any change runs, and opening the Survival Viewer puts twelve queues into
mode 2 from `0x11016E`. Our `mode(queue, 2)` inside `load_asset` is therefore
redundant, and restoring those queues to mode 1 afterwards destroys state the
game established: doing so crashes the game a second or so later.

## Reading a live build

The shipped executable is Steam-DRM wrapped (a `.bind` section), so `.text` on
disk decodes as garbage. Disassemble from `/proc/<pid>/mem` at the module base
found in `/proc/<pid>/maps` instead. Anything the mod hooks reads back as a
MinHook `jmp` in the first five bytes, so re-align a few bytes earlier when a
function entry looks wrong.

## Camouflage index

The index the HUD shows lives at `0x1E16CF4`, in tenths of a percent, so `1000`
is 100%. That is the player record at `kPlayerSlot` (`0x1E16CD0`), which is
0x80 bytes: `+0x24` is the index and `+0x28` the state bitset. `0xA8070`
computes it and `0x358CEB` stores it.

It is a sum of independent terms:

    index = uniform_value * 10 + face_value * 10 + movement_penalty + light

Only the first term depends on the uniform and only the second on the face
paint, and neither depends on the other. So ranking uniforms is exact, the
difference between two of them is exact, and the best pairing needs no search:
it is the best uniform together with the best face paint.

Values come from one 0x18-byte record per uniform at `0x1E216E0` and per face
paint at `0x1E214A0`, indexed by the equipped byte. `+0x00` is the internal
name, `+0x08` the value table. A uniform's table is 27 terrains of 5 postures,
signed bytes, plus an `0xFF` terminator; a face paint's is one byte per terrain.

The posture, and which surface it is read against, follow the state bits
`0x359020` queries -- that function is only `(bitset[bit / 32] >> (bit % 32)) & 1`
against `0x1E16CF8`.

| condition | surface | slot |
| --- | --- | --- |
| bit `0x3B`, on a wall | wall material | 3, or 4 crouched |
| prone: bit `3` and not bit `0xA9` | ground material | 2 |
| crouched: bit `2` | ground material | 1 |
| otherwise | ground material | 0 |

The two surface materials are republished every frame at `0x1D38AF8` (ground)
and `0x1D38AFC` (wall). `0xA88F0` maps a material to a dense terrain index by
scanning the table at `0x1D38B10`, whose entry count is at `0x1D38F10`: 8-byte
entries of an int key and a uint16 value, with a zero key acting as the
fallback. On a miss it calls back into game code, so the mod reimplements the
scan read-only rather than calling it from the render thread, and treats a miss
as no data.

Worked example, verified live: equipped uniform 0, ground material `4315316`,
standing. The material maps to terrain 11, so the slot is `11 * 5 + 0 = 55`;
Olive Drab's table holds `0x0F` there; `15 * 10 = 150`, and `0x1E16CF4` read
150.

## Uniform and face paint inventory

`0x9BA40` maps an equipped-uniform byte to an item id and `0x9BA70` does the
same for face paints: uniforms are items 41..73 and face paints 74..96, both
8-byte key/value pairs in tables at `0x8D1DB0` and `0x8D1CF0`. Item records
themselves are 0x50 bytes at `0x1D30B08`, with the item's own id at `+0x20` --
which is what `0x9BBE0` returns -- and its asset id at `+0x2C`.

The ownership table the mod finds by signature is a different table with the
same 0x50 stride, and it is indexed one lower than those item ids: uniforms
start at entry 40 and face paints at entry 73. Capacity of at least one means
owned. Getting this wrong by one shifts every entry onto its neighbour's
ownership, which reads as unlocked camouflage appearing in the menu.

## External references

- [Konami MGS3 manual](https://metalgear.konami.net/manual/mc1/mgs3/pc/en/page15.html)
- [MGSHDFix](https://github.com/Lyall/MGSHDFix) for independently named stats fields
- [ANTIBigBoss trainer](https://github.com/ANTIBigBoss/MGS3-Master-Collection-Trainer)
  for independent confirmation of equipped offsets and inventory layout
