import pathlib

EXE = 'METAL GEAR SOLID3.exe'
for p in pathlib.Path('/proc').glob('[0-9]*'):
    try:
        maps = (p / 'maps').read_text()
    except OSError:
        continue
    bases = [int(l.split('-')[0], 16) for l in maps.splitlines()
             if l.endswith('/' + EXE) and l.split()[2] == '00000000']
    if bases:
        base = min(bases)
        proc = p
        break

with (proc / 'mem').open('rb', buffering=0) as m:
    m.seek(base + 0x10E80)
    data = m.read(0x1000)
open('/tmp/dispatch.bin', 'wb').write(data)
with (proc / 'mem').open('rb', buffering=0) as m:
    m.seek(base + 0x36B900)
    data = m.read(0x300)
open('/tmp/refresh_sender.bin', 'wb').write(data)
print('ok')
