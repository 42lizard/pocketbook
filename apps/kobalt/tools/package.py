#!/usr/bin/env python3
"""Create an opt-in PocketBook package without reader data or firmware files."""
import argparse
from pathlib import Path
import zipfile

LAUNCHER = b'''#!/bin/sh
if [ "$#" -eq 0 ]; then
    exec /ebrmain/bin/bookshelf.app
fi
root=/mnt/ext1/system/kobalt
mkdir -p "$root"
if [ -f "$root/app.log" ] && [ "$(wc -c < "$root/app.log")" -gt 65536 ]; then
    mv "$root/app.log" "$root/app.previous.log"
fi
exec "$root/bin/kobalt" "$@" >> "$root/app.log" 2>&1
'''


def create(binary, assets, notices, output):
    if Path(assets).is_symlink() or not Path(assets).is_dir():
        raise ValueError('Assets must be a regular directory')
    files = [(Path(binary), 'system/kobalt/bin/kobalt')]
    for path in sorted(Path(assets).rglob('*')):
        if path.is_symlink():
            raise ValueError(f'Linked asset is not allowed: {path}')
        if path.is_file():
            files.append((path, 'system/kobalt/assets/' + path.relative_to(assets).as_posix()))
    files.extend((Path(path), 'system/kobalt/notices/' + Path(path).name) for path in notices)
    names = [name for _, name in files]
    if len(names) != len(set(names)) or not Path(assets, 'manifest.json').is_file():
        raise ValueError('Duplicate package path or missing asset manifest')
    for path, _ in files:
        if path.is_symlink() or not path.is_file():
            raise ValueError(f'Not a regular package input: {path}')
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        entry = zipfile.ZipInfo('applications/kobalt.app')
        entry.external_attr = 0o100755 << 16
        archive.writestr(entry, LAUNCHER)
        for path, name in files:
            entry = zipfile.ZipInfo(name)
            entry.external_attr = (0o100755 if name.endswith('/bin/kobalt') else 0o100644) << 16
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, path.read_bytes())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--assets', required=True, type=Path)
    parser.add_argument('--notice', action='append', required=True, type=Path)
    parser.add_argument('--notices-dir', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.notices_dir:
        if not args.notices_dir.is_dir() or args.notices_dir.is_symlink():
            parser.error('--notices-dir must be a regular directory')
        args.notice.extend(sorted(args.notices_dir.iterdir()))
    create(args.binary, args.assets, args.notice, args.output)
