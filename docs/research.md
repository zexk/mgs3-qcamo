# MGS3 internals

PC Master Collection, executable timestamp `0x6980B92F`. All addresses are
module-relative RVAs unless a path is given.

The shipped executable is Steam DRM wrapped (`.bind` section), so `.text` on
disk decodes as garbage. Disassemble from `/proc/<pid>/mem` at the base in
`/proc/<pid>/maps`. Hooked functions read back with a `jmp` over their first
five bytes.

## Globals

| RVA | Contents |
| --- | --- |
| `0xACDE98` | Pointer to player stats |
| `0x1E16CD0` | Player record, 0x80 bytes. Also the encoded controller handle |
| `0x1E14AE0` | Survival Viewer context, non-null only while that screen exists |
| `0x1D78F6C` | `GV_PauseLevel` |
| `0x1E21AB0` | Player state flags A |
| `0x1E21AB4` | Player state flags B |
| `0x1D7A550` | Allocator default heap index |
| `0x1E2C4A0` | Heap descriptor table, stride `0x28` |
| `0x1E15760` | Saved heap index, restored by `0x304050` |
| `0x1D30B08` | Item records, stride `0x50` |
| `0x1E216E0` | Uniform camouflage records, stride `0x18` |
| `0x1E214A0` | Face paint camouflage records, stride `0x18` |
| `0x1D38AF8` | Ground surface material, republished each frame |
| `0x1D38AFC` | Wall surface material, `-1` when none |
| `0x1D38B10` | Material to terrain map, 8-byte entries |
| `0x1D38F10` | Entry count for that map |
| `0x1B9F4B8` | Sound effect bank records, stride `0x54` |
| `0x8ED380` | Face paint icon ids, 23 dwords in equipped-face order |
| `0x8EDCD0` | Uniform icon ids |
| `0x8EDD60` | Uniform icon ids, 128x64 variants |
| `0x8D1DB0` | Uniform byte to item id pairs |
| `0x8D1CF0` | Face byte to item id pairs |

## Functions

| RVA | Signature |
| --- | --- |
| `0x10EDC0` | `dispatch(target, message, data)` |
| `0x9BFA0` | `asset_id(type, index)` |
| `0xE1650` | `request(type)` returns pool |
| `0xE1660` | `set_mode(queue, mode)` |
| `0xE17C0` | `set_id(queue, asset)` |
| `0xE1680` | `finish(queue, slot)` returns handle |
| `0xE1970` | `busy()` |
| `0xE16B0` | `finalize(queue, mode)` |
| `0x304050` | `finalize(entry, mode)`, wraps `0xE16B0` with the heap bracket |
| `0x113B00` | `find_asset(slot)` |
| `0x2F8D0` | `prepare_face(asset)` |
| `0xC3600` | `apply_face(slot, id, prepared)` |
| `0x2FDB00` | `refresh_equipment()` |
| `0x725CD0` | `yield(duration)`, task-context yield |
| `0x1143F0` | `set_heap(index)`, one instruction: `mov [0x1D7A550], ecx` |
| `0x113F90` | Allocator. `cmovs edi, [0x1D7A550]` when asked for heap `-1` |
| `0x114300` | Initialise one heap descriptor |
| `0x114280`, `0x114420` | Free into the heap table |
| `0x9BA40` | `uniform_item(byte)` |
| `0x9BA70` | `face_item(byte)` |
| `0x9BBE0` | `item_id(item)`, reads item record `+0x20` |
| `0xA8070` | Compute camouflage index |
| `0xA88F0` | `terrain(material)` |
| `0x359020` | `state_bit(bit)` against `0x1E16CF8` |
| `0x9ECC0` | `play_sound(cue)` |
| `0x10ECF0` | Read `GV_PauseLevel` |
| `0x10ED00` | Clear bits, `and not ecx` |
| `0x10ED10` | Set bits, `or ecx` |
| `0x10EE10` | `GV_ExecActor`, tests actor pause masks at `0x10F0FF` |
| `0x9BDA0` | Area asset loader |

## Structures

Player stats, via `[0xACDE98]`:

| Offset | Field |
| --- | --- |
| `+0x24` | Area code, 7 chars. `s*` and `v*` are gameplay stages |
| `+0x5D6` | Area kind, word |
| `+0x67E` | Equipped uniform |
| `+0x67F` | Equipped face paint |

Player record at `0x1E16CD0`, 0x80 bytes:

| Offset | Field |
| --- | --- |
| `+0x24` | Camouflage index, tenths of a percent |
| `+0x28` | State bitset, read by `0x359020` |

Camouflage record, stride `0x18`, indexed by the equipped byte:

| Offset | Field |
| --- | --- |
| `+0x00` | Internal name string |
| `+0x08` | Value table |

Uniform value tables are 27 terrains of 5 postures, signed bytes, then `0xFF`.
Face paint value tables are one byte per terrain, then `0xFF`.

Item record, stride `0x50` at `0x1D30B08`:

| Offset | Field |
| --- | --- |
| `+0x10` | Category, 2 for uniforms |
| `+0x20` | Own item id |
| `+0x2C` | Asset id |

Asset pool: 16-byte entries from `pool+0x10`, dword asset id at `+0x00`, queue
pointer at `+0x08`.

Material to terrain map: 8-byte entries, int key then uint16 value. A zero key
is the fallback.

## Messages

| Id | Meaning |
| --- | --- |
| `0x1A0001` | Begin uniform change |
| `0x1A0002` | Uniform asset handle |
| `0x1A000F` | Begin face paint change |
| `0x1A0014` | Refresh camouflage |
| `0x140025` | Sent by the Viewer to `[this+0x220]`, not required outside it |

Asset types: uniform `0x602F5702`, face paint `0x609B53C5`. Slots: uniform
`0x0D413AA8`, face `0x00413AA8`. Face resource `0x6903A157`, id `0x0003A157`.

## Change protocol

Both the uniform and the face paint must be reloaded. Writing the equipped byte
alone updates labels and the camouflage index but not the mesh; running only the
uniform phase works once and leaves the composite model unsafe for a second
change.

1. Write the uniform to stats `+0x67E`.
2. Dispatch `0x1A0001`.
3. `asset_id(0x602F5702, uniform)`.
4. `request`, `set_mode(queue, 2)`, `set_id(queue, asset)`.
5. `yield(0x106)` while `busy()`.
6. `finalize(entry, 2)`, then `finish(queue, 0x0D413AA8)`.
7. Dispatch `0x1A0002` with the handle.
8. Clear stats `+0x67F`, dispatch `0x1A000F`.
9. Before the next player tick, load the face asset through the same queue, pump
   and finalize path. Reusing the change path in the uniform tick can leave
   the composite model without a node; the game then dereferences null at
   `0xC8187`.
10. Restore stats `+0x67F`, `refresh_equipment()`, then `find_asset`,
    `prepare_face`, `apply_face`.
11. Before the following player tick, dispatch `0x1A0014`.

Run every phase after the actor-job dispatcher at `0x10EE10` returns. Running a
phase from the player dispatch at `0x5BC84B`, before or after its original call,
mutates model state while later actor jobs still walk the old nodes, producing
the null dereference at `0xC8187`.

Load before dispatching `0x1A0001`. That message begins a change the game
expects `0x1A0002` to finish with a real handle; failing the load after it has
gone out leaves the player mid-change and wedges the next attempt.

No wall-clock delay is needed. Neither native path has a timer: `0x3008C0` returns
to state 0 as soon as its last state finishes, and the path at `0x323xxx` waits
only on `0x2FF400`, which reads the Viewer context and so answers no during
gameplay. Once the sequence above returns, both assets are pumped to completion
and every dispatch has gone out; the only remaining dependency is the player
consuming them on its own tick.

Dispatch `0x1A0014` to the Snake actor as well as the player. Native sends it to
both; the second target can be latched from any `0x1A0014` the game sends
somewhere other than the player slot.

## Allocator heap

`0x1143F0` selects which heap the allocator uses for callers asking for heap
`-1`. Message handlers allocate, so dispatches are bracketed with it.

The Viewer brackets as `set(0)` ... `set(1)` because its screen runs with the
ambient heap already at 1, and its teardown at `0x303169` writes 0 back. During
gameplay the ambient heap is 0, so copying that pair leaves the index at 1
permanently, every later allocation lands in the wrong arena, and the next area
transition never finishes: `0x9BDA0` sits in its pump loop at `0x9BF40` while
the rest of the game keeps rendering. Save and restore the ambient value.

## Asset system

Request mode belongs to the game, not to a change. `0xE1660` shows the uniform
and face queues put into mode 2 from `0x9BF23` seconds before any change runs,
and opening the Viewer puts twelve queues into mode 2 from `0x11016E`. A change
passing `mode(queue, 2)` is therefore redundant; restoring mode 1 afterwards
destroys game state and crashes within seconds.

`0x725CD0` is a task-context yield, not a task pump. `0x22190` finds the current
context by scanning the table at `0x103BC60`, stride `0x90`, for the pointer in
`0x1044C60`. `0x725CD0` compares that index against 1: on a match it runs the
scheduler and ignores its argument, otherwise it treats the argument as a
duration. So `0x106` is a duration, not a category mask. `while (busy())
yield(0x106)` is the game's own pattern, used by the area loader at `0x9BF40`.

`0x9BDA0` scans its pool for asset ids already resident, then for each remaining
id takes a free entry and runs `set_mode`, `set_id`, pump, `0xE16B0(queue, 2)`.

## Survival Viewer

`0x3008C0` is the uniform change, `0x300E50` the face change. `0x3008C0` is a
jump-table state machine, index at `[this+0x26D8]`, table at `0x300BC8`, one
step per frame, returning without advancing while the asset system is busy.

| State | RVA | Action |
| --- | --- | --- |
| 2 | `0x3008FE` | Write `[this+0x218]` to stats `+0x67E`, `refresh_equipment()` |
| 3 | `0x30093D` | Dispatch `0x1A0001` |
| 7 | `0x300A10` | Resolve asset id, `request`, `set_mode`, `set_id` |
| 8 | `0x300A83` | Wait on `busy()`, finalize, finish, dispatch `0x140025` then `0x1A0002`, branch on bit 11 of the uniform flags to clear the face byte and dispatch `0x1A000F` |

`0x302FB0` through `0x303070`, `0x3030B0`, `0x3030F0` and `0x3031B8` is the
Viewer screen closing, not change completion. It restores pause and UI globals,
zeroes `0x1E15760` and sends `0x1A0014`. The `set_mode(queue, 1)` and `0x114280`
at `0x3030CF` act on the Viewer's own queue, `[[this+0x50]+0x10]`, never on a
request queue. Passing a request queue to `0x114280` writes a free-list link
over its name string and kills the game shortly after.

## Camouflage index

Stored at `0x1E16CF4` in tenths of a percent, so `1000` is 100%. Computed by
`0xA8070`, stored by `0x358CEB`.

    index = uniform_value * 10 + face_value * 10 + movement_penalty + light

The terms are independent. Ranking uniforms and differencing two of them is
exact, and the best pairing needs no search: it is the best uniform with the
best face paint.

Uniform value index is `terrain * 5 + posture`; face paint value index is
`terrain`.

| Condition | Surface | Posture |
| --- | --- | --- |
| State bit `0x3B`, on a wall | Wall | 3, or 4 crouched |
| State bit `3` and not bit `0xA9` | Ground | 2 |
| State bit `2` | Ground | 1 |
| Otherwise | Ground | 0 |

`0xA88F0` maps a material to a dense terrain index. On a miss it calls back into
game code, so a render thread should reimplement the scan read-only and treat a
miss as no data.

Worked example: uniform 0, ground material `4315316`, standing. Terrain 11, slot
`11 * 5 + 0 = 55`, Olive Drab's table holds `0x0F` there, and `0x1E16CF4` reads
`150`.

## Uniforms and face paints

Uniforms are items 41 to 73, face paints 74 to 96. The ownership table found by
signature is a different table of the same `0x50` stride, indexed one lower:
uniforms start at entry 40 and face paints at entry 73, and capacity of at least
one means owned.

Face paints in equipped order: no paint, woodland, black, water, mountain,
splitter, snow, kabuki, zombie, oyama, mask, green, brown, infinity, soviet
union, united kingdom, france, germany, italy, spain, sweden, japan, usa.

Face paint icons, 128x64 tiles in `textures/flatlist/_win`, from `0x8ED380`:

| Face paint | Icon | Face paint | Icon |
| --- | --- | --- | --- |
| no paint | `000a365b` | brown | `00ac362b` |
| woodland | `0042367f` | infinity | `004c3656` |
| black | `00e9362a` | soviet union | `0011366c` |
| water | `0092367d` | united kingdom | `008dab1c` |
| mountain | `00113632` | france | `00a3363b` |
| splitter | `006a366f` | germany | `0010363e` |
| snow | `002d366f` | italy | `00df3647` |
| kabuki | `0080364d` | spain | `005f366f` |
| zombie | `0000368b` | sweden | `00433670` |
| oyama | `008b3660` | japan | `006c364b` |
| mask | `00c93657` | usa | `00bf3677` |
| green | `00ac363f` | | |

The uniform icon tables at `0x8EDCD0` and `0x8EDD60` do not match the list in
`src/camo_swatch.cpp` index for index; ten of thirty-three differ and the order
looks like Viewer display order. The hand-built list is correct on screen, but
those tables are where to check if a swatch ever looks wrong.

## Textures

Every texture ships loose under `textures/flatlist/_win` in CTXR form:

| Offset | Field |
| --- | --- |
| `0x00` | `TXTR` |
| `0x04` | Version, big endian |
| `0x08` | Width, big endian |
| `0x0A` | Height, big endian |
| `0x80` | Byte count, big endian, then the top mip as raw BGRA |

Alpha follows the PS2 convention: `0x80` is opaque, not `0xFF`. Lower mips use
a different framing.

Uniform camouflage icons are seamless 128x128 tiles named by asset id. Uniform
bodies all share the source name `sna_def_olive.bmp`, so the flatlist holds one
hashed copy per camouflage; each camouflage slot lists its own in
`sp/slot/camoufla-<slot>/bp_assets.txt`, first field. Slot names use the
original internal spelling: `normal`, `rain_stroke`, `garco`, `desert`,
`animal`. Six uniforms are not BDU camouflage and carry their own body texture:
naked, sneaking suit, scientist, officer, maintenance, tuxedo. Slots `cell`,
`grenade` and `mummy` exist with no uniform id. The `banana` slot ships blank,
as does its icon.

Each `facepaint-*` slot lists one texture, mostly `0003a157.img_<hash>.ctxr`
where `0003a157` is `sna_face_def`. `none` takes `sna_face_def.bmp.ctxr`,
`brown` the unhashed `0003a157.img.ctxr`, and `mask` has none because it is a
model swap.

## HUD font

`Misc/Layoutfont/_win/layoutfont.ctxr`, 960x200, 32 columns by 5 rows of 30x40
cells, glyph shape in alpha only. Cell 0 is ASCII `0x20`; the first three rows
cover printable ASCII and the last two hold accented Latin. Ink bands sit at y
2, 42, 82, 121 and 161, pitch 40. Baselines align when each glyph is drawn
across its whole cell, so only horizontal ink bounds need measuring.

Colours sampled from the Viewer and the equipment HUD:

| Element | Colour |
| --- | --- |
| Panel | `0A0A07` |
| Frame, unselected row | `434335` |
| Selected row | `A8A88C` |
| Header text | `95957B` |
| Bright HUD text | `A6A68F` |
| Dim HUD text | `6E6E5E` |

## UI sound

`0x9ECC0` takes a cue id in `ecx` and nothing else: it masks to eleven bits,
tags `0x43` and hands it to the mixer. The `Misc/BP_SE.DAT` bank behind it is
never touched directly by UI code. Cues must be played from the gameplay
thread.

| Cue | Meaning | Sites |
| --- | --- | --- |
| `0x1A008` | Wheel open | `0x32DA49`, `0x32E6C6` |
| `0x1A009` | Wheel close | `0x32D361`, `0x32E034` |
| `0x1A00B` | Cursor | `0x302628` |
| `0x1A00C` | Decide | `0x302656` |
| `0x1A00D` | Back | `0x302710` |
| `0x300F` | Operation not permitted | |

Thirty-eight distinct cue ids are passed to `0x9ECC0` across the executable;
menu code overwhelmingly uses the middle three. The wheel pair was found by
looking for a `0x9ECC0` call just after a `set_pause(4)`. Scanning for
`mov ecx, imm32` immediately before the call misses one of the two wheel sites,
which has an unrelated store in between.

## Pause level

Weapon and item wheels move `GV_PauseLevel` from `0` to `4` and back.
`GV_ExecActor` tests each actor's pause mask against it at `0x10F0FF`;
intersecting actors stop while wheel UI and audio actors continue. Setting and
clearing bit 2 alone reproduces that without stopping threads, fibers,
rendering or audio.

## Player state flags

`0x1E21AB0` and `0x1E21AB4` are read as a pair and tested against a mask at
hundreds of sites, each mask naming the states that action must not interrupt:

    mov eax, [0x1E21AB4]
    or  eax, [0x1E21AB0]
    test eax, <mask>
    jne  <refuse>

| Mask | Sites |
| --- | --- |
| `0xFE000200` | `0x6CA3EA`, `0x6E074D` |
| `0x86000200` | `0x6DA4EB` |
| `0x78000000` | 12 sites from `0x2455F5` |
| `0x06000000` | 10 sites from `0x39C91C` |
| `0x18000000` | 4 sites from `0x33F172` |

The first two belong to the wheel-style popups, which take the same
`GV_PauseLevel` bit a quick menu takes, so their precondition is the one to
copy.

Observed over 2141 samples at 10Hz across four areas and several cutscenes: the
permitted state is a single value, `0x01000080`; blocked states are
`0x01000200`, `0x03000200`, `0x05000080`, `0x09000200` and `0x11000200`. Bit 9
reads as the cutscene bit but `0x05000080` has it clear and is caught only by
bit 26, so the mask matters and the single bit does not suffice.

Cutscenes keep their stage's area code, so an area check cannot substitute:
blocked stretches occur in `v001a` and `v003a`, not only in `v004a_0`.

## Menu gating

Signals for "playable gameplay", all read-only and checked fail-closed:

| Signal | Blocks when |
| --- | --- |
| Stats area code `+0x24` | First char is not `s` or `v` |
| Survival flags `+0x680` | Backpack bit `0x400` is clear |
| Viewer context `0x1E14AE0` | Non-null |
| `GV_PauseLevel` | Any bit outside the wheel bit; the wheel bit too on the opening edge |
| Player state flags | `(A \| B) & 0xFE000200` is non-zero |

`0x311444` tests bit `0x400` at stats `+0x680`. When clear, the Survival
Viewer writes disabled-entry mask `0x0B`, which disables CAMOUFLAGE, BACKPACK,
and CURE. The bit is clear before Snake retrieves his backpack and after the
torture sequence until he recovers his equipment. qcamo uses the same test and
plays the game's operation-not-permitted cue `0x300F` when opening is refused.

## Pad input

The pad reaches the process only through Steam Input. The executable imports
`GetAsyncKeyState` and `GetKeyState` from USER32 and no other input API; it and
`Engine.dll` pull `SteamInput006` through
`SteamInternal_FindOrCreateUserInterface`. XInput reports
`ERROR_DEVICE_NOT_CONNECTED` on all four slots, the winmm joystick API
enumerates nothing, and DirectInput 8 enumerates nothing under
`DI8DEVCLASS_GAMECTRL`. `DI8DEVCLASS_ALL` finds only "Wine Mouse", which is a
trap.

The game uses the C++ interface, so hooking the flat `SteamAPI_ISteamInput_*`
exports catches nothing. Those exports are still useful as documentation: each
is a thunk of the form `mov rax,[rcx]; jmp [rax+disp]`, which gives vtable
offsets without guessing at an SDK layout.

| Method | Offset | Slot |
| --- | --- | --- |
| `RunFrame` | `0x18` | 3 |
| `GetConnectedControllers` | `0x30` | 6 |
| `GetActionSetHandle` | `0x48` | 9 |
| `GetDigitalActionHandle` | `0x80` | 16 |
| `GetDigitalActionData` | `0x88` | 17 |
| `GetDigitalActionOrigins` | `0x90` | 18 |

`GetDigitalActionData` returns two bytes through a hidden pointer, which the
flat wrapper makes plain with a `lea rdx,[rsp+0x30]` ahead of the call.

The game polls sixteen digital actions every frame, handles 1 to 16.
`GetStringForDigitalActionName` names them in Xbox terms:

| Handle | Name | PlayStation |
| --- | --- | --- |
| 1 | `Y Button` | triangle |
| 2 | `B Button` | circle |
| 3 | `X Button` | square |
| 4 | `A Button` | cross |
| 5 to 8 | `Arrow Up/Right/Down/Left` | D-pad |
| 9, 10 | Stick buttons | L3, R3 |
| 11, 12 | `L1 Button`, `R1 Button` | L1, R1 |
| 13, 14 | `L2 Button`, `R2 Button` | L2, R2 |
| 15, 16 | `Back Button`, `Start Button` | select, start |

Those display names are not what `GetDigitalActionHandle` accepts; it wants the
identifier from the game's action manifest, which is neither on disk nor in the
binary's strings. Walk handles 1 to 64 and match on the reported name instead.
Sticks are analog actions whose names are not among the digital sixteen.

## PC control map

The keyboard prompt art is ground truth for pad mapping:
`textures/flatlist/ovr_stm/ctrltype_kbd/_win` replaces cross with `Enter`,
triangle with `E`, square with `Q` and R1 with a right-click mouse icon. The
`type_a` and `type_b` subdirectories give R2 as `2` or `R` and L2 as `1` or `Q`.

Layout A takes W A S D, Left Ctrl, Left Shift, Space, E, F, H, M, N, O, U,
I J K L, 1, 2, 9, 0, Tab, Esc and both mouse buttons. Layout B takes the same
movement keys plus C, E, N, O, Q, R, V, 1, 2, Tab, Esc, the wheel click and both
mouse buttons. `G` and the arrow keys are free in both. `Esc` is Codec Mode, so
it cannot serve as a close key.

## Constraints

| Approach | Result |
| --- | --- |
| Write the stats byte only | Labels and index change, mesh does not |
| Stats byte plus `0x1A0014` | Same |
| Asynchronous uniform queue outside the Viewer | Crashes, no pump or finalize phase |
| Uniform reload without face reload | First change works, second crashes |
| Preload two uniforms into the pool | Pool exposes one active slot |
| Restore request queues to mode 1 | Crashes within seconds |
| Pass a request queue to `0x114280` | Corrupts the queue, crashes |
| Copy the Viewer's `set(0)`/`set(1)` heap bracket | Next area transition never finishes |

## External references

- [Konami MGS3 manual](https://metalgear.konami.net/manual/mc1/mgs3/pc/en/page15.html)
- [MGSHDFix](https://github.com/Lyall/MGSHDFix) for independently named stats fields
- [ANTIBigBoss trainer](https://github.com/ANTIBigBoss/MGS3-Master-Collection-Trainer)
  for independent confirmation of equipped offsets and inventory layout
