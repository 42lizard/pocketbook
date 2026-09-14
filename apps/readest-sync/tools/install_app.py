#!/usr/bin/env python3
"""Install only the two reviewed package files, with backups and read-only DB checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile

TARGETS = ('system/readest-sync/ca-certificates.crt', 'applications/readest-sync.app')
PROTECTED = ('system/explorer-3/explorer-3.db', 'system/config/books.db',
             'Books/Readest/readest-sync-probe.epub')

def digest(path):
    if path.is_symlink() or not path.is_file():
        raise RuntimeError(f'Missing or unsafe file: {path}')
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def install(package, mount, expected_app_hash, backup_parent):
    package, mount = Path(package).absolute(), Path(mount).absolute()
    if mount.name != 'PB743G' or mount.resolve() != mount or not mount.is_dir():
        raise RuntimeError('Expected the connected PB743G storage directory')
    manifest = json.loads((package / 'manifest.json').read_text())
    if set(manifest) != set(TARGETS):
        raise RuntimeError('Package must contain exactly the reviewed executable and CA bundle')
    for relative in TARGETS:
        if digest(package / relative) != manifest[relative]:
            raise RuntimeError('Package checksum mismatch')
        target = mount / relative
        if target.parent.resolve() != target.parent or not target.parent.is_dir() or target.is_symlink():
            raise RuntimeError('Unsafe or missing app destination')
    if digest(mount / 'applications/readest-sync.app') != expected_app_hash:
        raise RuntimeError('Installed app changed: inspect it before proceeding')
    before = {relative: digest(mount / relative) for relative in PROTECTED}
    backup = Path(tempfile.mkdtemp(prefix='readest-app-install-', dir=backup_parent))
    previous = {}
    for relative in TARGETS:
        target = mount / relative
        previous[relative] = digest(target) if target.exists() else None
        if target.exists():
            saved = backup / relative
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(target, saved)
            if digest(saved) != previous[relative]:
                raise RuntimeError('Backup verification failed; device files are unchanged')
    audit = {'package': str(package), 'mount': str(mount), 'previous': previous,
             'new': manifest, 'protectedBefore': before, 'status': 'backed-up'}
    audit_file = backup / 'audit.json'
    audit_file.write_text(json.dumps(audit, indent=2) + '\n')
    staged = []
    try:
        for relative in TARGETS:
            fd, name = tempfile.mkstemp(prefix='.readest-install-', dir=(mount / relative).parent)
            staged.append((Path(name), mount / relative))
            with os.fdopen(fd, 'wb') as output, (package / relative).open('rb') as source:
                shutil.copyfileobj(source, output)
                output.flush()
                os.fsync(output.fileno())
            os.chmod(name, 0o755 if relative.endswith('.app') else 0o644)
            if digest(Path(name)) != manifest[relative]:
                raise RuntimeError('Staged copy checksum mismatch')
        for relative in TARGETS:
            current = digest(mount / relative) if (mount / relative).exists() else None
            if current != previous[relative]:
                raise RuntimeError('Installed files changed during preparation')
        for temporary, target in staged:
            os.replace(temporary, target)
        after = {relative: digest(mount / relative) for relative in PROTECTED}
        if after != before:
            raise RuntimeError('Protected files changed during installation; retain audit and inspect device')
        for relative in TARGETS:
            if digest(mount / relative) != manifest[relative]:
                raise RuntimeError('Installed file checksum mismatch')
        audit.update(status='installed', protectedAfter=after)
    except Exception as error:
        audit.update(status='incomplete', error=str(error))
        raise RuntimeError(f'Installation incomplete; backups and audit: {backup}') from error
    finally:
        for temporary, _ in staged:
            temporary.unlink(missing_ok=True)
        audit_file.write_text(json.dumps(audit, indent=2) + '\n')
    return backup

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--mount', type=Path, default=Path('/Volumes/PB743G'))
    parser.add_argument('--expected-app-sha256', required=True)
    parser.add_argument('--backup-parent', type=Path, default=Path(tempfile.gettempdir()))
    args = parser.parse_args()
    print(install(args.package, args.mount, args.expected_app_sha256, args.backup_parent))

if __name__ == '__main__':
    main()
