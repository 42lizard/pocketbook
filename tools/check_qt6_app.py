#!/usr/bin/env python3
"""Check the Qt6 ARM ABI and the dependency ceiling of our inspected InkPad firmware."""
import re
import subprocess
import sys
from pathlib import Path
binary = Path(sys.argv[1])
def readelf(*args):
    return subprocess.check_output(['arm-linux-gnueabi-readelf', *args, str(binary)], text=True)
assert 'Machine:                           ARM' in readelf('-h')
assert '/lib/ld-linux.so.3' in readelf('-l')
assert 'Tag_ABI_VFP_args' not in readelf('-A'), 'Hard-float ABI is incompatible'
dynamic = readelf('-d')
for name in ('libQt6Core.so.6', 'libQt6Qml.so.6', 'libQt6Quick.so.6', 'libinkview.so'):
    assert name in dynamic, f'Missing {name}'
assert 'RPATH' not in dynamic and 'RUNPATH' not in dynamic
symbols = readelf('--wide', '--dyn-syms')
assert 'qt_resourceFeatureZstd' not in symbols
for prefix, ceiling in [('GLIBC', (2,39)), ('GLIBCXX', (3,4,32)), ('CXXABI', (1,3,14)), ('OPENSSL', (3,0,0))]:
    versions = {tuple(map(int, v.split('.'))) for v in re.findall(r'@'+prefix+r'_([\d.]+)', symbols)}
    assert not versions or max(versions) <= ceiling, f'{prefix} version too new: {versions}'
if binary.name == 'readest-sync.app':
    assert 'libcrypto.so.3' in dynamic
    assert not re.search(r'\bGLOBAL\s+DEFAULT\s+\d+\s+(?:EVP_|png_)', symbols), 'Bundled legacy crypto/PNG exports'
print(f'{binary}: Qt6 ARM softfp, loader, resource format and ABI ceilings passed')
