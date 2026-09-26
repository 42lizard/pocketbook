#!/usr/bin/env python3
"""Native probe checks using the executable supplied by CTest."""
from contextlib import closing
from pathlib import Path
import hashlib
import importlib.util
import json
from test_support import executable
import sqlite3
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

APP = Path(__file__).resolve().parents[1]
BOOK = '/mnt/ext1/Books/Readest/readest-sync-probe.epub'
CFI = 'epubcfi(/6/6[charlie]!/4/4/1:0)'
HASH = '0123456789ABCDEF0123456789ABCDEF'


class ProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.binary = executable()
        cls.work = tempfile.TemporaryDirectory(prefix='readest-probe-tests-')

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def setUp(self):
        self.folder = tempfile.TemporaryDirectory(dir=self.work.name)
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.explorer = self.root / 'explorer.db'
        self.books = self.root / 'books.db'
        # The inner context commits/rolls back; closing() releases the connection.
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.executescript('''
                CREATE TABLE folders(id INTEGER, storageid INTEGER, name TEXT);
                CREATE TABLE files(book_id INTEGER, folder_id INTEGER, storageid INTEGER, filename TEXT, fast_hash BLOB);
                CREATE TABLE books_settings(bookid INTEGER, profileid INTEGER, position TEXT, position_ts INTEGER,
                  cpage INTEGER, npage INTEGER, completed INTEGER);
                INSERT INTO folders VALUES(1,1,'/mnt/ext1/Books/Readest');
            ''')
            db.execute('INSERT INTO files VALUES(1,1,1,?,?)', (Path(BOOK).name, bytes.fromhex(HASH)))
            db.execute('INSERT INTO books_settings VALUES(1,1,?,100,10,100,0)', ('#' + CFI,))
        with closing(sqlite3.connect(self.books)) as db, db:
            db.executescript('''
                CREATE TABLE Items(OID INTEGER, HashUUID TEXT);
                CREATE TABLE Tags(ItemID INTEGER, TagID INTEGER, Val TEXT, TimeEdt INTEGER);
                CREATE TABLE TagNames(OID INTEGER, TagName TEXT);
                INSERT INTO TagNames VALUES(1,'doc.last_read_position');
            ''')
            db.execute('INSERT INTO Items VALUES(1,?)', (HASH,))
            db.execute('INSERT INTO Tags VALUES(1,1,?,100)', ('pbr:/webkit?##' + CFI,))

    def inspect(self, path=BOOK):
        output = subprocess.check_output([str(self.binary), str(self.explorer), str(self.books), path], text=True)
        return json.loads(output)

    def test_native_sync_page_counts(self):
        for current, total, expected in [(19, 207, '[19,207]'), (0, 207, '[0,207]'),
                                         (208, 207, ''), (19, 0, ''), (None, 207, ''), (1.5, 207, '')]:
            with closing(sqlite3.connect(self.explorer)) as db, db:
                db.execute('UPDATE books_settings SET cpage=?, npage=?', (current, total))
            result = subprocess.check_output([str(self.binary), 'native-progress', str(self.explorer), BOOK], text=True)
            self.assertEqual(result.strip(), expected)

    def test_cfi_parser_and_native_database_read_only(self):
        before = [hashlib.sha256(p.read_bytes()).hexdigest() for p in [self.explorer, self.books]]
        result = self.inspect()
        self.assertEqual(result['cfi'], CFI)
        self.assertEqual(result['explorer']['positions'][0]['profileid'], '1')
        self.assertEqual(before, [hashlib.sha256(p.read_bytes()).hexdigest() for p in [self.explorer, self.books]])

    def test_conflicting_databases_do_not_guess(self):
        with closing(sqlite3.connect(self.books)) as db, db:
            db.execute("UPDATE Tags SET Val='pbr:/webkit?##epubcfi(/6/2!/4/2/1:0)'")
        self.assertEqual(self.inspect()['cfi'], '')

    def run_trial(self):
        return subprocess.run([str(self.binary), 'trial', str(self.explorer),
                               str(self.root / 'trial')], capture_output=True, text=True)

    def prepare_trial(self):
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('UPDATE files SET fast_hash=?', (bytes.fromhex('C7D0E520B24461A476557A4C5381DC2E'),))
            db.execute('ALTER TABLE books_settings ADD COLUMN favorite INTEGER DEFAULT 1')
            db.execute('INSERT INTO books_settings SELECT 2,profileid,position,position_ts,cpage,npage,completed,0 FROM books_settings')

    def test_trial_preserves_other_fields_rows_and_small_audit(self):
        self.prepare_trial()
        with closing(sqlite3.connect(self.explorer)) as db, db:
            before = db.execute('SELECT * FROM books_settings ORDER BY bookid').fetchall()
        result = self.run_trial()
        self.assertEqual(result.returncode, 0, result.stderr)
        with closing(sqlite3.connect(self.explorer)) as db, db:
            after = db.execute('SELECT * FROM books_settings ORDER BY bookid').fetchall()
        self.assertEqual(after[0][2], 'pbr:/webkit?##epubcfi(/6/4[bravo]!/4/2)')
        self.assertEqual(after[0][:2], before[0][:2])
        self.assertEqual(after[0][4:], before[0][4:])
        self.assertEqual(after[1], before[1])
        self.assertEqual(list((self.root / 'trial').glob('*.db')), [])
        audit = json.loads((self.root / 'trial/before.json').read_text())
        self.assertEqual(audit[0]['position'], before[0][2])
        self.assertEqual(audit[0]['position_ts'], str(before[0][3]))
        self.assertEqual((self.root / 'trial/outcome.txt').read_text(), 'committed\n')
        self.assertEqual((self.root / 'trial/readest-source-cfi.txt').read_text().strip(),
                         'epubcfi(/6/4[bravo]!/4,/2,/12[BRAVO-05]/1:179)')

    def test_trial_rejects_wrong_identity(self):
        before = self.explorer.read_bytes()
        self.assertNotEqual(self.run_trial().returncode, 0)
        self.assertEqual(self.explorer.read_bytes(), before)

    def test_trial_rejects_unrecognized_trigger(self):
        self.prepare_trial()
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('CREATE TRIGGER unexpected AFTER UPDATE ON books_settings BEGIN UPDATE books_settings SET favorite=0; END')
        before = self.explorer.read_bytes()
        result = self.run_trial()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Unrecognized database trigger', result.stderr)
        self.assertEqual(self.explorer.read_bytes(), before)
        self.assertTrue((self.root / 'trial/outcome.txt').read_text().startswith('rolled back\n'))

    def test_trial_rejects_multiple_profiles(self):
        self.prepare_trial()
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('INSERT INTO books_settings SELECT bookid,2,position,position_ts,cpage,npage,completed,favorite FROM books_settings WHERE bookid=1')
        result = self.run_trial()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('ambiguous', result.stderr)

    def test_trial_missing_source_not_created(self):
        self.explorer.unlink()
        self.assertNotEqual(self.run_trial().returncode, 0)
        self.assertFalse(self.explorer.exists())

    def test_trial_busy_database_preserves_position(self):
        self.prepare_trial()
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('BEGIN IMMEDIATE')
            result = self.run_trial()
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(db.execute('SELECT position FROM books_settings WHERE bookid=1').fetchone()[0], '#' + CFI)

    def test_trial_wal_update_and_native_completion_trigger(self):
        self.prepare_trial()
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('PRAGMA journal_mode=WAL')
            db.execute('ALTER TABLE books_settings ADD COLUMN completed_ts INTEGER DEFAULT 123')
            db.execute("CREATE TRIGGER completed_ts_update AFTER UPDATE ON books_settings WHEN NEW.completed <> OLD.completed BEGIN     UPDATE books_settings SET completed_ts = strftime('%s', 'now') WHERE bookid = NEW.bookid  AND profileid = NEW.profileid; END")
            db.commit()
            result = self.run_trial()
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(db.execute('SELECT completed_ts FROM books_settings WHERE bookid=1').fetchone()[0], 123)
        audit = json.loads((self.root / 'trial/before.json').read_text())
        self.assertEqual(audit[0]['position'], '#' + CFI)
        self.assertEqual(list((self.root / 'trial').glob('*.db')), [])

    def test_multiple_profiles_do_not_guess(self):
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('INSERT INTO books_settings SELECT bookid,2,position,position_ts,cpage,npage,completed FROM books_settings')
        self.assertEqual(self.inspect()['cfi'], '')

    def test_exact_path_and_sql_binding(self):
        self.assertEqual(self.inspect('/mnt/ext1/Other/' + Path(BOOK).name)['cfi'], '')
        self.assertEqual(self.inspect("/mnt/ext1/Books/Readest/' OR 1=1 --")['cfi'], '')

    def test_missing_database_is_not_created(self):
        self.explorer.unlink()
        result = self.inspect()
        self.assertEqual(result['cfi'], '')
        self.assertIn('error', result['explorer'])
        self.assertFalse(self.explorer.exists())

    def test_unsupported_schema_is_reported(self):
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('DROP TABLE files')
        self.assertIn('error', self.inspect()['explorer'])

    def test_wal_snapshot_sees_committed_changes_without_touching_source(self):
        source = sqlite3.connect(self.explorer)
        self.addCleanup(source.close)
        source.execute('PRAGMA journal_mode=WAL')
        source.execute('UPDATE books_settings SET cpage=17')
        source.commit()
        files = [self.explorer, Path(str(self.explorer) + '-wal'), Path(str(self.explorer) + '-shm')]
        before = [p.read_bytes() for p in files]
        destination = self.root / 'snapshot.db'
        output = subprocess.check_output([str(self.binary), 'snapshot', str(self.explorer),
                                          str(destination), str(self.books)], text=True)
        self.assertEqual(json.loads(output)['explorer']['positions'][0]['cpage'], '17')
        self.assertEqual(before, [p.read_bytes() for p in files])

    def test_snapshot_refuses_source_as_destination(self):
        result = subprocess.run([str(self.binary), 'snapshot', str(self.explorer),
                                 str(self.explorer), str(self.books)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)

    def test_managed_native_position_preserves_row(self):
        path = '/mnt/ext1/Books/Readest/81fbcb860e2eed5d223c359063680f87-ABC123/Book-81fbcb86.epub'
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('UPDATE folders SET name=?', (str(Path(path).parent),))
            db.execute('UPDATE files SET filename=?', (Path(path).name,))
            before = db.execute('SELECT * FROM books_settings').fetchone()
        trial = self.root / 'managed-trial'
        result = subprocess.run([str(self.binary), 'native', str(self.explorer), str(trial),
                                 path, 'normal', 'epubcfi(/6/4[bravo]!/4,/2,/12/1:179)'], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        with closing(sqlite3.connect(self.explorer)) as db, db:
            after = db.execute('SELECT * FROM books_settings').fetchone()
        self.assertEqual(after[:2], before[:2])
        self.assertEqual(after[2], 'pbr:/webkit?##epubcfi(/6/4[bravo]!/4/2)')
        self.assertEqual(after[4:], before[4:])
        audit = json.loads((trial / 'before.json').read_text())
        self.assertEqual(audit[0]['position'], before[2])
        self.assertEqual(audit[0]['position_ts'], str(before[3]))
        self.assertEqual(list(trial.glob('*.db')), [])

    def test_percentages_are_read_only_and_independent_of_position_format(self):
        def percentage(path=BOOK):
            before = self.explorer.read_bytes()
            value = float(subprocess.check_output(
                [str(self.binary), 'percentage', str(self.explorer), path], text=True))
            self.assertEqual(self.explorer.read_bytes(), before)
            return value
        self.assertEqual(percentage(), 10)
        self.assertEqual(percentage('/mnt/ext1/Books/missing.epub'), -1)
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute("UPDATE books_settings SET position='unsupported',cpage=25")
        self.assertEqual(percentage(), 25)
        for current,total,expected in [(0,100,0), (100,100,100), (150,100,100),
                                      (-1,100,-1), (5,0,-1), (None,100,-1), ('bad',100,-1)]:
            with closing(sqlite3.connect(self.explorer)) as db, db:
                db.execute('UPDATE books_settings SET cpage=?,npage=?', (current,total))
            self.assertEqual(percentage(), expected)
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('UPDATE books_settings SET cpage=20,npage=100')
            db.execute('INSERT INTO books_settings SELECT bookid,2,position,position_ts,cpage,npage,completed FROM books_settings')
        self.assertEqual(percentage(), -1)

    def test_percentages_stream_large_native_library(self):
        paths = [BOOK] + [f'/mnt/ext1/Books/Readest/book-{i}.epub' for i in range(2, 302)]
        with closing(sqlite3.connect(self.explorer)) as db, db:
            for i, path in enumerate(paths[1:], 2):
                db.execute('INSERT INTO files VALUES(?,1,1,?,?)', (i, Path(path).name, bytes.fromhex(HASH)))
                db.execute('INSERT INTO books_settings VALUES(?,1,?,100,25,100,0)', (i, '#' + CFI))
        before = self.explorer.read_bytes()
        def percentages(requested):
            output = subprocess.check_output([str(self.binary), 'percentage', str(self.explorer), *requested], text=True)
            return [float(line) for line in output.splitlines()]
        # Even one requested book must work with hundreds of unrelated rows.
        self.assertEqual(percentages([BOOK]), [10])
        self.assertEqual(percentages(paths), [10] + [25] * 300)
        self.assertEqual(self.explorer.read_bytes(), before)
        # Streaming must retain the existing identity/profile ambiguity rules.
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('INSERT INTO books_settings SELECT bookid,2,position,position_ts,cpage,npage,completed FROM books_settings WHERE bookid=2')
            db.execute('INSERT INTO files SELECT book_id,folder_id,storageid,filename,? FROM files WHERE book_id=3', (b'x' * 16,))
        self.assertEqual(percentages(paths[:4]), [10, -1, -1, 25])

    def test_native_position_reads_committed_wal_without_writing(self):
        def read(path=BOOK):
            before = self.explorer.read_bytes()
            wal_path = Path(str(self.explorer) + '-wal')
            wal = wal_path.read_bytes()
            result = subprocess.run([str(self.binary), 'native-read', str(self.explorer), path],
                                    capture_output=True, text=True)
            self.assertEqual(self.explorer.read_bytes(), before)
            self.assertEqual(wal_path.read_bytes(), wal)
            return result
        with closing(sqlite3.connect(self.explorer)) as db:
            db.execute('PRAGMA journal_mode=WAL')
            db.execute('UPDATE books_settings SET position_ts=765,cpage=25')
            db.commit()
            self.assertEqual(read().stdout.splitlines(), ['1', '1', '765', '[25,100]'])
            db.execute('UPDATE books_settings SET position_ts=900,cpage=30')
            # An active writer cannot leak uncommitted settings into a read.
            self.assertEqual(read().stdout.splitlines(), ['1', '1', '765', '[25,100]'])
            db.commit()
            self.assertEqual(read().stdout.splitlines(), ['1', '1', '900', '[30,100]'])
            self.assertEqual(read('/missing.epub').stdout.splitlines()[:2], ['0', '0'])
            db.execute('INSERT INTO books_settings SELECT bookid,2,position,position_ts,cpage,npage,completed FROM books_settings')
            db.commit()
            self.assertIn('Multiple native reading profiles', read().stderr)
            db.execute('DELETE FROM books_settings')
            db.commit()
            self.assertEqual(read().stdout.splitlines()[:2], ['1', '0'])
            db.execute('INSERT INTO files SELECT book_id+1,folder_id,storageid,filename,fast_hash FROM files')
            db.commit()
            self.assertIn('Ambiguous native book identity', read().stderr)

    def test_native_transactional_snapshot_includes_wal(self):
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('PRAGMA journal_mode=WAL')
            db.execute('UPDATE books_settings SET position_ts=765')
            db.commit()
            before = self.explorer.read_bytes()
            wal = Path(str(self.explorer) + '-wal').read_bytes()
            destination = self.root / 'consistent.db'
            result = subprocess.run([str(self.binary), 'backup', str(self.explorer), str(destination)], capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(self.explorer.read_bytes(), before)
            self.assertEqual(Path(str(self.explorer) + '-wal').read_bytes(), wal)
            with closing(sqlite3.connect(destination)) as snapshot, snapshot:
                self.assertEqual(snapshot.execute('SELECT position_ts FROM books_settings').fetchone()[0], 765)
                self.assertEqual(snapshot.execute('PRAGMA journal_mode').fetchone()[0], 'delete')
            repeated = subprocess.run([str(self.binary), 'backup', str(self.explorer), str(destination)], capture_output=True)
            self.assertNotEqual(repeated.returncode, 0)

    def test_managed_native_refuses_stale_or_unverified_firmware(self):
        original = self.explorer.read_bytes()
        for mode in ['stale', 'firmware']:
            result = subprocess.run([str(self.binary), 'native', str(self.explorer), str(self.root / mode),
                                     BOOK, mode, 'epubcfi(/6/4!/4/2)'], capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(self.explorer.read_bytes(), original)
        self.assertFalse((self.root / 'firmware').exists())

    def test_managed_native_missing_profile_requires_first_open(self):
        with closing(sqlite3.connect(self.explorer)) as db, db:
            db.execute('DELETE FROM books_settings')
        original = self.explorer.read_bytes()
        result = subprocess.run([str(self.binary), 'native', str(self.explorer), str(self.root / 'empty'),
                                 BOOK, 'normal', 'epubcfi(/6/4!/4/2)'], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.explorer.read_bytes(), original)

    def test_probe_epub_structure_and_reproducibility(self):
        spec = importlib.util.spec_from_file_location('package_probe', APP / 'tools/package_probe.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        first, second = self.root / 'first.epub', self.root / 'second.epub'
        module.create_epub(first)
        module.create_epub(second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(first) as archive:
            self.assertEqual(archive.namelist()[0], 'mimetype')
            self.assertEqual(archive.getinfo('mimetype').compress_type, zipfile.ZIP_STORED)
            self.assertIsNone(archive.testzip())
            for name in archive.namelist():
                if name.endswith(('.xml', '.opf', '.xhtml', '.svg')):
                    ET.fromstring(archive.read(name))
            package = ET.fromstring(archive.read('EPUB/package.opf'))
            # The actual Readest range starts at P + S = /6/4[bravo]!/4/2.
            self.assertEqual(package[2][1].attrib['id'], 'bravo')
            bravo = ET.fromstring(archive.read('EPUB/bravo.xhtml'))
            self.assertEqual(bravo[1][0].text, 'BRAVO')
            self.assertEqual(bravo[1][5].attrib['id'], 'BRAVO-05')
            # CFI /6/6[charlie] selects the third spine item; /4/4/1 selects
            # the first paragraph's text in that XHTML document.
            self.assertEqual(package[2][2].attrib['idref'], 'charlie')
            chapter = ET.fromstring(archive.read('EPUB/charlie.xhtml'))
            self.assertTrue(chapter[1][1].text.startswith('Marker CHARLIE-01.'))


if __name__ == '__main__':
    unittest.main()
