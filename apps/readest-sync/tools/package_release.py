#!/usr/bin/env python3
"""Create a release ZIP from the same two-file package used by the installer."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

from package_app import APP, create_package

ROOT = APP.parents[1]


def release(version, commit, output):
    if not re.fullmatch(r'(?:v[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?|dev-[0-9a-f]{12})', version):
        raise ValueError('Expected vMAJOR.MINOR.PATCH[-prerelease] or dev-<12 hex digits>')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Expected full Git commit SHA')
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    archive = output / f'readest-sync-{version}-pocketbook-qt6.zip'
    package = create_package()
    try:
        # Include only the installer's allowlisted files and release documentation.
        files = {name: (package / name).read_bytes() for name in
                 ('applications/readest-sync.app', 'system/readest-sync/ca-certificates.crt', 'manifest.json')}
        files['README.md'] = (APP / 'release/README.md').read_bytes()
        files['LICENSE'] = (ROOT / 'LICENSE').read_bytes()
        files['release.json'] = (json.dumps({'app': 'readest-sync', 'version': version,
                                           'commit': commit, 'target': 'pocketbook-qt6-arm-softfp'}, indent=2) + '\n').encode()
        files['apps/readest-sync/src/vendor/miniz/LICENSE'] = (APP / 'src/vendor/miniz/LICENSE').read_bytes()
        files['apps/readest-sync/assets/README.md'] = (APP / 'assets/README.md').read_bytes()
        for notice in sorted((ROOT / 'licenses').iterdir()):
            if notice.is_file() and notice.suffix in ('.txt', '.md'):
                files['licenses/' + notice.name] = notice.read_bytes()
        # Fixed metadata makes packaging the same inputs repeatable.
        with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as zipped:
            for name, data in sorted(files.items()):
                info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = (0o100755 if name.endswith('.app') else 0o100644) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                zipped.writestr(info, data)
        checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
        archive.with_suffix('.zip.sha256').write_text(f'{checksum}  {archive.name}\n')
        return archive
    finally:
        shutil.rmtree(package)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/readest-sync/release')
    args = parser.parse_args()
    print(release(args.version, args.commit, args.output))
