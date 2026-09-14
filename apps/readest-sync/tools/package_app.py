#!/usr/bin/env python3
"""Package only app-owned executable and trust bundle; never install to a device."""
from pathlib import Path
import hashlib
import json
import shutil

APP = Path(__file__).resolve().parents[1]
BUILD = APP.parents[1] / 'build/readest-sync'
CA_HASH = 'f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9'

def main():
    binary = BUILD / 'readest-sync.app'
    ca = APP / 'assets/ca-certificates.crt'
    if not binary.is_file():
        raise SystemExit('First run: docker compose run --rm qt6 make APP=readest-sync check')
    if hashlib.sha256(ca.read_bytes()).hexdigest() != CA_HASH:
        raise SystemExit('CA bundle changed: review its provenance and update the pinned checksum.')
    # New directory prevents stale diagnostic/credential files entering packages.
    import tempfile
    package = Path(tempfile.mkdtemp(prefix='app-package-', dir=BUILD))
    files = {'applications/readest-sync.app': binary,
             'system/readest-sync/ca-certificates.crt': ca}
    manifest = {}
    for relative, source in files.items():
        target = package / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        target.chmod(0o755 if relative.endswith('.app') else 0o644)
        manifest[relative] = hashlib.sha256(target.read_bytes()).hexdigest()
    (package / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(package)
    print('Workspace package only. Runtime and live-account validation are still required.')

if __name__ == '__main__':
    main()
