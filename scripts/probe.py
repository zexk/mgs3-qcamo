#!/usr/bin/env python3
"""Read-only Proton MGS3 probe. Run with Python 3; disassembly needs capstone.

python scripts/probe.py --dump /tmp/mgs3-text.bin
python scripts/probe.py --disasm 3008c0 300bc5
"""
import argparse
import pathlib
import re
import struct


UNIFORMS = (
    'Olive Drab', 'Tiger Stripe', 'Leaf', 'Tree Bark', 'Choco Chip',
    'Splitter', 'Raindrop', 'Squares', 'Water', 'Black', 'Snow', 'Naked',
    'Sneaking Suit', 'Scientist', 'Officer', 'Maintenance', 'Tuxedo',
    'Hornet Stripe', 'Spider', 'Moss', 'Fire', 'Spirit', 'Cold War',
    'Snake', 'Ga-Ko', 'Desert Tiger', 'DPM', 'Flecktarn', 'Auscam',
    'Animals', 'Fly', 'Banana', 'Downloaded')


def module_base(maps):
    addresses = [int(line.split('-')[0], 16) for line in maps.splitlines()
                 if line.endswith('/METAL GEAR SOLID3.exe')
                 and line.split()[2] == '00000000']
    if not addresses:
        raise ValueError('MGS3 module not mapped')
    return min(addresses)


def find_game():
    for process in pathlib.Path('/proc').glob('[0-9]*'):
        try:
            return process, module_base((process / 'maps').read_text())
        except (OSError, ValueError):
            pass
    raise RuntimeError('Running METAL GEAR SOLID3.exe not found')


def stats_slot(code):
    hits = list(re.finditer(
        rb'\x48\x8b\x0d....\xf7\x41\x08\x00\x40\x00\x00\x75\x09\x8b\x05',
        code, re.DOTALL))
    if len(hits) != 1:
        raise ValueError(f'Expected unique stats signature, got {len(hits)}')
    offset = hits[0].start()
    return 0x1000 + offset + 7 + struct.unpack_from('<i', code, offset + 3)[0]


def item_table(data, start_rva=0x1c00000):
    pattern = b'\x00\x00\xda\x5a\x2b\x00'
    hits = [match.start() for match in re.finditer(re.escape(pattern), data)]
    if len(hits) != 1:
        raise ValueError(f'Expected unique item-table signature, got {len(hits)}')
    return start_rva + hits[0] + len(pattern) + 12


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dump', type=pathlib.Path)
    parser.add_argument('--disasm', nargs=2, metavar=('START_RVA', 'END_RVA'))
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        assert module_base('1000-2000 r--p 00000000 00:00 0 /a/METAL GEAR SOLID3.exe') == 0x1000
        code = bytes.fromhex('48 8b 0d f9 ff ff ff f7 41 08 00 40 00 00 75 09 8b 05')
        assert stats_slot(code) == 0x1000
        for invalid in (b'', code * 2):
            try:
                stats_slot(invalid)
            except ValueError:
                continue
            raise AssertionError('Ambiguous or missing signature accepted')
        inventory = b'x' + b'\x00\x00\xda\x5a\x2b\x00' + b'y' * 12
        assert item_table(inventory, 0x2000) == 0x2013
        for invalid in (b'', inventory * 2):
            try:
                item_table(invalid)
            except ValueError:
                continue
            raise AssertionError('Ambiguous or missing item table accepted')
        print('Probe checks passed')
        return
    process, base = find_game()
    with (process / 'mem').open('rb', buffering=0) as memory:
        def read(address, size):
            memory.seek(address)
            data = memory.read(size)
            if len(data) != size:
                raise ValueError(f'Short read at {address:x}')
            return data
        pe = struct.unpack_from('<I', read(base, 64), 0x3c)[0]
        header = read(base + pe, 64)
        if header[:4] != b'PE\0\0':
            raise ValueError('Invalid PE header')
        code_size = struct.unpack_from('<I', header, 28)[0]
        code_rva = struct.unpack_from('<I', header, 44)[0]
        if code_rva != 0x1000:
            raise ValueError('Unexpected code RVA')
        code = read(base + code_rva, code_size)
        slot = stats_slot(code)
        block = struct.unpack('<Q', read(base + slot, 8))[0]
        print(f'pid={process.name} base={base:x} timestamp={struct.unpack_from("<I", header, 8)[0]:08x}')
        print(f'stats_slot={slot:x} block={block:x}')
        if block:
            state = read(block, 0x680)
            print(f'area={state[0x24:0x2b]!r} uniform={state[0x67e]} face={state[0x67f]}')
        inventory_start = 0x1c00000
        inventory = read(base + inventory_start, 0x300000)
        table = item_table(inventory, inventory_start)
        owned = [name for uniform, name in enumerate(UNIFORMS)
                 if struct.unpack('<h', read(base + table + (40 + uniform) * 80, 2))[0] >= 1]
        print(f'item_table={table:x} owned_uniforms={", ".join(owned)}')
        if args.dump:
            args.dump.write_bytes(code)
        if args.disasm:
            import capstone
            start, end = (int(value, 16) for value in args.disasm)
            if not code_rva <= start < end <= code_rva + code_size:
                raise ValueError('Disassembly range outside code')
            decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
            for instruction in decoder.disasm(code[start-code_rva:end-code_rva], start):
                print(f'{instruction.address:08x} {instruction.mnemonic:8} {instruction.op_str}')


if __name__ == '__main__':
    main()
