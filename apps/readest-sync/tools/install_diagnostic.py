#!/usr/bin/env python3
"""Install a separate startup logger without replacing Readest Sync."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
from install_app import digest, PROTECTED
mount=Path('/Volumes/PB743G')
expected=sys.argv[1]
app=mount/'applications/readest-sync.app'
source=Path(__file__).resolve().parents[1]/'diagnostics/readest-sync-debug.app'
target=mount/'applications/readest-sync-debug.app'
if not mount.is_dir() or mount.resolve()!=mount or target.parent.resolve()!=target.parent:
    raise SystemExit('Expected connected PB743G storage')
if digest(app)!=expected or target.exists() or target.is_symlink():
    raise SystemExit('Unexpected installed app or existing diagnostic launcher')
before={p:digest(mount/p) for p in PROTECTED}
content=source.read_bytes()
with target.open('xb') as output:
    output.write(content); output.flush(); os.fsync(output.fileno())
target.chmod(0o755)
assert digest(target)==hashlib.sha256(content).hexdigest()
after={p:digest(mount/p) for p in PROTECTED}
assert after==before and digest(app)==expected
folder=Path(tempfile.mkdtemp(prefix='readest-launch-diagnostic-'))
(folder/'audit.json').write_text(json.dumps({'status':'installed','target':str(target),
    'appUnchanged':expected,'protectedBefore':before,'protectedAfter':after},indent=2)+'\n')
print(folder)
