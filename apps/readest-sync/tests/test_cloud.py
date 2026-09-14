#!/usr/bin/env python3
"""Run the C++ auth/session tests using a deterministic fake transport."""
from pathlib import Path
import os
import subprocess
import tempfile

APP = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='readest-cloud-tests-') as folder:
    binary = Path(folder) / 'cloud-test'
    command = [os.environ.get('HOST_CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra', '-Werror',
               '-I' + str(APP / 'src'), str(APP / 'src/cloud.cpp'),
               str(APP / 'src/library.cpp'),
               str(APP / 'tests/cloud_test.cpp'), '-o', str(binary)]
    if Path('/opt/homebrew/include/json-c/json.h').exists():
        command += ['-isystem', '/opt/homebrew/include', '-L/opt/homebrew/lib']
    subprocess.run(command + ['-ljson-c'], check=True)
    subprocess.run([str(binary), folder], check=True)
    print('Cloud authentication/session checks passed.')
