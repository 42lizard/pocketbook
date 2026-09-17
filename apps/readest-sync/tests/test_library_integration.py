#!/usr/bin/env python3
"""Exercise uploads with an EPUB containing an embedded JPEG cover."""
from pathlib import Path
import subprocess
import tempfile
import zipfile
from test_support import executable, create_epub

with tempfile.TemporaryDirectory(prefix='readest-library-test-') as folder:
    original = Path(folder) / 'original.epub'
    create_epub(original)
    with zipfile.ZipFile(original) as archive:
        entries = [(info, archive.read(info)) for info in archive.infolist()]
    with zipfile.ZipFile(original, 'w') as archive:
        for info, data in entries:
            if info.filename == 'EPUB/package.opf':
                data = data.replace(b'</manifest>', b'<item id="cover" href="cover.jpg" '
                                    b'media-type="image/jpeg" properties="cover-image"/></manifest>')
            archive.writestr(info, data)
        archive.writestr('EPUB/cover.jpg', b'\xff\xd8\xff\xd9')
    subprocess.run([str(executable()), folder, str(original)], check=True)
