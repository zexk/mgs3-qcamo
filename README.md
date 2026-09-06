# qcamo

Proof of concept for changing camouflage during gameplay in the PC Master
Collection release of Metal Gear Solid 3.

Hold `G` during gameplay to bring up the quick menu. `W`/`S` or Up/Down select
a row, Enter equips it, and releasing `G` closes the menu without changing
anything. `F6` remains a test shortcut which toggles Olive Drab and Tiger
Stripe.

Each row is a whole set: an owned uniform together with the face paint that
scores best where Snake stands, and what equipping the pair would add to or
take off his camouflage index. Rows are sorted best first, so the cursor opens
on the best set available. Face paint scores independently of the uniform in
the game's own arithmetic, so the same face paint is right for every row and
the top row really is the best pairing, not a guess at one.

On a pad, triangle plus L1 opens the menu in either order, L1 alone keeps it
up, the D-pad moves the selection, cross equips, and releasing L1 closes. Every
pad button is already spoken for by the game, which is why opening needs a
chord.

Pad input comes from Steam Input, because that is the only place it exists: the
game imports no input API, and XInput, winmm and DirectInput all enumerate
nothing while Steam holds the device. The menu reads the same digital actions
the game reads, so it needs no controller configuration of its own. See
`docs/research.md` for how the actions were identified.

On the keyboard no chord is needed: `G` is free in both of the game's layouts,
as are the arrow keys and Enter, and Enter is what the game's own keyboard
prompts show for cross.

The menu opens only during playable gameplay. Cutscenes, the Survival Viewer,
other pause states and non-stage screens all refuse it, using the same player
state test the game's own wheel popups make before they open.

Opening quick menu uses same semi-pause as weapon and item wheels. World and
Snake stop; audio and menu movement continue. `W`/`S` therefore navigate
without moving Snake.

Changes are gated by one frame rather than a timer, so swaps can be made as
fast as they can be selected.

The menu uses the game's own sounds: the weapon wheel's open and the Survival
Viewer's cursor, decide and back, with the game's refusal sound when an equip
is rejected or the selected set is already worn.

Uniform changes run through game's uniform and face-paint asset pipelines, so
Snake's model and camouflage index both update.

## Install

1. Install an ASI loader for MGS3.
2. Copy `qcamo.asi` beside `METAL GEAR SOLID3.exe`.
3. Start gameplay and hold `G`.

Only executable timestamp `0x6980B92F` is supported. Unsupported builds log an
error and install no hooks. Runtime messages go to `qcamo.log` beside the DLL.

## Build

On NixOS:

```sh
nix build
```

Result: `result/qcamo.asi`.

Other systems can configure `CMakeLists.txt` with a MinHook source directory
in `minhook` and a 64-bit MinGW toolchain.

## Status

Phase 2 is complete. The menu draws through a D3D11 overlay in the game's own
HUD style: camouflage swatches are the Survival Viewer icon tiles, text uses the
game's HUD font atlas, and panel colours are sampled from the Viewer and the
equipment HUD.

Changes now survive area transitions. The mod used to copy the Survival
Viewer's `set_alloc_heap(0)` / `set_alloc_heap(1)` bracket around its message
dispatches, which is only correct inside the Viewer; during gameplay it left
the allocator pointing at the wrong arena and the next stage load never
finished. It now restores the value that was there.

Next work: freezing gameplay while the menu is open, and the final input
scheme.

See [docs/research.md](docs/research.md) for verified game protocol.
