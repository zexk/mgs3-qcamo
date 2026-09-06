# qcamo

Proof of concept for changing camouflage during gameplay in the PC Master
Collection release of Metal Gear Solid 3.

Hold `G` during gameplay to bring up the quick menu. `W`/`S` or Up/Down select
an owned uniform, Enter equips it, and releasing `G` closes the menu without
changing anything. `F6` remains a test shortcut which toggles Olive Drab and Tiger
Stripe.

On a pad, triangle plus L1 opens the menu, L1 alone keeps it up, the D-pad or
left stick moves the selection, cross equips, and releasing L1 closes. Every
pad button is already spoken for by the game, which is why opening needs a
chord. Pad input goes through XInput, resolved at run time, so a missing
runtime costs pad support rather than the whole overlay.

On the keyboard no chord is needed: `G` is free in both of the game's layouts,
as are the arrow keys and Enter, and Enter is what the game's own keyboard
prompts show for cross.

Opening quick menu uses same semi-pause as weapon and item wheels. World and
Snake stop; audio and menu movement continue. `W`/`S` therefore navigate
without moving Snake.

Enter accepts one change every four seconds. During reload/settle time, Enter
does nothing and the menu stays open.

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
equipment HUD. Next work: camouflage-score comparisons, face-paint
combinations, and final input scheme.

See [docs/research.md](docs/research.md) for verified game protocol.
