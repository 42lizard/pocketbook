#!/usr/bin/env python3
"""Validate real EPUB bytes with the same C++ validator used by the app."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import zipfile

APP = Path(__file__).resolve().parents[1]
FLAGS = ['-DMINIZ_NO_DEFLATE_APIS', '-DMINIZ_NO_ZLIB_APIS']
with tempfile.TemporaryDirectory(prefix='readest-integrity-') as folder:
    root = Path(folder)
    obj, binary = root / 'miniz.o', root / 'check'
    subprocess.run([os.environ.get('HOST_CC', 'cc'), '-O2', *FLAGS, '-c',
                    str(APP / 'src/vendor/miniz/miniz.c'), '-o', str(obj)], check=True)
    # macOS system libxml2 is architecture-compatible; Homebrew supplies OpenSSL.
    includes, libraries, json_flags = [], ['-lxml2', '-lcrypto'], []
    if Path('/opt/homebrew/opt/openssl@3').exists():
        sdk = subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
        includes = ['-isystem', sdk + '/usr/include/libxml2',
                    '-isystem', '/opt/homebrew/opt/openssl@3/include']
        libraries += ['-L/opt/homebrew/opt/openssl@3/lib']
        json_flags = ['-isystem', '/opt/homebrew/include', '-L/opt/homebrew/lib']
    else:
        import shlex
        includes = shlex.split(subprocess.check_output(
            ['pkg-config', '--cflags', 'libxml-2.0', 'openssl'], text=True))
    subprocess.run([os.environ.get('HOST_CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                    '-Werror', *FLAGS, *includes, '-I' + str(APP / 'src'),
                    str(APP / 'src/integrity.cpp'), str(APP / 'tests/integrity_test.cpp'),
                    str(obj), '-o', str(binary), *libraries], check=True)
    spec = importlib.util.spec_from_file_location('fixture', APP / 'tools/package_probe.py')
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    original = root / 'original.epub'
    fixture.create_epub(original)
    def convert(pointer, path=original):
        return subprocess.check_output([str(binary), str(path), pointer], text=True).strip()
    assert convert('/body/DocFragment[2]/body/h1') == 'epubcfi(/6/4!/4/2)'
    assert convert('/body[1]/DocFragment[2]/body[1]/p[1]/text()[1].12') == 'epubcfi(/6/4!/4/4/1:12)'
    assert subprocess.run([str(binary), str(original), '/body/DocFragment[99]/body/p'], capture_output=True).returncode == 1
    assert subprocess.run([str(binary), str(original), '/body/DocFragment[2]/body/p[1]/text().999999'], capture_output=True).returncode == 1
    whitespace_book = root / 'whitespace.epub'
    with zipfile.ZipFile(original) as source, zipfile.ZipFile(whitespace_book, 'w') as target:
        for entry in source.infolist():
            content = source.read(entry)
            if entry.filename == 'EPUB/bravo.xhtml':
                content = ('<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">'
                           '<html xmlns="http://www.w3.org/1999/xhtml"><head/><body>\n'
                           '<div>\n  <span>inline</span> \n next 😀  end</div>'
                           '<pre> \n😀  z</pre><p>&nbsp;</p></body></html>').encode()
            target.writestr(entry, content)
    assert convert('/body/DocFragment[2]/body/div/text()[1].8', whitespace_book) == 'epubcfi(/6/4!/4/2/3:10)'
    assert convert('/body/DocFragment[2]/body/pre/text().4', whitespace_book) == 'epubcfi(/6/4!/4/4/1:4)'
    assert convert('/body/DocFragment[2]/body/p/text().1', whitespace_book) == 'epubcfi(/6/4!/4/6/1:1)'
    download_binary = root / 'download-check'
    download_command = [os.environ.get('HOST_CXX', 'c++'), '-std=c++11', '-Wall', '-Wextra',
                    '-Werror', *FLAGS, *includes, '-I' + str(APP / 'src'),
                    *json_flags,
                    *[str(APP / ('src/' + source + '.cpp')) for source in ['integrity', 'download', 'cloud', 'state', 'library', 'progress', 'sync', 'position']],
                    str(APP / 'tests/download_test.cpp'), str(obj), '-o', str(download_binary),
                    *libraries, '-ljson-c', '-lsqlite3']
    subprocess.run(download_command, check=True)
    subprocess.run([str(download_binary), str(root), str(original)], check=True)
    progress_binary = root / 'progress-check'
    progress_command = [str(APP / 'tests/progress_test.cpp') if value == str(APP / 'tests/download_test.cpp')
                        else str(progress_binary) if value == str(download_binary) else value for value in download_command]
    subprocess.run(progress_command, check=True)
    subprocess.run([str(progress_binary), str(root), str(original)], check=True)
    data = original.read_bytes()
    output = subprocess.check_output([str(binary), str(original)], text=True).splitlines()
    assert output == ['81fbcb860e2eed5d223c359063680f87', hashlib.sha256(data).hexdigest(), str(len(data))]
    checks = 1
    def rejected(path):
        global checks
        result = subprocess.run([str(binary), str(path)], capture_output=True)
        assert result.returncode == 1, (path, result.stdout, result.stderr)
        checks += 1
    bad = root / 'bad.epub'
    bad.write_bytes(data[:-30])
    rejected(bad)
    # Corrupt the stored mimetype without changing its expected CRC.
    bad.write_bytes(data.replace(b'application/epub+zip', b'application/epub+bad', 1))
    rejected(bad)
    with zipfile.ZipFile(original) as archive:
        entries = [(i, archive.read(i)) for i in archive.infolist()]
    for extra_name, extra_data in [
        ('../outside', b'x'),
        ('mimetype', b'application/epub+zip'),
        ('META-INF/encryption.xml', b'<encryption><EncryptionMethod Algorithm="DRM"/></encryption>'),
    ]:
        with zipfile.ZipFile(bad, 'w') as archive:
            for info, content in entries:
                archive.writestr(info, content)
            archive.writestr(extra_name, extra_data)
        rejected(bad)
    link = root / 'link.epub'
    link.symlink_to(original)
    rejected(link)
    print(f'{checks} EPUB integrity checks passed.')
    print('Download installation, cleanup and signed-URL retry checks passed.')
    print('Persistent library, account isolation, checkpoints and recovery checks passed.')
    print('Remote progress, upload verification and conflict checks passed.')
