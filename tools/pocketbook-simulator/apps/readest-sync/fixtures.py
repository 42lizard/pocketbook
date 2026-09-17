#!/usr/bin/env python3
"""Generate disposable EPUBs from our existing position probe; no firmware input."""
from pathlib import Path
import importlib.util
import copy
import io
import sys
import zipfile

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('probe', Path(__file__).parents[4] / 'apps/readest-sync/tools/package_probe.py')
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
buffer = io.BytesIO()
probe.create_epub(buffer)
with zipfile.ZipFile(buffer) as source:
    for number in range(52):
        with zipfile.ZipFile(root / f'{number:02}.epub', 'w') as target:
            for info in source.infolist():
                data = source.read(info)
                if info.filename == 'EPUB/package.opf':
                    data = data.replace(b'Readest Sync', f'Simulator Book {number + 1:02}'.encode())
                target.writestr(copy.copy(info), data)
(root / 'ready').write_text('52 synthetic EPUBs\n')
