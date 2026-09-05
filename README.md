# qcamo

Proof of concept for changing camouflage during gameplay in the PC Master
Collection release of Metal Gear Solid 3.

`F7` opens the quick menu during gameplay. Use Up/Down to select an owned
uniform, Enter to equip, and Escape or F7 to close. Input is keyboard only for
now; the prompts are drawn with the game's controller art. `F6` remains a test
shortcut which toggles Olive Drab and Tiger Stripe.

Enter accepts one change every four seconds. During reload/settle time, Enter
does nothing and menu stays open.

Uniform changes run through game's uniform and face-paint asset pipelines, so
Snake's model and camouflage index both update.

## Install

1. Install an ASI loader for MGS3.
2. Copy `qcamo.asi` beside `METAL GEAR SOLID3.exe`.
3. Start gameplay and press `F7`.

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
