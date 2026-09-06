import pathlib
import struct
import sys

EXE = 'METAL GEAR SOLID3.exe'
want = int(sys.argv[1], 16)

for p in pathlib.Path('/proc').glob('[0-9]*'):
    try:
        maps = (p / 'maps').read_text()
    except OSError:
        continue
    mod = [l for l in maps.splitlines() if EXE in l]
    if any(l.split()[2] == '00000000' and l.endswith(EXE) for l in mod):
        proc = p
        base = min(int(l.split('-')[0], 16) for l in mod
                   if l.split()[2] == '00000000')
        break

needle = struct.pack('<Q', want)
hits = []
with (proc / 'mem').open('rb', buffering=0) as m:
    for line in maps.splitlines():
        if EXE not in line:
            continue
        parts = line.split()
        if 'r' not in parts[1]:
            continue
        start, end = (int(v, 16) for v in parts[0].split('-'))
        if end - start > 64 * 1024 * 1024:
            continue
        try:
            m.seek(start)
            blob = m.read(end - start)
        except (OSError, ValueError):
            continue
        if len(blob) != end - start:
            continue
        off = 0
        while True:
            i = blob.find(needle, off)
            if i < 0:
                break
            hits.append(start + i)
            off = i + 1
print(f'pid={proc.name} base={base:x} want={want:016x}')
for h in hits[:20]:
    print(f'  rva={h - base:08x}')
print(f'{len(hits)} hits')
