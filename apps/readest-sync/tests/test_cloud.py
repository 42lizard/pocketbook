#!/usr/bin/env python3
"""Run the C++ auth/session tests using a deterministic fake transport."""
from test_support import executable
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='readest-cloud-tests-') as folder:
    binary = executable()
    subprocess.run([str(binary), folder], check=True)
    print('Cloud authentication/session checks passed.')
