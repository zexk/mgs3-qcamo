#!/usr/bin/env python3
"""Pause-flag hunt for the MGS3 weapon/item wheel.

Takes three snapshots of the game's readable non-executable memory:
  A = normal gameplay, B = wheel held open, C = wheel released.
Bytes that flip A->B and restore B->C ("pulses") are pause-flag candidates.

Run on the Proton host while the game is running:

  python scripts/pause_probe.py

Then in game: stand somewhere safe, press Enter here for A, hold the
weapon wheel open, press Enter for B, release the wheel, press Enter
for C. Repeat per wheel (weapon, then item).

Options:
  --out DIR      save raw snapshots A/B/C plus candidates report
  --max N        show top N candidates (default 60)
  --self-test    run the diff unit check without a game

Read-only: uses /proc/<pid>/mem like scripts/probe.py.
"""
import argparse
import pathlib
import struct
import sys


EXE = 'METAL GEAR SOLID3.exe'


def find_game():
    for process in pathlib.Path('/proc').glob('[0-9]*'):
        try:
            maps = (process / 'maps').read_text()
        except OSError:
            continue
        regions = [line for line in maps.splitlines() if EXE in line]
        if any(line.split()[2] == '00000000' and line.endswith(EXE) for line in regions):
            bases = [int(line.split('-')[0], 16) for line in regions
                     if line.split()[2] == '00000000']
            return process, min(bases), maps
    raise RuntimeError(f'Running {EXE} not found')


def data_regions(maps):
    """Readable, non-executable regions of the game module (flags live here)."""
    out = []
    for line in maps.splitlines():
        if EXE not in line:
            continue
        parts = line.split()
        span, perms = parts[0], parts[1]
        if 'r' not in perms or 'x' in perms:
            continue
        start, end = (int(v, 16) for v in span.split('-'))
        if end - start == 0 or end - start > 256 * 1024 * 1024:
            continue
        out.append((start, end, perms))
    return out


def snapshot(process, regions):
    data = {}
    with (process / 'mem').open('rb', buffering=0) as memory:
        for start, end, perms in regions:
            try:
                memory.seek(start)
                blob = memory.read(end - start)
            except (OSError, ValueError):
                continue
            if len(blob) != end - start:
                continue
            data[start] = blob
    return data


def pulses(a, b, c):
    """Yield (address, byte_a, byte_b) for bytes that flip A->B, restore B->C."""
    for start in a:
        if start not in b or start not in c:
            continue
        ba, bb, bc = a[start], b[start], c[start]
        size = min(len(ba), len(bb), len(bc))
        for i in range(size):
            if ba[i] != bb[i] and bb[i] != bc[i] and ba[i] == bc[i]:
                yield start + i, ba[i], bb[i]


def group_runs(hits):
    runs = []
    for addr, va, vb in sorted(hits):
        if runs and addr == runs[-1][0] + runs[-1][3]:
            r = runs[-1]
            r[1].append(va)
            r[2].append(vb)
            r[3] += 1
        else:
            runs.append([addr, [va], [vb], 1])
    return runs


def describe(addr, va, vb, base):
    line = f'  rva={addr - base:08x} size={len(va):2d} A={" ".join(f"{v:02x}" for v in va)} B={" ".join(f"{v:02x}" for v in vb)}'
    if len(va) == 4:
        fa, fb = struct.unpack('<f', bytes(va))[0], struct.unpack('<f', bytes(vb))[0]
        if all(abs(v) < 1e6 for v in (fa, fb)):
            line += f'  float {fa:.4g} -> {fb:.4g}'
    if len(va) in (2, 4, 8):
        ia = int.from_bytes(va, 'little')
        ib = int.from_bytes(vb, 'little')
        line += f'  int {ia} -> {ib}'
    return line


def prompt(name):
    input(f'>>> {name}: press Enter to capture... ')
    print(f'    captured {name}', flush=True)


def self_test():
    a = {0x1000: bytes([1, 2, 3, 4, 5])}
    b = {0x1000: bytes([1, 9, 9, 4, 5])}
    c = {0x1000: bytes([1, 2, 3, 4, 5])}
    runs = group_runs(pulses(a, b, c))
    assert len(runs) == 1 and runs[0][0] == 0x1001 and runs[0][3] == 2, runs
    print('Pause-probe checks passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=pathlib.Path)
    parser.add_argument('--max', type=int, default=60)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return

    process, base, maps = find_game()
    regions = data_regions(maps)
    total = sum(end - start for start, end, _ in regions)
    print(f'pid={process.name} base={base:x} data-regions={len(regions)} bytes={total} (rva = addr - base)')
    print('Stand somewhere safe with no enemies nearby.')
    prompt('snapshot A (normal gameplay)')
    a = snapshot(process, regions)
    prompt('snapshot B (weapon/item wheel HELD OPEN)')
    b = snapshot(process, regions)
    prompt('snapshot C (wheel released, gameplay again)')
    c = snapshot(process, regions)

    runs = group_runs(pulses(a, b, c))
    # Most pause-like first: single-byte 0/1 flips, then small runs.
    runs.sort(key=lambda r: (0 if r[3] == 1 and sorted(r[1] + r[2]) == [0, 1] else 1, r[3]))
    print(f'pulse candidates: {len(runs)} (flip open, restore on close)')
    for addr, va, vb, _ in runs[:args.max]:
        print(describe(addr, va, vb, base))

    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        for name, snap in (('A', a), ('B', b), ('C', c)):
            with (args.out / f'snap_{name}.bin').open('wb') as f:
                for start in sorted(snap):
                    f.write(struct.pack('<QQ', start - base, len(snap[start])))
                    f.write(snap[start])
        with (args.out / 'candidates.txt').open('w') as f:
            f.write(f'pid={process.name} base={base:x}\n')
            for addr, va, vb, _ in runs:
                f.write(describe(addr, va, vb, base) + '\n')
        print(f'saved snapshots + candidates to {args.out}')

    if not runs:
        print('No pulses. Retake with the wheel held longer, or try the other wheel.',
              file=sys.stderr)
        return
    print('Send back the top lines (or the candidates file) and which wheel was held.')


if __name__ == '__main__':
    main()
