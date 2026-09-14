#!/usr/bin/env python3
"""Create a self-contained diagnostic bundle; never writes to a reader."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

REPO = Path(__file__).resolve().parents[3]
BUILD = REPO / 'build/readest-sync'
TARGET = 'epubcfi(/6/6[charlie]!/4/4/1:0)'


def create_epub(path):
    chapters = [('alpha', 'ALPHA'), ('bravo', 'BRAVO'), ('charlie', 'CHARLIE')]
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        def write(name, data, compression=zipfile.ZIP_DEFLATED):
            entry = zipfile.ZipInfo(name, date_time=(2026, 9, 8, 0, 0, 0))
            entry.compress_type = compression
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)

        write('mimetype', 'application/epub+zip', zipfile.ZIP_STORED)
        write('META-INF/container.xml', '<?xml version="1.0"?>'
              '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">'
              '<rootfiles><rootfile full-path="EPUB/package.opf" '
              'media-type="application/oebps-package+xml"/></rootfiles></container>')
        manifest = ''.join(f'<item id="{key}" href="{key}.xhtml" media-type="application/xhtml+xml"/>'
                           for key, _ in chapters)
        spine = ''.join(f'<itemref id="{key}" idref="{key}"/>' for key, _ in chapters)
        write('EPUB/package.opf', '<?xml version="1.0" encoding="utf-8"?>'
              '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">'
              '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
              '<dc:identifier id="uid">urn:uuid:ac046123-1b98-45c1-b91a-22954656a901</dc:identifier>'
              '<dc:title>Readest Sync — Position Probe</dc:title><dc:creator>PocketBook development</dc:creator>'
              '<dc:language>en</dc:language><meta property="dcterms:modified">2026-09-08T00:00:00Z</meta>'
              '</metadata><manifest>' + manifest +
              '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
              '<item id="image" href="marker.svg" media-type="image/svg+xml"/>'
              '</manifest><spine>' + spine + '</spine></package>')
        nav = ''.join(f'<li><a href="{key}.xhtml">{title}</a></li>' for key, title in chapters)
        write('EPUB/nav.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml" '
              'xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head>'
              '<body><nav epub:type="toc"><h1>Contents</h1><ol>' + nav + '</ol></nav></body></html>')
        write('EPUB/marker.svg', '<svg xmlns="http://www.w3.org/2000/svg" width="240" height="120">'
              '<rect x="2" y="2" width="236" height="116" fill="white" stroke="black" stroke-width="4"/>'
              '<circle cx="120" cy="60" r="35" fill="black"/></svg>')
        for key, title in chapters:
            paragraphs = []
            for number in range(1, 25):
                marker = f'{title}-{number:02d}'
                text = (f'Marker {marker}. This is a disposable test book for reading-position interoperability. '
                        'Record the first visible marker, rather than the page number. '
                        'Changing the font size changes pagination but must not change the passage. '
                        'Unicode sample: café, Straße, Ελληνικά. ')
                if number % 5 == 0:
                    text += '<em>Emphasis and <strong>nested text</strong> test structural positions.</em> '
                if number == 10:
                    text += '<a href="#note" epub:type="noteref">Test footnote</a>. '
                paragraphs.append(f'<p id="{marker}">' + text * 3 + '</p>')
                if number == 12:
                    paragraphs.append('<p><img src="marker.svg" alt="A black circle in a rectangle"/></p>')
            write(f'EPUB/{key}.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml" '
                  'xmlns:epub="http://www.idpf.org/2007/ops"><head><title>' + title + '</title></head>'
                  '<body><h1>' + title + '</h1>' + ''.join(paragraphs) +
                  '<aside id="note" epub:type="footnote"><p>This is the test footnote.</p></aside></body></html>')


def main():
    package = BUILD / 'fixture-package'
    for directory in ['Books/Readest', 'system/readest-sync/probe']:
        (package / directory).mkdir(parents=True, exist_ok=True)
    epub = package / 'Books/Readest/readest-sync-probe.epub'
    create_epub(epub)
    (package / 'system/readest-sync/probe/target-cfi.txt').write_text(TARGET + '\n')
    manifest = {str(path.relative_to(package)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(package.rglob('*')) if path.is_file()}
    (BUILD / 'fixture-package.sha256.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(package)
    print('EPUB SHA-256:', hashlib.sha256(epub.read_bytes()).hexdigest())
    print('Target passage: CHARLIE-01 (synthetic CFI; confirm in both readers).')
    print('Fixture only; use package_app.py to package the current cloud app.')


if __name__ == '__main__':
    main()
