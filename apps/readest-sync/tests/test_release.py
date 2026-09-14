#!/usr/bin/env python3
"""Check release contents and checksums using a disposable build directory."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import package_app
import package_release

with tempfile.TemporaryDirectory(prefix='readest-release-') as folder:
    root = Path(folder)
    package_app.BUILD = root / 'build'
    package_app.BUILD.mkdir()
    binary = package_app.BUILD / 'readest-sync.app'
    binary.write_bytes(b'fixture executable; ARM validity is checked by the build job')
    (package_app.BUILD / 'session.json').write_text('must never ship')
    (package_app.BUILD / 'state.db').write_text('must never ship')
    commit = 'a' * 40
    archive = package_release.release('v0.1.0', commit, root / 'release')
    with zipfile.ZipFile(archive) as zipped:
        names = set(zipped.namelist())
        assert {n for n in names if n.startswith(('applications/', 'system/'))} == {
            'applications/readest-sync.app', 'system/readest-sync/ca-certificates.crt'}
        assert not any('session' in n or n.endswith('.db') or 'prototype' in n for n in names)
        assert {'LICENSE', 'licenses/MPL-2.0.txt', 'README.md', 'release.json',
                'apps/readest-sync/src/vendor/miniz/LICENSE'} <= names
        manifest = json.loads(zipped.read('manifest.json'))
        assert set(manifest) == {'applications/readest-sync.app', 'system/readest-sync/ca-certificates.crt'}
        for name, expected in manifest.items():
            assert hashlib.sha256(zipped.read(name)).hexdigest() == expected
        assert json.loads(zipped.read('release.json'))['commit'] == commit
        assert (zipped.getinfo('applications/readest-sync.app').external_attr >> 16) & 0o111 == 0o111
    assert archive.with_suffix('.zip.sha256').read_text() == hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n'
    repeated = package_release.release('v0.1.0', commit, root / 'repeat')
    assert archive.read_bytes() == repeated.read_bytes()
    for version, revision in [('../escape', commit), ('v1.0', commit), ('v1.0.0', 'bad')]:
        try:
            package_release.release(version, revision, root / 'invalid')
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid release identity accepted')
    assert not list(package_app.BUILD.glob('app-package-*'))
print('Release allowlist, checksums, identity, executable mode and repeatability checks passed.')
