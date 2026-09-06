# qcamo: MGS3 quick camouflage

Quick camouflage switching during gameplay for the PC Master Collection release
of Metal Gear Solid 3, styled after the Delta remake's camo menu.

## Features

- Owned uniforms ranked by their live camouflage gain
- Best owned face paint paired with every uniform
- Fast equipping without opening the Survival Viewer
- Wheel-style semi-pause during selection
- Game-native camouflage art, HUD font, sounds, and asset loading
- Keyboard and Steam Input controller support

Hold `G`, use `W`/`S` or `Up`/`Down`, and press Enter to equip. On a pad,
press triangle plus L1, select with the D-pad, and equip with cross. Releasing
`G` or L1 closes the menu.

The menu only opens during playable gameplay. Changes update both Snake's model
and camouflage index and survive area transitions.

## Install

Requires an ASI loader. [MGSHDFix](https://github.com/ShizCalev/MGSHDFix/releases)
provides the recommended MGS3 setup.

Copy `qcamo.asi` beside `METAL GEAR SOLID3.exe`, then start gameplay and hold
`G`.

Only executable timestamp `0x6980B92F` is supported. Runtime messages go to
`qcamo.log` beside the DLL.

## Build

On NixOS:

```sh
nix build
```

Artifact: `result/qcamo.asi`.

Other systems can configure `CMakeLists.txt` with a MinHook source directory in
`minhook` and a 64-bit MinGW toolchain.

Game internals and addresses: [docs/research.md](docs/research.md).

MIT licensed. See [LICENSE](LICENSE).
