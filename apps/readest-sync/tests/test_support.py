"""Shared fixture loading and the CTest-to-Python executable contract."""
import importlib.util
import os
from pathlib import Path


def executable():
    value = os.environ.get('READEST_TEST_BINARY')
    if not value or not Path(value).is_file():
        raise RuntimeError('Run this check through CTest after building its test target, '
                           'or set READEST_TEST_BINARY to that executable.')
    return Path(value).resolve()


def create_epub(path):
    source = Path(__file__).resolve().parents[1] / 'tools/package_probe.py'
    spec = importlib.util.spec_from_file_location('fixture', source)
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    fixture.create_epub(path)
