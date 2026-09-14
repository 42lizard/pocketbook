#!/usr/bin/env python3
"""Optional export check against libraries extracted from our inspected firmware."""
import subprocess
import sys
from pathlib import Path
binary, firmware = map(Path, sys.argv[1:3])
def symbols(path, undefined):
    rows = subprocess.check_output(['arm-linux-gnueabi-readelf', '--wide', '--dyn-syms', str(path)], text=True)
    return {p[7].replace('@@','@') for row in rows.splitlines() if len(p := row.split()) >= 8 and
            p[0].endswith(':') and (p[6] == 'UND') == undefined}
required = symbols(binary, True)
checks = {
    '@OPENSSL_': [firmware / 'qt-runtime/lib/libcrypto.so.3'],
    '@Qt_': list((firmware / 'qt-runtime/lib').glob('libQt6*.so.*')),
    '@LIBXML2_': [firmware / 'runtime/libxml2.so.2.13.8'],
    'curl_': [firmware / 'runtime/libcurl.so.4.8.0'],
}
for version, libraries in checks.items():
    exported = set().union(*(symbols(path,False) for path in libraries))
    needed = {s for s in required if version in s}
    missing = needed - exported
    assert not missing, f'{version}: missing firmware exports: {sorted(missing)}'
    print(f'{version}: {len(needed)} imported symbols match the inspected firmware')
