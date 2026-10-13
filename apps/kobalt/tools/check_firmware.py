#!/usr/bin/env python3
"""Check the reader's direct imports against real, extracted target firmware."""
import argparse
from pathlib import Path
import re
import subprocess


def elf(path, *options):
    return subprocess.check_output(['arm-linux-gnueabi-readelf', *options, str(path)], text=True)


def symbols(path):
    for line in elf(path, '--wide', '--dyn-syms').splitlines():
        fields = line.split()
        if len(fields) >= 8 and fields[0].rstrip(':').isdigit():
            yield fields[4], fields[6], fields[7]


def check(binary, directories):
    header = elf(binary, '-h')
    if 'Machine:                           ARM' not in header or 'soft-float ABI' not in header:
        raise ValueError('Expected ARM soft-float ABI')
    if '/lib/ld-linux.so.3' not in elf(binary, '-l'):
        raise ValueError('Wrong runtime loader')
    dynamic = elf(binary, '-d')
    if 'RPATH' in dynamic or 'RUNPATH' in dynamic:
        raise ValueError('Build paths must not be embedded')
    libraries = {}
    exports = set()
    for directory in directories:
        for path in sorted(directory.iterdir()):
            if path.is_symlink() or not path.is_file():
                continue
            with path.open('rb') as stream:
                if stream.read(4) != b'\x7fELF':
                    continue
            soname = re.search(r'\(SONAME\).*\[(.*?)\]', elf(path, '-d'))
            if soname:
                libraries[soname[1]] = path
            for binding, index, name in symbols(path):
                if index != 'UND' and binding in ('GLOBAL', 'WEAK'):
                    exports.add(name.replace('@@', '@'))
                    if '@@' in name:
                        exports.add(name.split('@@')[0])
    needed = set(re.findall(r'\(NEEDED\).*\[(.*?)\]', dynamic))
    missing_libraries = needed - libraries.keys()
    imports = {name for binding, index, name in symbols(binary) if binding == 'GLOBAL' and index == 'UND'}
    missing_symbols = imports - exports
    if missing_libraries or missing_symbols:
        raise ValueError(f'Missing libraries: {sorted(missing_libraries)}\nMissing symbols: {sorted(missing_symbols)}')
    print(f'{binary}: {len(needed)} direct libraries and {len(imports)} strong imports match firmware; ARM softfp, loader and paths passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--library-dir', type=Path, action='append', required=True)
    args = parser.parse_args()
    check(args.binary, args.library_dir)
