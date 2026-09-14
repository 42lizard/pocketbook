#!/usr/bin/env python3
"""Run installation checks on disposable directories, never on a device."""
from pathlib import Path
import importlib.util
import json
import tempfile

APP = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installer', APP / 'tools/install_app.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
with tempfile.TemporaryDirectory(prefix='readest-install-test-') as work:
    root = Path(work).resolve()
    mount, package = root / 'PB743G', root / 'package'
    for relative in installer.TARGETS + installer.PROTECTED:
        path = mount / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(('existing ' + relative).encode())
    manifest = {}
    for relative in installer.TARGETS:
        path = package / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(('new ' + relative).encode())
        manifest[relative] = installer.digest(path)
    (package / 'manifest.json').write_text(json.dumps(manifest))
    expected = installer.digest(mount / 'applications/readest-sync.app')
    originals = {r: (mount / r).read_bytes() for r in installer.TARGETS + installer.PROTECTED}
    for bad in [dict(manifest, **{'system/config/books.db': 'bad'}),
                dict(manifest, **{'applications/readest-sync.app': 'bad'})]:
        (package / 'manifest.json').write_text(json.dumps(bad))
        try:
            installer.install(package, mount, expected, root)
        except RuntimeError:
            pass
        else:
            raise AssertionError('Unsafe package accepted')
        assert all((mount / r).read_bytes() == data for r, data in originals.items())
    (package / 'manifest.json').write_text(json.dumps(manifest))
    backup = installer.install(package, mount, expected, root)
    assert json.loads((backup / 'audit.json').read_text())['status'] == 'installed'
    for relative in installer.TARGETS:
        assert installer.digest(mount / relative) == manifest[relative]
        assert (backup / relative).read_bytes() == originals[relative]
    for relative in installer.PROTECTED:
        assert (mount / relative).read_bytes() == originals[relative]
    try:
        installer.install(package, mount, expected, root)
    except RuntimeError:
        pass
    else:
        raise AssertionError('Changed installed app was accepted')
    print('Installer allowlist, checksums, backups and protected-file checks passed.')
