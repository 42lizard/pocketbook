#!/usr/bin/env python3
"""Run the native scale check with 500 disposable EPUBs."""
from pathlib import Path
import subprocess
import tempfile
import zipfile
from test_support import executable, create_epub

binary = executable()
with tempfile.TemporaryDirectory(prefix='readest-scale-') as folder:
    root = Path(folder)
    original = root / 'original.epub'
    create_epub(original)
    scale_root = root / 'scale'
    scale_root.mkdir()
    with zipfile.ZipFile(original) as source:
        entries = [(info, source.read(info.filename)) for info in source.infolist()]
    for number in range(500):
        with zipfile.ZipFile(scale_root / f'{number:03}.epub', 'w') as target:
            for info, content in entries:
                if info.filename.endswith('.opf'):
                    content = content.replace(b'</dc:title>', f' {number}</dc:title>'.encode())
                target.writestr(info, content)
            target.writestr('scale-payload.txt', str(number).encode().ljust(1024 * 1024, b'x'))
    subprocess.run([str(binary), str(scale_root), str(scale_root / '499.epub')], check=True)
