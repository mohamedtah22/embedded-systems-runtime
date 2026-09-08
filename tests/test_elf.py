#!/usr/bin/env python3
"""Deterministic ELF fixtures and malformed-file regression tests; no multilib needed."""
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import tempfile
import unittest

TOOL = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 else str(Path('build/mini-runtime').resolve())


def fixture(bits=32):
    names = b'\0.text\0.data\0.bss\0.rodata\0.shstrtab\0.strtab\0.symtab\0'
    strings = b'\0entry_fn\0counter\0'
    data = bytearray(0x400)
    ident = b'\x7fELF' + bytes([1 if bits == 32 else 2, 1, 1]) + bytes(9)
    base = 0x8048000 if bits == 32 else 0x400000
    phoff = 52 if bits == 32 else 64
    phsize = 32 if bits == 32 else 56
    shoff = 0x200
    shsize = 40 if bits == 32 else 64
    if bits == 32:
        header = struct.pack('<16sHHIIIIIHHHHHH', ident, 2, 3, 1, base + 0x100,
                             phoff, shoff, 0, 52, phsize, 1, shsize, 8, 5)
        ph = struct.pack('<IIIIIIII', 1, 0, base, base, 0x180, 0x200, 5, 0x1000)
        syms = bytes(16) + struct.pack('<IIIBBH', 1, base + 0x100, 4, 0x12, 0, 1)
        syms += struct.pack('<IIIBBH', 10, base + 0x180, 32, 0x11, 0, 3)
    else:
        header = struct.pack('<16sHHIQQQIHHHHHH', ident, 2, 62, 1, base + 0x100,
                             phoff, shoff, 0, 64, phsize, 1, shsize, 8, 5)
        ph = struct.pack('<IIQQQQQQ', 1, 5, 0, base, base, 0x180, 0x200, 0x1000)
        syms = bytes(24) + struct.pack('<IBBHQQ', 1, 0x12, 0, 1, base + 0x100, 4)
        syms += struct.pack('<IBBHQQ', 10, 0x11, 0, 3, base + 0x180, 32)
    data[:len(header)] = header
    data[phoff:phoff + len(ph)] = ph
    data[0x100:0x104] = b'\x90\x90\x90\xc3'
    data[0x130:0x130 + len(names)] = names
    data[0x180:0x180 + len(strings)] = strings
    data[0x1a0:0x1a0 + len(syms)] = syms
    # File bytes at the BSS conceptual offset intentionally aren't zero.
    # A translator must never return them as BSS content.
    entries = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]
    for name, typ, flags, addr, off, size, link, info, align, ent in [
        ('.text', 1, 6, base + 0x100, 0x100, 4, 0, 0, 16, 0),
        ('.data', 1, 3, base + 0x110, 0x110, 4, 0, 0, 4, 0),
        ('.bss', 8, 3, base + 0x180, 0x180, 0x80, 0, 0, 16, 0),
        ('.rodata', 1, 2, base + 0x120, 0x120, 8, 0, 0, 8, 0),
        ('.shstrtab', 3, 0, 0, 0x130, len(names), 0, 0, 1, 0),
        ('.strtab', 3, 0, 0, 0x180, len(strings), 0, 0, 1, 0),
        ('.symtab', 2, 0, 0, 0x1a0, len(syms), 6, 1, 4 if bits == 32 else 8, 16 if bits == 32 else 24),
    ]:
        entries.append((names.index(name.encode()), typ, flags, addr, off, size, link, info, align, ent))
    for i, entry in enumerate(entries):
        packed = struct.pack('<IIIIIIIIII' if bits == 32 else '<IIQQQQIIQQ', *entry)
        data[shoff + i * shsize:shoff + (i + 1) * shsize] = packed
    return data


class InspectorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temp.name)
        for bits in (32, 64):
            (cls.root / f'elf{bits}').write_bytes(fixture(bits))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_tool(self, *args, status=0):
        result = subprocess.run([TOOL, *map(str, args)], text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, status, result.stdout + result.stderr)
        self.assertNotIn('Sanitizer', result.stderr)
        self.assertNotIn('runtime error:', result.stderr)
        return result.stdout + result.stderr

    def bad(self, data, message):
        path = self.root / 'bad'
        path.write_bytes(data)
        self.assertIn(message, self.run_tool('elf', path, status=1))

    def mutate(self, offset, fmt, value, message, bits=32):
        data = fixture(bits)
        struct.pack_into(fmt, data, offset, value)
        self.bad(data, message)

    def test_headers_both_classes(self):
        for bits, arch in [(32, 'Intel 80386'), (64, 'AMD x86-64')]:
            out = self.run_tool('elf', '--header', self.root / f'elf{bits}')
            self.assertIn(f'ELF{bits}', out)
            self.assertIn(arch, out)
            self.assertIn('Magic: 7f 45 4c 46', out)

    def test_sections(self):
        for bits in (32, 64):
            out = self.run_tool('elf', '--sections', self.root / f'elf{bits}')
            for name in ('.text', '.data', '.bss', '.rodata', 'NOBITS', 'AW-', 'A-X'):
                self.assertIn(name, out)

    def test_symbols(self):
        for bits in (32, 64):
            out = self.run_tool('elf', '--symbols', self.root / f'elf{bits}')
            for name in ('entry_fn', 'counter', 'FUNC', 'OBJECT', 'GLOBAL'):
                self.assertIn(name, out)

    def test_memory_layout(self):
        out = self.run_tool('memory', self.root / 'elf32')
        for text in ('LOAD', 'R-X', '0x0000000180', '0x0000000200', '0x80', 'no memory is mapped'):
            self.assertIn(text, out)

    def test_translation_boundaries(self):
        for bits, base in [(32, 0x8048000), (64, 0x400000)]:
            for delta in (0, 0x100, 0x17f):
                out = self.run_tool('elf', '--vaddr', hex(base + delta), self.root / f'elf{bits}')
                self.assertIn(f'file offset {hex(delta)}', out)
            for delta in (0x180, 0x1ff):
                self.assertIn('no file offset', self.run_tool('elf', '--vaddr', hex(base + delta), self.root / f'elf{bits}'))
            for delta in (-1, 0x200):
                self.assertIn('not covered', self.run_tool('elf', '--vaddr', hex(base + delta), self.root / f'elf{bits}', status=1))

    def test_missing_file(self):
        self.assertIn('open', self.run_tool('elf', self.root / 'missing', status=1))

    def test_cli_errors(self):
        for args in [('elf',), ('memory',), ('unknown',), ('elf', '--wat'),
                     ('elf', '--vaddr', '-1', 'x'), ('elf', '--vaddr', '0x10000000000000000', 'x'),
                     ('elf', '--vaddr', '123junk', 'x')]:
            self.run_tool(*args, status=2)
        self.run_tool('--help')
        self.run_tool('--version')

    def test_bad_identification(self):
        self.bad(b'bad input' * 10, 'magic')
        self.mutate(4, 'B', 3, 'class')
        self.mutate(5, 'B', 2, 'byte order')
        self.mutate(6, 'B', 0, 'version')
        self.mutate(18, '<H', 40, 'architecture')

    def test_bad_header_sizes(self):
        self.mutate(40, '<H', 1, 'header size')
        self.mutate(42, '<H', 1, 'program header table')
        self.mutate(46, '<H', 1, 'section header table')

    def test_extended_numbering_rejected(self):
        self.mutate(44, '<H', 0xffff, 'extended')
        self.mutate(48, '<H', 0, 'extended')
        self.mutate(50, '<H', 0xffff, 'extended')

    def test_out_of_range_tables(self):
        self.mutate(28, '<I', 0xfffffff0, 'program header table')
        self.mutate(32, '<I', 0xfffffff0, 'section header table')
        self.mutate(40, '<Q', 0xfffffffffffffff0, 'section header table', 64)

    def test_invalid_load(self):
        self.mutate(52 + 16, '<I', 0x300, 'filesz exceeds')
        self.mutate(52 + 4, '<I', 0xfffffff0, 'file range')
        self.mutate(52 + 28, '<I', 3, 'power of two')
        self.mutate(52 + 8, '<I', 0x8048001, 'incongruent')
        self.mutate(52 + 8, '<I', 0xffffff00, '32-bit address')
        self.mutate(64 + 16, '<Q', 0xffffffffffffff00, 'address overflow', 64)

    def test_invalid_section(self):
        self.mutate(0x200 + 40 + 16, '<I', 0xfffffff0, 'file range')
        self.mutate(0x200 + 40, '<I', 0xfffffff0, 'invalid name')
        self.mutate(50, '<H', 1, 'must use STRTAB')
        self.mutate(0x130, 'B', 1, 'string table')

    def test_invalid_symbols(self):
        self.mutate(0x200 + 7 * 40 + 36, '<I', 0, 'symbol entry size')
        self.mutate(0x200 + 7 * 40 + 24, '<I', 99, 'symbol string table')
        self.mutate(0x1a0 + 16, '<I', 999, 'invalid name')
        self.mutate(0x1a0 + 16 + 14, '<H', 99, 'section index')

    def test_no_sections(self):
        data = fixture()
        struct.pack_into('<I', data, 32, 0)
        struct.pack_into('<HHH', data, 46, 0, 0, 0)
        path = self.root / 'sectionless'
        path.write_bytes(data)
        self.assertIn('LOAD', self.run_tool('elf', path))

    def test_nobits_outside_file(self):
        data = fixture()
        struct.pack_into('<I', data, 0x200 + 3 * 40 + 16, 0x100000)
        path = self.root / 'nobits'
        path.write_bytes(data)
        self.assertIn('.bss', self.run_tool('elf', path))

    def test_overlap_is_ambiguous(self):
        data = fixture()
        struct.pack_into('<H', data, 44, 2)
        data[84:116] = data[52:84]
        path = self.root / 'overlap'
        path.write_bytes(data)
        self.assertIn('ambiguous', self.run_tool('elf', '--vaddr', '0x8048100', path, status=1))

    def test_non_load_not_translated(self):
        data = fixture()
        struct.pack_into('<I', data, 52, 4)  # PT_NOTE
        path = self.root / 'note'
        path.write_bytes(data)
        self.assertIn('not covered', self.run_tool('elf', '--vaddr', '0x8048100', path, status=1))

    def test_names_are_terminal_safe(self):
        data = fixture()
        data[0x130 + 1] = 27
        path = self.root / 'escape'
        path.write_bytes(data)
        out = self.run_tool('elf', path)
        self.assertIn('\\x1btext', out)
        self.assertNotIn('\x1b', out)

    def test_truncations(self):
        data = fixture()
        for size in (0, 1, 15, 16, 51, 52, 83, 100, 511, 700):
            with self.subTest(size=size):
                self.bad(data[:size], 'elf:')

    def test_real_binary_against_readelf(self):
        expected = subprocess.check_output(['readelf', '-hW', TOOL], text=True)
        actual = self.run_tool('elf', '--header', TOOL)
        entry = re.search(r'Entry point address:\s+(0x[0-9a-f]+)', expected).group(1)
        self.assertIn(f'Entry: {entry}', actual)
        for label, actual_label in [('Number of program headers', 'Program headers'), ('Number of section headers', 'Section headers')]:
            count = re.search(label + r':\s+(\d+)', expected).group(1)
            self.assertRegex(actual, actual_label + r':.*count=' + count + r'\b')
        segment_expected = subprocess.check_output(['readelf', '-lW', TOOL], text=True)
        segment_actual = self.run_tool('elf', '--segments', TOOL)
        self.assertEqual(len(re.findall(r'^\s*LOAD\s', segment_expected, re.M)),
                         len(re.findall(r'^\s*\d+ LOAD\s', segment_actual, re.M)))
        self.assertIn('.text', self.run_tool('elf', '--sections', TOOL))
        self.assertIn('elf_open', self.run_tool('elf', '--symbols', TOOL))

    def test_deterministic_mutations_do_not_crash(self):
        rng = random.Random(42)
        path = self.root / 'mutated'
        for i in range(80):
            data = fixture(32 if i % 2 else 64)
            for _ in range(4):
                data[rng.randrange(len(data))] = rng.randrange(256)
            path.write_bytes(data)
            result = subprocess.run([TOOL, 'elf', str(path)], capture_output=True, timeout=10)
            self.assertIn(result.returncode, (0, 1), result.stderr)
            self.assertNotIn(b'Sanitizer', result.stderr)
            self.assertNotIn(b'runtime error:', result.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
