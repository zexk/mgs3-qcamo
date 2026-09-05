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

## External references

- [Konami MGS3 manual](https://metalgear.konami.net/manual/mc1/mgs3/pc/en/page15.html)
- [MGSHDFix](https://github.com/Lyall/MGSHDFix) for independently named stats fields
- [ANTIBigBoss trainer](https://github.com/ANTIBigBoss/MGS3-Master-Collection-Trainer)
  for independent confirmation of equipped offsets and inventory layout
