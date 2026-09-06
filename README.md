# qcamo

Quick camouflage switching during gameplay for the PC Master Collection release
of Metal Gear Solid 3, in the style of the Delta remake's camo menu.

Hold a key and the menu appears beside Snake, listing what he can wear, best
camouflage first. Pick a row, equip it, release. The game is never paused into
a submenu, and nothing is loaded that the game would not load itself.

## Controls

Keyboard: hold `G` to open. `W`/`S` or Up/Down select, Enter equips, releasing
`G` closes without changing anything. `G` is free in both of the game's own
layouts, as are the arrow keys, and Enter is what the game's keyboard prompts
already show for cross.

Pad: triangle plus L1 opens, in either order. L1 alone keeps it up, the D-pad
moves the selection, cross equips, releasing L1 closes. Opening needs a chord
because every pad button is already spoken for by the game.

## What a row means

Each row is a complete set: an owned uniform together with the face paint that
scores best where Snake is standing, and the difference equipping that pair
would make to his camouflage index.

The same face paint appears on every row. That is not a shortcut. The game adds
the uniform's contribution and the face paint's separately, so the best face
paint is the same whichever uniform is worn, and the top row is therefore the
best set available rather than an estimate of one.

Rows are sorted best first and frozen while the menu is open, so the row under
the cursor cannot move as Snake's footing changes.

## Behaviour

The menu opens only during playable gameplay. Cutscenes, the Survival Viewer,
other pause states and non-stage screens all refuse it, using the same player
state test the game's own weapon and item wheels make before they open.

While open it holds the same semi-pause those wheels use: the world and Snake
stop, audio and menu movement continue, so `W`/`S` navigate without moving
Snake.

Changes go through the game's own uniform and face paint asset pipelines, so
the model and the camouflage index both update, and they survive area
transitions. A change is gated by a single frame rather than a timer, so swaps
are as fast as they can be selected.

Sounds are the game's own: the weapon wheel's open, the Survival Viewer's
cursor, decide and back, and the game's refusal sound when an equip is rejected
or the selected set is already worn.

Artwork is the game's own too. Swatches are the Survival Viewer's camouflage
and face paint icons, text is drawn from the game's HUD font atlas, and the
panel colours are sampled from the Viewer and the equipment HUD.

## Install

1. Install an ASI loader for MGS3.
2. Copy `qcamo.asi` beside `METAL GEAR SOLID3.exe`.
3. Start gameplay and hold `G`.

Only executable timestamp `0x6980B92F` is supported. Other builds log an error
and install no hooks. Runtime messages go to `qcamo.log` beside the DLL.

Pad input comes from Steam Input, which is the only place it exists: the game
imports no input API, and XInput, winmm and DirectInput all enumerate nothing
while Steam holds the device. The menu reads the same digital actions the game
reads, so it needs no controller configuration of its own.

## Build

On NixOS:

```sh
nix build
```

Result: `result/qcamo.asi`.

Other systems can configure `CMakeLists.txt` with a MinHook source directory in
`minhook` and a 64-bit MinGW toolchain.

## Research

[docs/research.md](docs/research.md) records the game internals this depends
on: the camouflage index calculation, the uniform and face paint asset and
inventory tables, the change protocol, the HUD font and icon tables, the sound
cues, and the player state flags the gate reads. Addresses are RVAs against
executable timestamp `0x6980B92F`.
