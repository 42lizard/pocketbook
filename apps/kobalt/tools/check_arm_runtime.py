#!/usr/bin/env python3
"""Run the real ARM reader test with firmware Qt/libc and an offscreen display.

Requires prepare_firmware.py --runtime-test output, the SDK compiler/readelf,
and qemu-arm. Hardware calls are replaced; this does not validate the native QPA,
physical keys, refresh or the device task manager.
"""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]


def run(firmware, binary, output, qemu):
    firmware, binary, output = firmware.resolve(), binary.resolve(), output.resolve()
    libraries = [firmware / 'runtime/lib', firmware / 'rootfs/lib', firmware / 'rootfs/usr/lib']
    for directory in libraries:
        for library in list(directory.glob('*.so*')):
            if not library.is_file() or library.is_symlink():
                continue
            result = subprocess.run(['arm-linux-gnueabi-readelf', '-d', str(library)], capture_output=True, text=True)
            soname = re.search(r'\(SONAME\).*\[(.*?)\]', result.stdout)
            if soname:
                name = soname[1]
                if Path(name).name != name:
                    raise ValueError('Invalid library SONAME')
                alias = directory / name
                if not alias.exists():
                    alias.symlink_to(library.name)
    output.mkdir(parents=True, exist_ok=True)
    shim = output / 'inkview-stub.so'
    subprocess.run(['arm-linux-gnueabi-gcc', '-shared', '-fPIC',
                    str(ROOT / 'external/kobalt/tests/pocketbook/inkview_stub.c'), '-o', str(shim)], check=True)
    command = [qemu, '-L', str(firmware / 'rootfs')]
    for item in [f'LD_LIBRARY_PATH={":".join(map(str, libraries))}', f'LD_PRELOAD={shim}',
                 'QT_QPA_PLATFORM=offscreen', 'QT_LOGGING_RULES=*.debug=false',
                 f'QT_PLUGIN_PATH={firmware / "ebrmain/plugins"}',
                 f'KOBALT_TEST_CAPTURE={output / "first-page.png"}']:
        command.extend(['-E', item])
    command.append(str(binary))
    subprocess.run(command, check=True, timeout=120)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path)
    parser.add_argument('--binary', type=Path, default=ROOT / 'external/kobalt/build/pocketbook/arm/app/pocketbook-test')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/kobalt/arm-runtime-test')
    parser.add_argument('--qemu', default='qemu-arm')
    args = parser.parse_args()
    run(args.firmware, args.binary, args.output, args.qemu)
