#!/usr/bin/env python3
"""Add Kobalt to PocketBook's EPUB 'Open with' choices, preserving the default."""
import argparse
from pathlib import Path
import os
import shutil
import tempfile

# PB743K3 U743k3.6.10.2854 /ebrmain/config/extensions.cfg.
DEFAULT_EPUB = 'epub:@EPUB_file:1:eink-reader_with_blink.app,eink-reader_with_epub2.app:ICON_EPUB'


def register(text):
    lines = text.splitlines(keepends=True)
    entries = [i for i, line in enumerate(lines) if line.startswith('epub:')]
    if len(entries) > 1:
        raise ValueError('Multiple EPUB associations; resolve them before registering Kobalt')
    if not entries:
        if lines and not lines[-1].endswith(('\n', '\r')):
            lines[-1] += '\n'
        entries = [len(lines)]
        lines.append(DEFAULT_EPUB + '\n')
    index = entries[0]
    line = lines[index]
    content = line.rstrip('\r\n')
    fields = content.split(':')
    if len(fields) != 5 or not fields[3] or any(not app for app in fields[3].split(',')):
        raise ValueError('Malformed EPUB association')
    if 'kobalt.app' not in fields[3].split(','):
        fields[3] += ',kobalt.app'
    lines[index] = ':'.join(fields) + line[len(content):]
    return ''.join(lines)


def install(config):
    if config.is_symlink():
        raise ValueError('Refusing a linked extensions.cfg')
    before = config.read_bytes() if config.exists() else b''
    after = register(before.decode('utf-8')).encode('utf-8')
    if before == after:
        return
    backup = config.with_name(config.name + '.before-kobalt')
    if config.exists() and not backup.exists():
        with backup.open('xb') as stream:
            stream.write(before)
            stream.flush()
            os.fsync(stream.fileno())
        shutil.copystat(config, backup)
    with tempfile.NamedTemporaryFile(dir=config.parent, prefix='.extensions-kobalt-', delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(after)
        stream.flush()
        os.fsync(stream.fileno())
    try:
        if (config.read_bytes() if config.exists() else b'') != before:
            raise RuntimeError('extensions.cfg changed during registration')
        temporary.chmod(config.stat().st_mode if config.exists() else 0o644)
        temporary.replace(config)
        if config.read_bytes() != after:
            raise OSError('Registration verification failed')
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('device', type=Path, help='Mounted PocketBook volume')
    args = parser.parse_args()
    if not (args.device / 'applications/kobalt.app').is_file():
        parser.error('Install Kobalt before registering it')
    install(args.device / 'system/config/extensions.cfg')
    print('Kobalt is available under Open with; the current default reader is preserved.')
