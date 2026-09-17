#!/usr/bin/env python3
"""Give a native download/progress/application check its own EPUB and directory."""
from pathlib import Path
import subprocess
import tempfile
from test_support import executable, create_epub

binary = executable()
with tempfile.TemporaryDirectory(prefix='readest-epub-test-') as folder:
    original = Path(folder) / 'original.epub'
    create_epub(original)
    subprocess.run([str(binary), folder, str(original)], check=True)
