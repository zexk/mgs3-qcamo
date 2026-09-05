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

Calls to dispatcher are bracketed by `0x1143F0(0)` and `0x1143F0(1)`, matching
native code.

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

Single atomic gate covers queued request, native reload, and four-second settle
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

## External references

- [Konami MGS3 manual](https://metalgear.konami.net/manual/mc1/mgs3/pc/en/page15.html)
- [MGSHDFix](https://github.com/Lyall/MGSHDFix) for independently named stats fields
- [ANTIBigBoss trainer](https://github.com/ANTIBigBoss/MGS3-Master-Collection-Trainer)
  for independent confirmation of equipped offsets and inventory layout
