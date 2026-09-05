# qcamo

Proof of concept for changing camouflage during gameplay in the PC Master
Collection release of Metal Gear Solid 3.

Current milestone binds `F6` to toggle between Olive Drab and Tiger Stripe.
It runs the game's uniform and face-paint asset pipelines, so Snake's model and
camouflage index both update. Repeated changes were verified in live gameplay.

## Install

1. Install an ASI loader for MGS3.
2. Copy `qcamo.asi` beside `METAL GEAR SOLID3.exe`.
3. Start gameplay and press `F6`.

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

This is milestone 1, not the finished quick menu. Next work: owned-uniform
enumeration, controller input, selection UI, face-paint selection, and build
signatures instead of fixed RVAs.

See [docs/research.md](docs/research.md) for verified game protocol.
